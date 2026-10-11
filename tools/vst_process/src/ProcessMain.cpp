#include <WinJACKNexus/Common/StandardChannelNames.hpp>
#include <WinJACKNexus/Common/VstBlockIpc.hpp>
#include <cmath>
/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/utility/memoryibstream.h"

#if WJN_ENABLE_VST2
#include "pluginterfaces/vst2.x/aeffect.h"
#include "pluginterfaces/vst2.x/aeffectx.h"
#endif

#include <jack/jack.h>
#include <jack/midiport.h>

#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifndef WJN_ENABLE_VST2
#define WJN_ENABLE_VST2 0
#endif

#if !WJN_ENABLE_VST2
using VstInt32 = std::int32_t;
#endif

namespace
{
namespace IPC = WinJACKNexus::Common::VstBlockIpc;

constexpr int kArgumentError = 1;
constexpr int kPluginLoadError = 2;
constexpr int kJackError = 3;
constexpr std::string_view kVst3AudioModuleClass = "Audio Module Class";
constexpr std::int32_t kMaxMidiEventsPerBlock = 1024;

DWORD WINAPI watchParentProcess(void *context)
{
    const auto handle = static_cast<HANDLE>(context);
    WaitForSingleObject(handle, INFINITE);
    CloseHandle(handle);
    ExitProcess(kArgumentError);
}
const Steinberg::FUID kIPlugFrameIid(0x367FAF01, 0xAFA94693, 0x8D4DA2A0, 0xED0882A3);

#if WJN_ENABLE_VST2
VstIntPtr currentVst2UniqueId = 0;
double currentVst2SampleRate = 48000.0;
VstIntPtr currentVst2BlockSize = 512;
std::atomic<HWND> currentVst2EditorWindow{};
thread_local VstTimeInfo currentVst2Time{};

bool resizeClientArea(HWND window, SIZE clientSize) noexcept;

VstIntPtr VSTCALLBACK vst2HostCallback(AEffect *, VstInt32 opcode, VstInt32 index, VstIntPtr value,
                                       void *, float)
{
    if (opcode == audioMasterVersion)
        return 2400;
    if (opcode == audioMasterCurrentId)
        return currentVst2UniqueId;
    if (opcode == audioMasterGetTime)
        return reinterpret_cast<VstIntPtr>(&currentVst2Time);
    if (opcode == audioMasterGetSampleRate)
        return static_cast<VstIntPtr>(currentVst2SampleRate);
    if (opcode == audioMasterGetBlockSize)
        return currentVst2BlockSize;
    if (opcode == __audioMasterNeedIdleDeprecated)
        return 1;
    if (opcode == audioMasterSizeWindow)
    {
        const auto window = currentVst2EditorWindow.load();
        return window != nullptr && resizeClientArea(window, {index, static_cast<LONG>(value)}) ? 1
                                                                                                : 0;
    }
    return 0;
}

using Vst2EntryProc = AEffect *(VSTCALLBACK *)(audioMasterCallback);
#endif

struct Options final
{
    std::wstring path;
    std::wstring clientName{L"WinJACKNexus-VST"};
    std::wstring pipeName;
    std::wstring virtualMapping;
    DWORD parentPid{};
    std::wstring internalPlugin;
    std::wstring portDisplayMode{L"default"};
    VstInt32 bypassInputs{};
    VstInt32 bypassOutputs{};
    VstInt32 bypassMidiInputs{};
    std::vector<std::string> bypassInputLabels;
    std::vector<std::string> bypassOutputLabels;
    bool bypass{};
    bool help{};
};

void printUsage()
{
    std::wcout
        << L"Usage: VSTProcess --path <plugin.vst3"
#if WJN_ENABLE_VST2
        << L"|plugin.dll"
#endif
        << L"> --pipe <name> [--client <name>]" << L" [--parent-pid <pid>]"
        << L" [--internal_plugin <name>] [--bypass --bypass-inputs <count> --bypass-outputs <count> --bypass-midi-inputs <count>]\n";
}

bool readOptionValue(int &index, int argc, wchar_t *argv[], std::wstring &value)
{
    if (++index >= argc)
        return false;
    value = argv[index];
    return !value.empty();
}

std::string utf8FromWide(const std::wstring &value);

bool parseOptions(int argc, wchar_t *argv[], Options &options)
{
    for (int index = 1; index < argc; ++index)
    {
        const std::wstring_view argument = argv[index];
        if (argument == L"--help" || argument == L"-h")
        {
            options.help = true;
            return true;
        }
        if (argument == L"--path")
        {
            if (!readOptionValue(index, argc, argv, options.path))
                return false;
            continue;
        }
        if (argument == L"--virtual-blocks-v1")
        {
            if (!readOptionValue(index, argc, argv, options.virtualMapping))
                return false;
            continue;
        }
        if (argument == L"--client")
        {
            if (!readOptionValue(index, argc, argv, options.clientName))
                return false;
            continue;
        }
        if (argument == L"--pipe" || argument == L"--port")
        {
            if (!readOptionValue(index, argc, argv, options.pipeName))
                return false;
            continue;
        }
        if (argument == L"--parent-pid")
        {
            std::wstring value;
            if (!readOptionValue(index, argc, argv, value))
                return false;
            try
            {
                std::size_t used = 0;
                const auto parsed = std::stoull(value, &used);
                if (used != value.size() || parsed == 0 || parsed > MAXDWORD)
                    return false;
                options.parentPid = static_cast<DWORD>(parsed);
            }
            catch (...)
            {
                return false;
            }
            continue;
        }
        if (argument == L"--internal_plugin")
        {
            if (!readOptionValue(index, argc, argv, options.internalPlugin))
                return false;
            continue;
        }
        if (argument == L"--port-display")
        {
            if (!readOptionValue(index, argc, argv, options.portDisplayMode))
                return false;
            continue;
        }
        if (argument == L"--bypass")
        {
            options.bypass = true;
            continue;
        }
        if (argument == L"--bypass-inputs" || argument == L"--bypass-outputs" ||
            argument == L"--bypass-midi-inputs")
        {
            std::wstring value;
            if (!readOptionValue(index, argc, argv, value))
                return false;
            try
            {
                const auto count = std::stoi(value);
                if (count < 0)
                    return false;
                if (argument == L"--bypass-inputs")
                    options.bypassInputs = count;
                else if (argument == L"--bypass-outputs")
                    options.bypassOutputs = count;
                else
                    options.bypassMidiInputs = count;
            }
            catch (...)
            {
                return false;
            }
            continue;
        }
        if (argument == L"--bypass-input-label" || argument == L"--bypass-output-label")
        {
            // Empty plugin labels are valid; preserve their channel positions in bypass.
            if (++index >= argc)
                return false;
            const std::wstring value = argv[index];
            auto &labels = argument == L"--bypass-input-label" ? options.bypassInputLabels
                                                               : options.bypassOutputLabels;
            labels.push_back(utf8FromWide(value));
            continue;
        }
        return false;
    }
    return !options.path.empty();
}

std::string utf8FromWide(const std::wstring &value)
{
    if (value.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(),
                        size, nullptr, nullptr);
    return result;
}

std::wstring wideFromUtf8(std::string_view value)
{
    if (value.empty())
        return {};
    const int size =
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0)
        return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(),
                        size);
    return result;
}

std::wstring normalizedPipeName(std::wstring value)
{
    constexpr std::wstring_view prefix = L"\\\\.\\pipe\\";
    if (value.rfind(prefix, 0) == 0)
        return value;
    return std::wstring(prefix) + value;
}

std::string jsonString(std::string_view value)
{
    std::string result{'"'};
    for (const auto character : value)
    {
        switch (character)
        {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            result += character;
            break;
        }
    }
    result += '"';
    return result;
}

std::string jsonArray(const std::vector<std::string> &values)
{
    std::string result{"["};
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        if (index != 0)
            result += ',';
        result += jsonString(values[index]);
    }
    result += ']';
    return result;
}

struct MidiInputEvent final
{
    std::int32_t portIndex{};
    std::int32_t sampleOffset{};
    std::int32_t sequence{};
    const jack_midi_data_t *data{};
    std::size_t size{};
};

enum class PresetKind : std::uint32_t
{
    vst2 = 1,
    vst3 = 2
};

struct PresetState final
{
    PresetKind kind{};
    std::vector<std::byte> component;
    std::vector<std::byte> controller;
};

class FixedParameterValueQueue final : public Steinberg::Vst::IParamValueQueue
{
  public:
    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override
    {
        return parameterId_;
    }

    Steinberg::int32 PLUGIN_API getPointCount() override
    {
        return pointCount_;
    }

    Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index, Steinberg::int32 &sampleOffset,
                                           Steinberg::Vst::ParamValue &value) override
    {
        if (index < 0 || index >= pointCount_)
            return Steinberg::kResultFalse;
        sampleOffset = points_[index].sampleOffset;
        value = points_[index].value;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 sampleOffset,
                                           Steinberg::Vst::ParamValue value,
                                           Steinberg::int32 &index) override
    {
        auto insertIndex = pointCount_;
        for (Steinberg::int32 point = 0; point < pointCount_; ++point)
        {
            if (points_[point].sampleOffset == sampleOffset)
            {
                points_[point].value = value;
                index = point;
                return Steinberg::kResultTrue;
            }
            if (points_[point].sampleOffset > sampleOffset)
            {
                insertIndex = point;
                break;
            }
        }
        if (pointCount_ >= static_cast<Steinberg::int32>(points_.size()))
            return Steinberg::kResultFalse;
        std::move_backward(points_.begin() + insertIndex, points_.begin() + pointCount_,
                           points_.begin() + pointCount_ + 1);
        points_[insertIndex] = {sampleOffset, value};
        ++pointCount_;
        index = insertIndex;
        return Steinberg::kResultTrue;
    }

    void reset(Steinberg::Vst::ParamID parameterId) noexcept
    {
        parameterId_ = parameterId;
        pointCount_ = 0;
    }

  private:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void **object) override
    {
        if (object == nullptr)
            return Steinberg::kInvalidArgument;
        *object = nullptr;
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IParamValueQueue::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
        {
            *object = this;
            addRef();
            return Steinberg::kResultOk;
        }
        return Steinberg::kNoInterface;
    }

    Steinberg::uint32 PLUGIN_API addRef() override
    {
        return 1;
    }
    Steinberg::uint32 PLUGIN_API release() override
    {
        return 1;
    }

    struct Point final
    {
        Steinberg::int32 sampleOffset{};
        Steinberg::Vst::ParamValue value{};
    };

    Steinberg::Vst::ParamID parameterId_{Steinberg::Vst::kNoParamId};
    Steinberg::int32 pointCount_{};
    std::array<Point, kMaxMidiEventsPerBlock> points_{};
};

class FixedParameterChanges final : public Steinberg::Vst::IParameterChanges
{
  public:
    Steinberg::int32 PLUGIN_API getParameterCount() override
    {
        return parameterCount_;
    }

    Steinberg::Vst::IParamValueQueue *PLUGIN_API getParameterData(Steinberg::int32 index) override
    {
        if (index < 0 || index >= parameterCount_)
            return nullptr;
        return &queues_[index];
    }

    Steinberg::Vst::IParamValueQueue *PLUGIN_API
    addParameterData(const Steinberg::Vst::ParamID &parameterId, Steinberg::int32 &index) override
    {
        for (Steinberg::int32 queue = 0; queue < parameterCount_; ++queue)
        {
            if (queues_[queue].getParameterId() == parameterId)
            {
                index = queue;
                return &queues_[queue];
            }
        }
        if (parameterCount_ >= static_cast<Steinberg::int32>(queues_.size()))
            return nullptr;
        queues_[parameterCount_].reset(parameterId);
        index = parameterCount_++;
        return &queues_[index];
    }

    void clearQueue() noexcept
    {
        parameterCount_ = 0;
    }

  private:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void **object) override
    {
        if (object == nullptr)
            return Steinberg::kInvalidArgument;
        *object = nullptr;
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IParameterChanges::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
        {
            *object = this;
            addRef();
            return Steinberg::kResultOk;
        }
        return Steinberg::kNoInterface;
    }

    Steinberg::uint32 PLUGIN_API addRef() override
    {
        return 1;
    }
    Steinberg::uint32 PLUGIN_API release() override
    {
        return 1;
    }

    Steinberg::int32 parameterCount_{};
    std::array<FixedParameterValueQueue, kMaxMidiEventsPerBlock> queues_{};
};

class AudioPlugin
{
  public:
    virtual ~AudioPlugin() = default;
    virtual void setSamplePosition(uint64_t) noexcept {}
    virtual bool prepare(double, VstInt32, std::string &)
    {
        return true;
    }
    virtual void process(float **, float **, VstInt32, const MidiInputEvent *,
                         VstInt32) noexcept = 0;
    virtual VstInt32 inputCount() const noexcept = 0;
    virtual VstInt32 outputCount() const noexcept = 0;
    virtual VstInt32 midiInputCount() const noexcept
    {
        return 0;
    }
    virtual std::vector<std::string> inputBusNames() const
    {
        return {};
    }
    virtual std::vector<std::string> outputBusNames() const
    {
        return {};
    }
    virtual bool usesBusNamesForDefaultPorts() const noexcept
    {
        return false;
    }
    virtual bool openEditor(std::string &error)
    {
        error = "Plugin editor is not available";
        return false;
    }
    virtual void closeEditor() noexcept {}
    virtual bool getPresetState(PresetState &, std::string &error)
    {
        error = "Plugin presets are not available";
        return false;
    }
    virtual bool setPresetState(const PresetState &, std::string &error)
    {
        error = "Plugin presets are not available";
        return false;
    }
};

class BypassPlugin final : public AudioPlugin
{
  public:
    BypassPlugin(VstInt32 inputCount, VstInt32 outputCount, VstInt32 midiInputCount,
                 bool useBusNamesForDefaultPorts, std::vector<std::string> inputLabels,
                 std::vector<std::string> outputLabels)
        : inputCount_(inputCount), outputCount_(outputCount), midiInputCount_(midiInputCount),
          useBusNamesForDefaultPorts_(useBusNamesForDefaultPorts),
          inputLabels_(std::move(inputLabels)), outputLabels_(std::move(outputLabels))
    {
    }

    void process(float **inputs, float **outputs, VstInt32 frames, const MidiInputEvent *,
                 VstInt32) noexcept override
    {
        for (VstInt32 channel = 0; channel < outputCount_; ++channel)
        {
            auto *output = outputs == nullptr ? nullptr : outputs[channel];
            if (output == nullptr)
                continue;
            const auto *input =
                inputs != nullptr && channel < inputCount_ ? inputs[channel] : nullptr;
            if (input != nullptr)
                std::copy_n(input, frames, output);
            else
                std::fill_n(output, frames, 0.0F);
        }
    }

    VstInt32 inputCount() const noexcept override
    {
        return inputCount_;
    }
    VstInt32 outputCount() const noexcept override
    {
        return outputCount_;
    }
    VstInt32 midiInputCount() const noexcept override
    {
        return midiInputCount_;
    }
    std::vector<std::string> inputBusNames() const override
    {
        return inputLabels_;
    }
    std::vector<std::string> outputBusNames() const override
    {
        return outputLabels_;
    }
    bool usesBusNamesForDefaultPorts() const noexcept override
    {
        return useBusNamesForDefaultPorts_;
    }

  private:
    VstInt32 inputCount_{};
    VstInt32 outputCount_{};
    VstInt32 midiInputCount_{};
    bool useBusNamesForDefaultPorts_{};
    std::vector<std::string> inputLabels_;
    std::vector<std::string> outputLabels_;
};

class EditorWindow final
{
  public:
    ~EditorWindow()
    {
        stop();
    }

    bool start(std::string &error)
    {
        if (dispatcher_ != nullptr)
            return true;
        ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (ready_ == nullptr)
        {
            error = "Unable to create editor synchronization event";
            return false;
        }
        thread_ = std::thread([this] {
            const auto comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            const bool comInitialized = SUCCEEDED(comResult);
            WNDCLASSW windowClass{};
            windowClass.lpfnWndProc = &EditorWindow::windowProcedure;
            windowClass.hInstance = GetModuleHandleW(nullptr);
            windowClass.lpszClassName = L"WinJACKNexusVstEditor";
            RegisterClassW(&windowClass);
            dispatcher_ = CreateWindowExW(0, windowClass.lpszClassName, L"", 0, 0, 0, 0, 0,
                                          HWND_MESSAGE, nullptr, windowClass.hInstance, this);
            SetEvent(ready_);
            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0)
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            dispatcher_ = nullptr;
            if (comInitialized)
                CoUninitialize();
        });
        WaitForSingleObject(ready_, 3000);
        CloseHandle(ready_);
        ready_ = nullptr;
        if (dispatcher_ != nullptr)
            return true;
        if (thread_.joinable())
            thread_.join();
        error = "Unable to start the Win32 editor UI thread";
        return false;
    }

    bool run(std::function<bool()> task, std::string &error)
    {
        if (!start(error))
            return false;
        std::lock_guard lock(taskMutex_);
        taskComplete_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (taskComplete_ == nullptr)
        {
            error = "Unable to create editor task event";
            return false;
        }
        task_ = std::move(task);
        const auto dispatcher = dispatcher_.load();
        if (dispatcher == nullptr || !PostMessageW(dispatcher, kRunTaskMessage, 0, 0) ||
            WaitForSingleObject(taskComplete_, 3000) != WAIT_OBJECT_0)
        {
            CloseHandle(taskComplete_);
            taskComplete_ = nullptr;
            task_ = nullptr;
            error = "The plugin editor UI thread did not complete its task";
            return false;
        }
        CloseHandle(taskComplete_);
        taskComplete_ = nullptr;
        task_ = nullptr;
        return taskResult_;
    }

    void stop() noexcept
    {
        close();
        const auto dispatcher = dispatcher_.load();
        if (dispatcher != nullptr)
            PostMessageW(dispatcher, WM_CLOSE, 0, 0);
        if (thread_.joinable())
            thread_.join();
    }

    static constexpr UINT kAttachEditorMessage = WM_APP + 1;

    bool open(const wchar_t *title, SIZE clientSize, std::function<bool(HWND)> attach,
              std::function<void()> detach, std::function<void()> idle, std::string &error)
    {
        if (dispatcher_ != nullptr)
        {
            const auto opened = run(
                [this, title, clientSize, attach = std::move(attach), detach = std::move(detach),
                 idle = std::move(idle)]() mutable {
                    return openOnUiThread(title, clientSize, std::move(attach), std::move(detach),
                                          std::move(idle));
                },
                error);
            if (!opened && error.empty())
                error = "Unable to attach the plugin editor to a Win32 window";
            return opened;
        }
        if (window_ != nullptr)
        {
            ShowWindow(window_, SW_RESTORE);
            SetForegroundWindow(window_);
            return true;
        }
        close();
        ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (ready_ == nullptr)
        {
            error = "Unable to create editor synchronization event";
            return false;
        }
        attachedReady_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (attachedReady_ == nullptr)
        {
            CloseHandle(ready_);
            ready_ = nullptr;
            error = "Unable to create editor attachment event";
            return false;
        }
        attach_ = std::move(attach);
        detach_ = std::move(detach);
        idle_ = std::move(idle);
        thread_ = std::thread([this, title, clientSize] {
            const auto comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            const bool comInitialized = SUCCEEDED(comResult);
            WNDCLASSW windowClass{};
            windowClass.lpfnWndProc = &EditorWindow::windowProcedure;
            windowClass.hInstance = GetModuleHandleW(nullptr);
            windowClass.lpszClassName = L"WinJACKNexusVstEditor";
            RegisterClassW(&windowClass);
            constexpr DWORD style =
                (WS_OVERLAPPEDWINDOW & ~(WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_VISIBLE;
            RECT windowRect{0, 0, clientSize.cx, clientSize.cy};
            AdjustWindowRectEx(&windowRect, style, FALSE, 0);
            const auto window = CreateWindowExW(
                0, windowClass.lpszClassName, title, style, CW_USEDEFAULT, CW_USEDEFAULT,
                windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, nullptr,
                nullptr, windowClass.hInstance, this);
            window_ = window;
            SetEvent(ready_);
            MSG message{};
            if (window != nullptr)
            {
                while (GetMessageW(&message, nullptr, 0, 0) > 0)
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
            KillTimer(window, 1);
            if (attached_ && detach_)
                detach_();
            attach_ = nullptr;
            detach_ = nullptr;
            idle_ = nullptr;
            window_ = nullptr;
            if (comInitialized)
                CoUninitialize();
        });
        WaitForSingleObject(ready_, 3000);
        CloseHandle(ready_);
        ready_ = nullptr;
        const auto window = window_.load();
        if (window == nullptr || !PostMessageW(window, kAttachEditorMessage, 0, 0) ||
            WaitForSingleObject(attachedReady_, 3000) != WAIT_OBJECT_0)
        {
            if (window != nullptr)
                PostMessageW(window, WM_CLOSE, 0, 0);
            if (thread_.joinable())
                thread_.join();
            CloseHandle(attachedReady_);
            attachedReady_ = nullptr;
            error = "Unable to start the plugin editor on its Win32 UI thread";
            return false;
        }
        CloseHandle(attachedReady_);
        attachedReady_ = nullptr;
        if (!attached_)
        {
            PostMessageW(window, WM_CLOSE, 0, 0);
            if (thread_.joinable())
                thread_.join();
            error = "Unable to attach the plugin editor to a Win32 window";
            return false;
        }
        return true;
    }

    void close() noexcept
    {
        if (dispatcher_ != nullptr)
        {
            std::string ignoredError;
            run(
                [this] {
                    const auto window = window_.load();
                    if (window != nullptr)
                        DestroyWindow(window);
                    return true;
                },
                ignoredError);
            return;
        }
        if (window_ != nullptr)
            PostMessageW(window_, WM_CLOSE, 0, 0);
        if (thread_.joinable())
            thread_.join();
        window_ = nullptr;
        attached_ = false;
    }

  private:
    static constexpr UINT kRunTaskMessage = WM_APP + 2;

    bool openOnUiThread(const wchar_t *title, SIZE clientSize, std::function<bool(HWND)> attach,
                        std::function<void()> detach, std::function<void()> idle)
    {
        if (window_ != nullptr)
        {
            ShowWindow(window_, SW_RESTORE);
            SetForegroundWindow(window_);
            return true;
        }
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = &EditorWindow::windowProcedure;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = L"WinJACKNexusVstEditor";
        RegisterClassW(&windowClass);
        constexpr DWORD style =
            (WS_OVERLAPPEDWINDOW & ~(WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_VISIBLE;
        RECT windowRect{0, 0, clientSize.cx, clientSize.cy};
        AdjustWindowRectEx(&windowRect, style, FALSE, 0);
        attach_ = std::move(attach);
        detach_ = std::move(detach);
        idle_ = std::move(idle);
        const auto window = CreateWindowExW(
            0, windowClass.lpszClassName, title, style, CW_USEDEFAULT, CW_USEDEFAULT,
            windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, nullptr,
            nullptr, windowClass.hInstance, this);
        if (window == nullptr)
        {
            attach_ = nullptr;
            detach_ = nullptr;
            idle_ = nullptr;
            return false;
        }
        window_ = window;
        attached_ = attach_(window);
        if (!attached_)
        {
            DestroyWindow(window);
            return false;
        }
        if (idle_)
            SetTimer(window, 1, 16, nullptr);
        return true;
    }

    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lParam);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto *editor = reinterpret_cast<EditorWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == kRunTaskMessage && editor != nullptr)
        {
            editor->taskResult_ = editor->task_ && editor->task_();
            SetEvent(editor->taskComplete_);
            return 0;
        }
        if (message == kAttachEditorMessage && editor != nullptr)
        {
            editor->attached_ = editor->attach_ && editor->attach_(window);
            if (editor->attached_ && editor->idle_)
                SetTimer(window, 1, 16, nullptr);
            SetEvent(editor->attachedReady_);
            return 0;
        }
        if (message == WM_TIMER && editor != nullptr && editor->idle_)
            editor->idle_();
        if (message == WM_DESTROY && editor != nullptr && window == editor->window_)
        {
            KillTimer(window, 1);
            if (editor->attached_ && editor->detach_)
                editor->detach_();
            editor->attached_ = false;
            editor->attach_ = nullptr;
            editor->detach_ = nullptr;
            editor->idle_ = nullptr;
            editor->window_ = nullptr;
        }
        if (message == WM_DESTROY &&
            (editor == nullptr || editor->dispatcher_ == nullptr || window == editor->dispatcher_))
            PostQuitMessage(0);
        return DefWindowProcW(window, message, wParam, lParam);
    }

    HANDLE ready_{};
    HANDLE attachedReady_{};
    HANDLE taskComplete_{};
    std::thread thread_;
    std::mutex taskMutex_;
    std::function<bool()> task_;
    bool taskResult_{};
    std::atomic<HWND> dispatcher_{};
    std::atomic<HWND> window_{};
    std::atomic_bool attached_{};
    std::function<bool(HWND)> attach_;
    std::function<void()> detach_;
    std::function<void()> idle_;
};

bool resizeClientArea(HWND window, SIZE clientSize) noexcept
{
    if (window == nullptr || clientSize.cx <= 0 || clientSize.cy <= 0)
        return false;
    const auto style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
    const auto extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
    RECT windowRect{0, 0, clientSize.cx, clientSize.cy};
    if (!AdjustWindowRectEx(&windowRect, style, FALSE, extendedStyle))
        return false;
    return SetWindowPos(window, nullptr, 0, 0, windowRect.right - windowRect.left,
                        windowRect.bottom - windowRect.top,
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
}

class Vst3EditorFrame final : public Steinberg::IPlugFrame
{
  public:
    void attach(Steinberg::IPlugView *view, HWND window) noexcept
    {
        view_ = view;
        window_ = window;
    }

    void detach() noexcept
    {
        view_ = nullptr;
        window_ = nullptr;
    }

    Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView *view,
                                             Steinberg::ViewRect *newSize) override
    {
        if (view == nullptr || newSize == nullptr || view != view_ || window_ == nullptr ||
            newSize->getWidth() <= 0 || newSize->getHeight() <= 0)
            return Steinberg::kInvalidArgument;

        Steinberg::ViewRect currentSize{};
        if (view_->getSize(&currentSize) != Steinberg::kResultOk)
            return Steinberg::kInternalError;
        if (currentSize.getWidth() == newSize->getWidth() &&
            currentSize.getHeight() == newSize->getHeight())
            return Steinberg::kResultOk;
        if (!resizeClientArea(window_, {newSize->getWidth(), newSize->getHeight()}))
            return Steinberg::kInternalError;
        return view_->onSize(newSize);
    }

  private:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void **object) override
    {
        if (object == nullptr)
            return Steinberg::kInvalidArgument;
        *object = nullptr;
        if (Steinberg::FUnknownPrivate::iidEqual(iid, kIPlugFrameIid.toTUID()) ||
            Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
        {
            *object = this;
            addRef();
            return Steinberg::kResultOk;
        }
        return Steinberg::kNoInterface;
    }

    Steinberg::uint32 PLUGIN_API addRef() override
    {
        return 1;
    }
    Steinberg::uint32 PLUGIN_API release() override
    {
        return 1;
    }

    Steinberg::IPlugView *view_{};
    HWND window_{};
};

class Vst3Plugin final : public AudioPlugin
{
  public:
    bool load(const std::wstring &path, const std::wstring &internalPlugin, std::string &error)
    {
        if (!editor_.start(error))
            return false;
        return editor_.run([this, &path, &internalPlugin,
                            &error] { return loadOnUiThread(path, internalPlugin, error); },
                           error);
    }

    bool loadOnUiThread(const std::wstring &path, const std::wstring &internalPlugin,
                        std::string &error)
    {
        module_ = VST3::Hosting::Module::create(utf8FromWide(path), error);
        if (!module_)
            return false;

        host_ = Steinberg::owned(new Steinberg::Vst::HostApplication);
        module_->getFactory().setHostContext(host_);
        VST3::Optional<VST3::UID> requestedClassId;
        if (!internalPlugin.empty())
        {
            requestedClassId = VST3::UID::fromString(utf8FromWide(internalPlugin), false);
            if (!requestedClassId)
            {
                error = "VST3 plugin class ID is invalid";
                unloadOnUiThread();
                return false;
            }
        }
        for (const auto &classInfo : module_->getFactory().classInfos())
        {
            if (classInfo.category() != kVst3AudioModuleClass)
                continue;
            if (requestedClassId && classInfo.ID() != *requestedClassId)
                continue;
            Steinberg::Vst::PluginContextFactory::instance().setPluginContext(host_);
            auto provider =
                std::make_unique<Steinberg::Vst::PlugProvider>(module_->getFactory(), classInfo);
            if (!provider->initialize())
                continue;
            component_ = provider->getComponentPtr();
            controller_ = provider->getControllerPtr();
            if (component_)
            {
                processor_ = Steinberg::U::cast<Steinberg::Vst::IAudioProcessor>(component_);
                if (!processor_)
                {
                    component_ = nullptr;
                    controller_ = nullptr;
                    continue;
                }
                inputCount_ = channelCount(Steinberg::Vst::kInput);
                outputCount_ = channelCount(Steinberg::Vst::kOutput);
                eventInputBusCount_ = std::max<Steinberg::int32>(
                    0, component_->getBusCount(Steinberg::Vst::kEvent, Steinberg::Vst::kInput));
                inputBusNames_ = channelBusNames(Steinberg::Vst::kInput);
                outputBusNames_ = channelBusNames(Steinberg::Vst::kOutput);
                if (controller_)
                    controller_->setComponentHandler(&componentHandler_);
                provider_ = std::move(provider);
                return true;
            }
            component_ = nullptr;
            controller_ = nullptr;
        }

        error = requestedClassId ? "VST3 module does not expose the requested audio component"
                                 : "VST3 module does not expose an initializable audio component";
        unloadOnUiThread();
        return false;
    }

    void unload()
    {
        editor_.close();
        std::string ignoredError;
        editor_.run(
            [this] {
                unloadOnUiThread();
                return true;
            },
            ignoredError);
        editor_.stop();
    }

    ~Vst3Plugin()
    {
        unload();
    }

    Steinberg::Vst::IComponent *component() const noexcept
    {
        return component_.get();
    }

    std::vector<std::string> inputBusNames() const override
    {
        return inputBusNames_;
    }
    std::vector<std::string> outputBusNames() const override
    {
        return outputBusNames_;
    }
    bool usesBusNamesForDefaultPorts() const noexcept override
    {
        return true;
    }

    bool prepare(double sampleRate, VstInt32 blockSize, std::string &error) override
    {
        return editor_.run(
            [this, sampleRate, blockSize, &error] {
                if (!component_ || !processor_)
                {
                    error = "VST3 audio processor is not loaded";
                    return false;
                }
                if (processor_->canProcessSampleSize(Steinberg::Vst::kSample32) !=
                    Steinberg::kResultOk)
                {
                    error = "VST3 plugin does not support 32-bit audio processing";
                    return false;
                }

                Steinberg::Vst::ProcessSetup setup{};
                setup.processMode = Steinberg::Vst::kRealtime;
                setup.symbolicSampleSize = Steinberg::Vst::kSample32;
                setup.maxSamplesPerBlock = blockSize;
                setup.sampleRate = sampleRate;

                std::vector<Steinberg::Vst::SpeakerArrangement> inputArrangements;
                std::vector<Steinberg::Vst::SpeakerArrangement> outputArrangements;
                const auto readArrangements =
                    [this](Steinberg::Vst::BusDirection direction,
                           std::vector<Steinberg::Vst::SpeakerArrangement> &arrangements) {
                        const auto busCount =
                            component_->getBusCount(Steinberg::Vst::kAudio, direction);
                        arrangements.resize(busCount);
                        for (Steinberg::int32 busIndex = 0; busIndex < busCount; ++busIndex)
                        {
                            if (processor_->getBusArrangement(direction, busIndex,
                                                              arrangements[busIndex]) !=
                                Steinberg::kResultOk)
                                return false;
                        }
                        return true;
                    };
                if (!readArrangements(Steinberg::Vst::kInput, inputArrangements) ||
                    !readArrangements(Steinberg::Vst::kOutput, outputArrangements))
                {
                    error = "Unable to configure VST3 audio bus layouts";
                    return false;
                }
                const auto arrangementResult = processor_->setBusArrangements(
                    inputArrangements.data(),
                    static_cast<Steinberg::int32>(inputArrangements.size()),
                    outputArrangements.data(),
                    static_cast<Steinberg::int32>(outputArrangements.size()));
                if ((arrangementResult != Steinberg::kResultOk &&
                     arrangementResult != Steinberg::kResultFalse) ||
                    processor_->setupProcessing(setup) != Steinberg::kResultOk)
                {
                    error = "Unable to configure VST3 audio bus layouts";
                    return false;
                }

                bool busesActivated = true;
                for (const auto mediaType : {Steinberg::Vst::kAudio, Steinberg::Vst::kEvent})
                {
                    for (const auto direction : {Steinberg::Vst::kInput, Steinberg::Vst::kOutput})
                    {
                        if (mediaType == Steinberg::Vst::kEvent &&
                            direction == Steinberg::Vst::kOutput)
                            continue;
                        const auto busCount = component_->getBusCount(mediaType, direction);
                        for (Steinberg::int32 busIndex = 0; busIndex < busCount; ++busIndex)
                            busesActivated =
                                component_->activateBus(mediaType, direction, busIndex, true) ==
                                    Steinberg::kResultOk &&
                                busesActivated;
                    }
                }
                if (!busesActivated ||
                    !processData_.prepare(*component_, 0, Steinberg::Vst::kSample32) ||
                    component_->setActive(true) != Steinberg::kResultOk ||
                    processor_->setProcessing(true) != Steinberg::kResultOk)
                {
                    processData_.unprepare();
                    component_->setActive(false);
                    error = "Unable to configure VST3 audio processing";
                    return false;
                }
                prepareMidiMappings();
                inputEvents_.clear();
                inputParameterChanges_.clearQueue();
                processing_ = true;
                processedSamples_ = 0;
                processContext_ = {};
                processContext_.sampleRate = sampleRate;
                processContext_.tempo = 120.0;
                processContext_.state = Steinberg::Vst::ProcessContext::kPlaying |
                                        Steinberg::Vst::ProcessContext::kSystemTimeValid |
                                        Steinberg::Vst::ProcessContext::kContTimeValid |
                                        Steinberg::Vst::ProcessContext::kTempoValid;
                processData_.processContext = &processContext_;
                processData_.inputEvents = eventInputBusCount_ > 0 ? &inputEvents_ : nullptr;
                processData_.inputParameterChanges = &inputParameterChanges_;
                return true;
            },
            error);
    }

    void setSamplePosition(uint64_t position) noexcept override
    {
        processedSamples_ = position;
    }

    void process(float **inputs, float **outputs, VstInt32 frames, const MidiInputEvent *midiEvents,
                 VstInt32 midiEventCount) noexcept override
    {
        if (!processing_ || !processor_)
            return;

        processContext_.projectTimeSamples =
            static_cast<Steinberg::Vst::TSamples>(processedSamples_);
        processContext_.continousTimeSamples =
            static_cast<Steinberg::Vst::TSamples>(processedSamples_);
        processContext_.systemTime = static_cast<Steinberg::int64>(GetTickCount64()) * 1'000'000;
        processData_.numSamples = frames;
        mapAudioBuffers(Steinberg::Vst::kInput, inputs);
        mapAudioBuffers(Steinberg::Vst::kOutput, outputs);
        inputEvents_.clear();
        inputParameterChanges_.clearQueue();
        appendMidiEvents(midiEvents, midiEventCount);
        processor_->process(processData_);
        processedSamples_ += static_cast<Steinberg::uint64>(frames);
    }

    VstInt32 inputCount() const noexcept override
    {
        return inputCount_;
    }

    VstInt32 outputCount() const noexcept override
    {
        return outputCount_;
    }

    VstInt32 midiInputCount() const noexcept override
    {
        return eventInputBusCount_;
    }

    bool openEditor(std::string &error) override
    {
        if (!component_)
        {
            error = "VST3 component is not loaded";
            return false;
        }
        if (!editor_.run([this, &error] { return initializeControllerOnUiThread(error); }, error))
            return false;
        return editor_.open(
            L"WinJACKNexus VST3 Editor", {800, 600},
            [this](HWND parent) { return attachEditor(parent); }, [this] { detachEditor(); }, {},
            error);
    }

    void closeEditor() noexcept override
    {
        editor_.close();
    }

    bool getPresetState(PresetState &state, std::string &error) override
    {
        return editor_.run(
            [this, &state, &error] {
                if (!component_)
                {
                    error = "VST3 component is not loaded";
                    return false;
                }
                Steinberg::ResizableMemoryIBStream componentStream;
                if (component_->getState(&componentStream) != Steinberg::kResultOk)
                {
                    error = "VST3 component did not export its state";
                    return false;
                }
                state.kind = PresetKind::vst3;
                auto componentData = componentStream.take();
                state.component.assign(reinterpret_cast<const std::byte *>(componentData.data()),
                                       reinterpret_cast<const std::byte *>(componentData.data()) +
                                           componentData.size());
                state.controller.clear();
                if (controller_)
                {
                    Steinberg::ResizableMemoryIBStream controllerStream;
                    if (controller_->getState(&controllerStream) != Steinberg::kResultOk)
                    {
                        error = "VST3 controller did not export its state";
                        return false;
                    }
                    auto controllerData = controllerStream.take();
                    state.controller.assign(
                        reinterpret_cast<const std::byte *>(controllerData.data()),
                        reinterpret_cast<const std::byte *>(controllerData.data()) +
                            controllerData.size());
                }
                return true;
            },
            error);
    }

    bool setPresetState(const PresetState &state, std::string &error) override
    {
        return editor_.run(
            [this, &state, &error] {
                if (!component_ || state.kind != PresetKind::vst3 || state.component.empty())
                {
                    error = "The preset does not contain a VST3 component state";
                    return false;
                }
                if (state.component.size() >
                        static_cast<std::size_t>(std::numeric_limits<Steinberg::int32>::max()) ||
                    state.controller.size() >
                        static_cast<std::size_t>(std::numeric_limits<Steinberg::int32>::max()))
                {
                    error = "The preset state is too large";
                    return false;
                }
                Steinberg::ResizableMemoryIBStream componentStream(
                    static_cast<Steinberg::int32>(state.component.size()));
                Steinberg::int32 bytesWritten{};
                componentStream.write(const_cast<std::byte *>(state.component.data()),
                                      static_cast<Steinberg::int32>(state.component.size()),
                                      &bytesWritten);
                componentStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
                if (bytesWritten != static_cast<Steinberg::int32>(state.component.size()) ||
                    component_->setState(&componentStream) != Steinberg::kResultOk)
                {
                    error = "VST3 component rejected the preset";
                    return false;
                }
                if (!controller_)
                    return state.controller.empty();

                Steinberg::ResizableMemoryIBStream componentStateStream(
                    static_cast<Steinberg::int32>(state.component.size()));
                componentStateStream.write(const_cast<std::byte *>(state.component.data()),
                                           static_cast<Steinberg::int32>(state.component.size()),
                                           nullptr);
                componentStateStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
                if (controller_->setComponentState(&componentStateStream) != Steinberg::kResultOk)
                {
                    error = "VST3 controller rejected the component state";
                    return false;
                }
                if (state.controller.empty())
                    return true;
                Steinberg::ResizableMemoryIBStream controllerStream(
                    static_cast<Steinberg::int32>(state.controller.size()));
                controllerStream.write(const_cast<std::byte *>(state.controller.data()),
                                       static_cast<Steinberg::int32>(state.controller.size()),
                                       nullptr);
                controllerStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
                if (controller_->setState(&controllerStream) != Steinberg::kResultOk)
                {
                    error = "VST3 controller rejected the preset";
                    return false;
                }
                return true;
            },
            error);
    }

  private:
    class ComponentHandler final : public Steinberg::Vst::IComponentHandler
    {
      public:
        Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID) override
        {
            return Steinberg::kResultOk;
        }
        Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID,
                                                  Steinberg::Vst::ParamValue) override
        {
            return Steinberg::kResultOk;
        }
        Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID) override
        {
            return Steinberg::kResultOk;
        }
        Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32) override
        {
            return Steinberg::kResultOk;
        }

      private:
        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid,
                                                     void **object) override
        {
            if (object == nullptr)
                return Steinberg::kInvalidArgument;
            *object = nullptr;
            if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler::iid) ||
                Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
            {
                *object = this;
                addRef();
                return Steinberg::kResultOk;
            }
            return Steinberg::kNoInterface;
        }

        Steinberg::uint32 PLUGIN_API addRef() override
        {
            return 1;
        }
        Steinberg::uint32 PLUGIN_API release() override
        {
            return 1;
        }
    };

    bool initializeControllerOnUiThread(std::string &error)
    {
        if (controller_)
            return true;
        Steinberg::TUID controllerClassId{};
        if (component_->getControllerClassId(controllerClassId) != Steinberg::kResultOk)
        {
            error = "VST3 plugin does not provide an edit controller";
            return false;
        }
        controller_ = module_->getFactory().createInstance<Steinberg::Vst::IEditController>(
            controllerClassId);
        if (!controller_ || controller_->initialize(host_) != Steinberg::kResultOk)
        {
            controller_ = nullptr;
            error = "Unable to initialize the VST3 edit controller";
            return false;
        }
        controller_->setComponentHandler(&componentHandler_);
        return true;
    }

    void unloadOnUiThread()
    {
        if (processor_ && processing_)
            processor_->setProcessing(false);
        if (component_ && processing_)
            component_->setActive(false);
        processing_ = false;
        processData_.unprepare();
        view_ = nullptr;
        processor_ = nullptr;
        controller_ = nullptr;
        component_ = nullptr;
        provider_.reset();
        Steinberg::Vst::PluginContextFactory::instance().setPluginContext(nullptr);
        host_ = nullptr;
        module_.reset();
        inputCount_ = 0;
        outputCount_ = 0;
        eventInputBusCount_ = 0;
        midiControllerAssignments_.clear();
    }

    void mapAudioBuffers(Steinberg::Vst::BusDirection direction, float **buffers) noexcept
    {
        const auto busCount =
            direction == Steinberg::Vst::kInput ? processData_.numInputs : processData_.numOutputs;
        std::size_t channelOffset = 0;
        for (Steinberg::int32 busIndex = 0; busIndex < busCount; ++busIndex)
        {
            auto &bus = direction == Steinberg::Vst::kInput ? processData_.inputs[busIndex]
                                                            : processData_.outputs[busIndex];
            bus.silenceFlags = 0;
            for (Steinberg::int32 channel = 0; channel < bus.numChannels; ++channel)
                processData_.setChannelBuffer(direction, busIndex, channel,
                                              buffers == nullptr ? nullptr
                                                                 : buffers[channelOffset++]);
        }
    }

    void prepareMidiMappings()
    {
        constexpr auto channelCount = 16;
        constexpr auto controllerCount = Steinberg::Vst::kCountCtrlNumber;
        midiControllerAssignments_.assign(static_cast<std::size_t>(eventInputBusCount_) *
                                              channelCount * controllerCount,
                                          Steinberg::Vst::kNoParamId);
        if (!controller_)
            return;
        Steinberg::FUnknownPtr<Steinberg::Vst::IMidiMapping> midiMapping(controller_);
        if (!midiMapping)
            return;
        for (Steinberg::int32 busIndex = 0; busIndex < eventInputBusCount_; ++busIndex)
        {
            for (Steinberg::int16 channel = 0; channel < channelCount; ++channel)
            {
                for (Steinberg::int16 controller = 0; controller < controllerCount; ++controller)
                {
                    Steinberg::Vst::ParamID parameterId = Steinberg::Vst::kNoParamId;
                    if (midiMapping->getMidiControllerAssignment(
                            busIndex, channel, controller, parameterId) == Steinberg::kResultTrue)
                    {
                        const auto index =
                            (static_cast<std::size_t>(busIndex) * channelCount + channel) *
                                controllerCount +
                            controller;
                        midiControllerAssignments_[index] = parameterId;
                    }
                }
            }
        }
    }

    Steinberg::Vst::ParamID midiControllerAssignment(Steinberg::int32 busIndex,
                                                     Steinberg::int16 channel,
                                                     Steinberg::int16 controller) const noexcept
    {
        constexpr auto channelCount = 16;
        constexpr auto controllerCount = Steinberg::Vst::kCountCtrlNumber;
        if (busIndex < 0 || busIndex >= eventInputBusCount_ || channel < 0 ||
            channel >= channelCount || controller < 0 || controller >= controllerCount)
            return Steinberg::Vst::kNoParamId;
        const auto index =
            (static_cast<std::size_t>(busIndex) * channelCount + channel) * controllerCount +
            controller;
        return midiControllerAssignments_[index];
    }

    void appendMappedController(Steinberg::int32 busIndex, Steinberg::int16 channel,
                                Steinberg::int16 controller, Steinberg::int32 sampleOffset,
                                Steinberg::Vst::ParamValue value) noexcept
    {
        const auto parameterId = midiControllerAssignment(busIndex, channel, controller);
        if (parameterId == Steinberg::Vst::kNoParamId)
            return;
        Steinberg::int32 queueIndex = 0;
        auto *queue = inputParameterChanges_.addParameterData(parameterId, queueIndex);
        if (queue == nullptr)
            return;
        Steinberg::int32 pointIndex = 0;
        queue->addPoint(sampleOffset, value, pointIndex);
    }

    void appendMidiEvents(const MidiInputEvent *midiEvents, VstInt32 midiEventCount) noexcept
    {
        if (midiEvents == nullptr || midiEventCount <= 0)
            return;
        const auto count = std::min<VstInt32>(midiEventCount, kMaxMidiEventsPerBlock);
        for (VstInt32 index = 0; index < count; ++index)
        {
            const auto &midi = midiEvents[index];
            if (midi.data == nullptr || midi.size == 0 || midi.portIndex < 0 ||
                midi.portIndex >= eventInputBusCount_)
                continue;
            const auto sampleOffset = std::max<std::int32_t>(0, midi.sampleOffset);
            const auto status = midi.data[0];
            if (status == 0xf0)
            {
                Steinberg::Vst::Event event{};
                event.busIndex = midi.portIndex;
                event.sampleOffset = sampleOffset;
                event.ppqPosition = -1.0;
                event.flags = Steinberg::Vst::Event::kIsLive;
                event.type = Steinberg::Vst::Event::kDataEvent;
                event.data.type = Steinberg::Vst::DataEvent::kMidiSysEx;
                event.data.size = static_cast<Steinberg::uint32>(std::min<std::size_t>(
                    midi.size, std::numeric_limits<Steinberg::uint32>::max()));
                event.data.bytes = reinterpret_cast<const Steinberg::uint8 *>(midi.data);
                inputEvents_.addEvent(event);
                continue;
            }
            if (status < 0x80 || status >= 0xf0)
                continue;
            const auto messageType = status & 0xf0;
            const auto channel = static_cast<Steinberg::int16>(status & 0x0f);
            const auto hasTwoDataBytes = messageType != 0xc0 && messageType != 0xd0;
            if (midi.size < (hasTwoDataBytes ? 3u : 2u))
                continue;
            const auto first = midi.data[1];
            const auto second = hasTwoDataBytes ? midi.data[2] : 0;
            switch (messageType)
            {
            case 0x80:
            case 0x90: {
                Steinberg::Vst::Event event{};
                event.busIndex = midi.portIndex;
                event.sampleOffset = sampleOffset;
                event.ppqPosition = -1.0;
                event.flags = Steinberg::Vst::Event::kIsLive;
                if (messageType == 0x90 && second != 0)
                {
                    event.type = Steinberg::Vst::Event::kNoteOnEvent;
                    event.noteOn.channel = channel;
                    event.noteOn.pitch = first;
                    event.noteOn.tuning = 0.0F;
                    event.noteOn.velocity = static_cast<float>(second) / 127.0F;
                    event.noteOn.length = 0;
                    event.noteOn.noteId = -1;
                }
                else
                {
                    event.type = Steinberg::Vst::Event::kNoteOffEvent;
                    event.noteOff.channel = channel;
                    event.noteOff.pitch = first;
                    event.noteOff.velocity = static_cast<float>(second) / 127.0F;
                    event.noteOff.noteId = -1;
                    event.noteOff.tuning = 0.0F;
                }
                inputEvents_.addEvent(event);
                break;
            }
            case 0xa0: {
                Steinberg::Vst::Event event{};
                event.busIndex = midi.portIndex;
                event.sampleOffset = sampleOffset;
                event.ppqPosition = -1.0;
                event.flags = Steinberg::Vst::Event::kIsLive;
                event.type = Steinberg::Vst::Event::kPolyPressureEvent;
                event.polyPressure.channel = channel;
                event.polyPressure.pitch = first;
                event.polyPressure.pressure = static_cast<float>(second) / 127.0F;
                event.polyPressure.noteId = -1;
                inputEvents_.addEvent(event);
                break;
            }
            case 0xb0:
                appendMappedController(midi.portIndex, channel, first, sampleOffset,
                                       static_cast<double>(second) / 127.0);
                break;
            case 0xd0:
                appendMappedController(midi.portIndex, channel, Steinberg::Vst::kAfterTouch,
                                       sampleOffset, static_cast<double>(first) / 127.0);
                break;
            case 0xe0: {
                const auto value = static_cast<Steinberg::int32>(first) |
                                   (static_cast<Steinberg::int32>(second) << 7);
                appendMappedController(midi.portIndex, channel, Steinberg::Vst::kPitchBend,
                                       sampleOffset, static_cast<double>(value) / 16383.0);
                break;
            }
            default:
                break;
            }
        }
    }

    bool attachEditor(HWND parent)
    {
        if (!view_)
            view_ = controller_->createView(Steinberg::Vst::ViewType::kEditor);
        if (!view_ ||
            view_->isPlatformTypeSupported(Steinberg::kPlatformTypeHWND) != Steinberg::kResultOk)
            return false;

        Steinberg::ViewRect viewRect{};
        if (view_->getSize(&viewRect) != Steinberg::kResultOk || viewRect.getWidth() <= 0 ||
            viewRect.getHeight() <= 0)
            return false;
        if (!resizeClientArea(parent, {viewRect.getWidth(), viewRect.getHeight()}))
            return false;

        editorFrame_.attach(view_, parent);
        if (view_->setFrame(&editorFrame_) != Steinberg::kResultOk ||
            view_->attached(parent, Steinberg::kPlatformTypeHWND) != Steinberg::kResultOk)
        {
            view_->setFrame(nullptr);
            editorFrame_.detach();
            return false;
        }
        editorAttached_ = true;
        return true;
    }

    void detachEditor() noexcept
    {
        if (view_ && editorAttached_)
            view_->removed();
        if (view_)
            view_->setFrame(nullptr);
        editorFrame_.detach();
        editorAttached_ = false;
        view_ = nullptr;
    }

    VstInt32 channelCount(Steinberg::Vst::BusDirection direction) const noexcept
    {
        if (!component_)
            return 0;
        VstInt32 channels = 0;
        const auto busCount = component_->getBusCount(Steinberg::Vst::kAudio, direction);
        for (Steinberg::int32 busIndex = 0; busIndex < busCount; ++busIndex)
        {
            Steinberg::Vst::BusInfo bus{};
            if (component_->getBusInfo(Steinberg::Vst::kAudio, direction, busIndex, bus) ==
                Steinberg::kResultOk)
                channels += std::max<Steinberg::int32>(0, bus.channelCount);
        }
        return channels;
    }

    std::vector<std::string> channelBusNames(Steinberg::Vst::BusDirection direction) const
    {
        std::vector<std::string> names;
        if (!component_)
            return names;
        const auto busCount = component_->getBusCount(Steinberg::Vst::kAudio, direction);
        for (Steinberg::int32 busIndex = 0; busIndex < busCount; ++busIndex)
        {
            Steinberg::Vst::BusInfo bus{};
            if (component_->getBusInfo(Steinberg::Vst::kAudio, direction, busIndex, bus) !=
                Steinberg::kResultOk)
                continue;
            const auto name = utf8FromWide(reinterpret_cast<const wchar_t *>(bus.name));
            names.insert(names.end(), std::max<Steinberg::int32>(0, bus.channelCount), name);
        }
        return names;
    }

    VST3::Hosting::Module::Ptr module_;
    Steinberg::IPtr<Steinberg::Vst::HostApplication> host_;
    std::unique_ptr<Steinberg::Vst::PlugProvider> provider_;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component_;
    Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> processor_;
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller_;
    Steinberg::IPtr<Steinberg::IPlugView> view_;
    Steinberg::Vst::HostProcessData processData_;
    Steinberg::Vst::ProcessContext processContext_;
    ComponentHandler componentHandler_;
    Vst3EditorFrame editorFrame_;
    bool editorAttached_{};
    bool processing_{};
    Steinberg::uint64 processedSamples_{};
    VstInt32 inputCount_{};
    VstInt32 outputCount_{};
    VstInt32 eventInputBusCount_{};
    std::vector<std::string> inputBusNames_;
    std::vector<std::string> outputBusNames_;
    std::vector<Steinberg::Vst::ParamID> midiControllerAssignments_;
    Steinberg::Vst::EventList inputEvents_{kMaxMidiEventsPerBlock};
    FixedParameterChanges inputParameterChanges_;
    EditorWindow editor_;
};

#if WJN_ENABLE_VST2
struct Vst2EventBlock final
{
    VstEvents events{};
    std::array<VstEvent *, kMaxMidiEventsPerBlock - 2> extraEventPointers{};
    std::array<VstMidiEvent, kMaxMidiEventsPerBlock> midiEvents{};
    std::array<VstMidiSysexEvent, kMaxMidiEventsPerBlock> sysexEvents{};

    Vst2EventBlock()
    {
        events.reserved = 0;
    }

    void clear() noexcept
    {
        events.numEvents = 0;
    }

    bool append(VstEvent *event) noexcept
    {
        if (events.numEvents >= kMaxMidiEventsPerBlock)
            return false;
        auto *pointers = events.events;
        if (events.numEvents >= 2)
            extraEventPointers[events.numEvents - 2] = event;
        else
            pointers[events.numEvents] = event;
        ++events.numEvents;
        return true;
    }
};

static_assert(offsetof(Vst2EventBlock, extraEventPointers) == sizeof(VstEvents));

class Vst2Plugin final : public AudioPlugin
{
  public:
    bool load(const std::wstring &path, const std::wstring &internalPlugin, std::string &error)
    {
        module_ = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (module_ == nullptr)
        {
            error = "Unable to load VST2 module";
            return false;
        }
        auto entry = reinterpret_cast<Vst2EntryProc>(GetProcAddress(module_, "VSTPluginMain"));
        if (entry == nullptr)
            entry = reinterpret_cast<Vst2EntryProc>(GetProcAddress(module_, "main"));
        if (entry == nullptr)
        {
            error = "VST2 module has no VSTPluginMain/main entry point";
            unload();
            return false;
        }

        if (!internalPlugin.empty())
        {
            try
            {
                currentVst2UniqueId = static_cast<VstIntPtr>(std::stoll(internalPlugin));
            }
            catch (const std::exception &)
            {
                auto *shell = entry(vst2HostCallback);
                if (shell == nullptr || shell->magic != kEffectMagic ||
                    shell->dispatcher == nullptr)
                {
                    error = "VST2 shell returned an invalid AEffect";
                    unload();
                    return false;
                }
                shell->dispatcher(shell, effOpen, 0, 0, nullptr, 0.0F);
                char shellName[kVstMaxProductStrLen + 1]{};
                const auto requestedName = utf8FromWide(internalPlugin);
                while (true)
                {
                    std::fill(std::begin(shellName), std::end(shellName), '\0');
                    const auto uniqueId =
                        shell->dispatcher(shell, effShellGetNextPlugin, 0, 0, shellName, 0.0F);
                    if (uniqueId == 0)
                        break;
                    if (requestedName == shellName)
                    {
                        currentVst2UniqueId = uniqueId;
                        break;
                    }
                }
                shell->dispatcher(shell, effClose, 0, 0, nullptr, 0.0F);
                if (currentVst2UniqueId == 0)
                {
                    error = "VST2 shell does not expose the requested internal_plugin";
                    unload();
                    return false;
                }
            }
        }
        effect_ = entry(vst2HostCallback);
        currentVst2UniqueId = 0;
        if (effect_ == nullptr || effect_->magic != kEffectMagic || effect_->dispatcher == nullptr)
        {
            error = "VST2 module returned an invalid AEffect";
            unload();
            return false;
        }
        effect_->dispatcher(effect_, effOpen, 0, 0, nullptr, 0.0F);
        opened_ = true;
        midiInputCount_ = detectMidiInputCount();
        return true;
    }

    bool prepare(double sampleRate, VstInt32 blockSize, std::string &error) override
    {
        if (effect_ == nullptr)
            return false;
        currentVst2SampleRate = sampleRate;
        currentVst2BlockSize = blockSize;
        effect_->dispatcher(effect_, effSetSampleRate, 0, 0, nullptr,
                            static_cast<float>(sampleRate));
        effect_->dispatcher(effect_, effSetBlockSize, 0, blockSize, nullptr, 0.0F);
        effect_->dispatcher(effect_, effMainsChanged, 0, 1, nullptr, 0.0F);
        processing_ = true;
        if (effect_->processReplacing == nullptr)
        {
            error = "VST2 plugin does not expose processReplacing";
            return false;
        }
        return true;
    }

    void setSamplePosition(uint64_t position) noexcept override
    {
        currentVst2Time.samplePos = double(position);
        currentVst2Time.sampleRate = currentVst2SampleRate;
        currentVst2Time.flags = kVstTransportPlaying;
    }

    void process(float **inputs, float **outputs, VstInt32 frames, const MidiInputEvent *midiEvents,
                 VstInt32 midiEventCount) noexcept override
    {
        if (effect_ == nullptr)
            return;
        midiEventBlock_.clear();
        appendMidiEvents(midiEvents, midiEventCount);
        if (midiEventBlock_.events.numEvents > 0 && effect_->dispatcher != nullptr)
            effect_->dispatcher(effect_, effProcessEvents, 0, 0, &midiEventBlock_.events, 0.0F);
        if (effect_->processReplacing != nullptr)
            effect_->processReplacing(effect_, inputs, outputs, frames);
    }

    VstInt32 inputCount() const noexcept override
    {
        return effect_ == nullptr ? 0 : std::max<VstInt32>(0, effect_->numInputs);
    }
    VstInt32 outputCount() const noexcept override
    {
        return effect_ == nullptr ? 0 : std::max<VstInt32>(0, effect_->numOutputs);
    }
    VstInt32 midiInputCount() const noexcept override
    {
        return midiInputCount_;
    }
    std::vector<std::string> inputBusNames() const override
    {
        return pinLabels(effGetInputProperties, inputCount());
    }
    std::vector<std::string> outputBusNames() const override
    {
        return pinLabels(effGetOutputProperties, outputCount());
    }
    VstInt32 parameterCount() const noexcept
    {
        return effect_ == nullptr ? 0 : std::max<VstInt32>(0, effect_->numParams);
    }

    bool setParameter(VstInt32 index, float value) noexcept
    {
        if (effect_ == nullptr || effect_->setParameter == nullptr || index < 0 ||
            index >= parameterCount())
            return false;
        effect_->setParameter(effect_, index, std::clamp(value, 0.0F, 1.0F));
        return true;
    }

    bool getParameter(VstInt32 index, float &value) const noexcept
    {
        if (effect_ == nullptr || effect_->getParameter == nullptr || index < 0 ||
            index >= parameterCount())
            return false;
        value = effect_->getParameter(effect_, index);
        return true;
    }

    std::vector<std::byte> state() const
    {
        void *data = nullptr;
        const auto size =
            effect_ == nullptr ? 0 : effect_->dispatcher(effect_, effGetChunk, 0, 0, &data, 0.0F);
        if (size <= 0 || data == nullptr)
            return {};
        return {static_cast<const std::byte *>(data), static_cast<const std::byte *>(data) + size};
    }

    bool setState(const void *data, std::size_t size) noexcept
    {
        return effect_ != nullptr &&
               effect_->dispatcher(effect_, effSetChunk, 0, static_cast<VstIntPtr>(size),
                                   const_cast<void *>(data), 0.0F) != 0;
    }

    bool getPresetState(PresetState &preset, std::string &error) override
    {
        preset.kind = PresetKind::vst2;
        preset.component = state();
        if (preset.component.empty())
        {
            error = "VST2 plugin did not export a chunk state";
            return false;
        }
        preset.controller.clear();
        return true;
    }

    bool setPresetState(const PresetState &preset, std::string &error) override
    {
        if (preset.kind != PresetKind::vst2 || preset.component.empty() ||
            !setState(preset.component.data(), preset.component.size()))
        {
            error = "VST2 plugin rejected the preset";
            return false;
        }
        return true;
    }

    bool openEditor(std::string &error) override
    {
        if (effect_ == nullptr || (effect_->flags & effFlagsHasEditor) == 0)
        {
            error = "VST2 plugin does not provide an editor";
            return false;
        }
        editorOpen_ = false;
        return editor_.open(
            L"WinJACKNexus VST2 Editor", {640, 480},
            [this](HWND parent) {
                currentVst2EditorWindow.store(parent);
                effect_->dispatcher(effect_, effEditOpen, 0, 0, parent, 0.0F);
                editorOpen_ = true;
                ERect *editorRect{};
                if (effect_->dispatcher(effect_, effEditGetRect, 0, 0, &editorRect, 0.0F) != 0 &&
                    editorRect != nullptr && editorRect->right > editorRect->left &&
                    editorRect->bottom > editorRect->top)
                {
                    resizeClientArea(parent, {editorRect->right - editorRect->left,
                                              editorRect->bottom - editorRect->top});
                }
                return true;
            },
            [this] {
                if (editorOpen_)
                    effect_->dispatcher(effect_, effEditClose, 0, 0, nullptr, 0.0F);
                editorOpen_ = false;
                currentVst2EditorWindow.store(nullptr);
            },
            [this] { effect_->dispatcher(effect_, effEditIdle, 0, 0, nullptr, 0.0F); }, error);
    }

    void closeEditor() noexcept override
    {
        editor_.close();
    }

    void unload() noexcept
    {
        editor_.close();
        if (effect_ != nullptr && processing_)
            effect_->dispatcher(effect_, effMainsChanged, 0, 0, nullptr, 0.0F);
        processing_ = false;
        if (effect_ != nullptr && opened_)
            effect_->dispatcher(effect_, effClose, 0, 0, nullptr, 0.0F);
        opened_ = false;
        effect_ = nullptr;
        if (module_ != nullptr)
            FreeLibrary(module_);
        module_ = nullptr;
    }

    ~Vst2Plugin()
    {
        unload();
    }

  private:
    VstIntPtr queryCanDo(const char *capability) const noexcept
    {
        if (effect_ == nullptr || effect_->dispatcher == nullptr)
            return 0;
        return effect_->dispatcher(effect_, effCanDo, 0, 0, const_cast<char *>(capability), 0.0F);
    }

    VstInt32 detectMidiInputCount() const noexcept
    {
        if (effect_ == nullptr || effect_->dispatcher == nullptr)
            return 0;
        const auto midiChannels =
            effect_->dispatcher(effect_, effGetNumMidiInputChannels, 0, 0, nullptr, 0.0F);
        if (midiChannels > 0)
            return 1;
        const auto receiveMidi = queryCanDo("receiveVstMidiEvent");
        const auto receiveEvents = queryCanDo("receiveVstEvents");
        const auto category = effect_->dispatcher(effect_, effGetPlugCategory, 0, 0, nullptr, 0.0F);
        const auto isInstrument =
            (effect_->flags & effFlagsIsSynth) != 0 || category == kPlugCategSynth;
        return receiveMidi == 1 || receiveEvents == 1 ||
                       (isInstrument && receiveMidi >= 0 && receiveEvents >= 0)
                   ? 1
                   : 0;
    }

    void appendMidiEvents(const MidiInputEvent *midiEvents, VstInt32 midiEventCount) noexcept
    {
        if (midiEvents == nullptr || midiEventCount <= 0)
            return;
        const auto count = std::min<VstInt32>(midiEventCount, kMaxMidiEventsPerBlock);
        for (VstInt32 index = 0; index < count; ++index)
        {
            const auto &midi = midiEvents[index];
            if (midi.data == nullptr || midi.size == 0)
                continue;
            const auto eventIndex = midiEventBlock_.events.numEvents;
            if (eventIndex >= kMaxMidiEventsPerBlock)
                break;
            const auto deltaFrames = static_cast<VstInt32>(
                std::min<std::size_t>(std::max<std::int32_t>(0, midi.sampleOffset),
                                      std::numeric_limits<VstInt32>::max()));
            if (midi.data[0] == 0xf0)
            {
                auto &event = midiEventBlock_.sysexEvents[eventIndex];
                event = {};
                event.type = kVstSysExType;
                event.byteSize = sizeof(VstMidiSysexEvent);
                event.deltaFrames = deltaFrames;
                event.dumpBytes = static_cast<VstInt32>(
                    std::min<std::size_t>(midi.size, std::numeric_limits<VstInt32>::max()));
                event.sysexDump =
                    reinterpret_cast<char *>(const_cast<jack_midi_data_t *>(midi.data));
                midiEventBlock_.append(reinterpret_cast<VstEvent *>(&event));
                continue;
            }
            auto &event = midiEventBlock_.midiEvents[eventIndex];
            event = {};
            event.type = kVstMidiType;
            event.byteSize = sizeof(VstMidiEvent);
            event.deltaFrames = deltaFrames;
            event.flags = kVstMidiEventIsRealtime;
            const auto size = std::min<std::size_t>(midi.size, sizeof(event.midiData) - 1);
            std::copy_n(midi.data, size, event.midiData);
            midiEventBlock_.append(reinterpret_cast<VstEvent *>(&event));
        }
    }

    std::vector<std::string> pinLabels(VstInt32 opcode, VstInt32 count) const
    {
        std::vector<std::string> labels;
        if (effect_ == nullptr || effect_->dispatcher == nullptr)
            return labels;
        labels.reserve(std::max<VstInt32>(0, count));
        for (VstInt32 index = 0; index < count; ++index)
        {
            VstPinProperties properties{};
            if (effect_->dispatcher(effect_, opcode, index, 0, &properties, 0.0F) == 0)
            {
                labels.emplace_back();
                continue;
            }
            const auto end =
                std::find(std::begin(properties.label), std::end(properties.label), '\0');
            labels.emplace_back(properties.label, end);
        }
        return labels;
    }

    HMODULE module_{};
    AEffect *effect_{};
    bool opened_{};
    bool processing_{};
    bool editorOpen_{};
    VstInt32 midiInputCount_{};
    Vst2EventBlock midiEventBlock_;
    EditorWindow editor_;
};
#endif

#include "VirtualRuntime.inc"

class JackRuntime final
{
  public:
    ~JackRuntime()
    {
        close();
    }

    bool open(const std::wstring &requestedClientName, const std::wstring &portDisplayMode,
              AudioPlugin &plugin, std::string &error)
    {
        if (!loadApi(error))
            return false;

        const auto clientName = utf8FromWide(requestedClientName);
        jack_status_t status = JackFailure;
        client_ = clientOpen_(clientName.empty() ? "WinJACKNexus-VST" : clientName.c_str(),
                              JackNoStartServer, &status);
        if (client_ == nullptr)
        {
            error = "Unable to open JACK client, status=" + std::to_string(status);
            return false;
        }
        if (setProcessCallback_(client_, &JackRuntime::process, this) != 0)
        {
            error = "Unable to set JACK process callback";
            return false;
        }
        if (!registerAudioPorts(plugin, portDisplayMode, error))
            return false;
        if (!registerMidiPorts(plugin, error))
            return false;
        if (!plugin.prepare(sampleRate_(client_), static_cast<VstInt32>(bufferSize_(client_)),
                            error))
            return false;
        plugin_ = &plugin;
        if (activate_(client_) != 0)
        {
            error = "Unable to activate JACK client";
            return false;
        }
        active_ = true;
        return true;
    }

    void close() noexcept
    {
        deactivate();
        if (client_ != nullptr)
            clientClose_(client_);
        client_ = nullptr;
        plugin_ = nullptr;
        audioInputs_.clear();
        audioOutputs_.clear();
        midiInputs_.clear();
        inputBuffers_.clear();
        outputBuffers_.clear();
        midiInputBuffers_.clear();
        midiEventCount_ = 0;
        if (library_ != nullptr)
            FreeLibrary(library_);
        library_ = nullptr;
    }

    bool isActive() const noexcept
    {
        return active_;
    }

    void deactivate() noexcept
    {
        if (client_ != nullptr && active_)
            deactivate_(client_);
        active_ = false;
    }

  private:
    using ClientOpen = jack_client_t *(*)(const char *, jack_options_t, jack_status_t *, ...);
    using ClientClose = int (*)(jack_client_t *);
    using Activate = int (*)(jack_client_t *);
    using Deactivate = int (*)(jack_client_t *);
    using SetProcessCallback = int (*)(jack_client_t *, JackProcessCallback, void *);
    using PortRegister = jack_port_t *(*)(jack_client_t *, const char *, const char *,
                                          unsigned long, unsigned long);
    using PortGetBuffer = void *(*)(jack_port_t *, jack_nframes_t);
    using MidiGetEventCount = uint32_t (*)(void *);
    using MidiEventGet = int (*)(jack_midi_event_t *, void *, uint32_t);
    using SampleRate = jack_nframes_t (*)(jack_client_t *);
    using BufferSize = jack_nframes_t (*)(jack_client_t *);

    bool loadApi(std::string &error)
    {
        if (library_ != nullptr)
            return true;

        constexpr const wchar_t *candidates64[]{L"libjack64.dll", L"libjack.dll"};
        constexpr const wchar_t *candidates32[]{L"libjack.dll"};
        const auto &candidates = sizeof(void *) == 8 ? candidates64 : candidates32;
        const auto candidateCount =
            sizeof(void *) == 8 ? std::size(candidates64) : std::size(candidates32);
        for (std::size_t index = 0; index < candidateCount && library_ == nullptr; ++index)
            library_ = LoadLibraryW(candidates[index]);
        if (library_ == nullptr)
        {
            error = "Unable to load JACK runtime library";
            return false;
        }

        clientOpen_ = loadFunction<ClientOpen>("jack_client_open");
        clientClose_ = loadFunction<ClientClose>("jack_client_close");
        activate_ = loadFunction<Activate>("jack_activate");
        deactivate_ = loadFunction<Deactivate>("jack_deactivate");
        setProcessCallback_ = loadFunction<SetProcessCallback>("jack_set_process_callback");
        portRegister_ = loadFunction<PortRegister>("jack_port_register");
        portGetBuffer_ = loadFunction<PortGetBuffer>("jack_port_get_buffer");
        midiGetEventCount_ = loadFunction<MidiGetEventCount>("jack_midi_get_event_count");
        midiEventGet_ = loadFunction<MidiEventGet>("jack_midi_event_get");
        sampleRate_ = loadFunction<SampleRate>("jack_get_sample_rate");
        bufferSize_ = loadFunction<BufferSize>("jack_get_buffer_size");
        if (clientOpen_ != nullptr && clientClose_ != nullptr && activate_ != nullptr &&
            deactivate_ != nullptr && setProcessCallback_ != nullptr && portRegister_ != nullptr &&
            portGetBuffer_ != nullptr && sampleRate_ != nullptr && bufferSize_ != nullptr)
            return true;

        error = "JACK runtime library is missing a required client API";
        close();
        return false;
    }

    template <typename Function> Function loadFunction(const char *name) const noexcept
    {
        return reinterpret_cast<Function>(GetProcAddress(library_, name));
    }

    bool registerAudioPorts(const AudioPlugin &plugin, const std::wstring &portDisplayMode,
                            std::string &error)
    {
        const auto mode = utf8FromWide(portDisplayMode);
        const auto registerDirection = [&](VstInt32 count, const std::vector<std::string> &busNames,
                                           unsigned long jackFlags, const char *prefix,
                                           std::vector<jack_port_t *> &ports) {
            for (VstInt32 channel = 0; channel < count; ++channel)
            {
                const auto channelNumber = std::to_string(channel + 1);
                std::string name;
                if (mode == "standard")
                {
                    name = std::string(prefix) + '_' +
                           WinJACKNexus::Common::standardChannelName(
                               static_cast<std::size_t>(channel), static_cast<std::size_t>(count));
                }
                else if (mode == "bus-labels")
                {
                    const auto busName = channel < static_cast<VstInt32>(busNames.size())
                                             ? sanitizedPortName(busNames[channel])
                                             : std::string{};
                    name = busName.empty()
                               ? std::string(prefix) + '_' + channelNumber
                               : std::string(prefix) + '_' + busName + '_' + channelNumber;
                }
                else if (mode == "default")
                {
                    const auto busName = plugin.usesBusNamesForDefaultPorts() &&
                                                 channel < static_cast<VstInt32>(busNames.size())
                                             ? sanitizedPortName(busNames[channel])
                                             : std::string{};
                    name = busName.empty() ? std::string(prefix) + '_' + channelNumber
                                           : busName + '_' + channelNumber;
                }
                else
                {
                    name = std::string(prefix) + '_' + channelNumber;
                }
                auto *port =
                    portRegister_(client_, name.c_str(), JACK_DEFAULT_AUDIO_TYPE, jackFlags, 0);
                if (port == nullptr)
                    return false;
                ports.push_back(port);
            }
            return true;
        };

        if (!registerDirection(plugin.inputCount(), plugin.inputBusNames(), JackPortIsInput, "in",
                               audioInputs_) ||
            !registerDirection(plugin.outputCount(), plugin.outputBusNames(), JackPortIsOutput,
                               "out", audioOutputs_))
        {
            error = "Unable to register JACK audio ports";
            return false;
        }
        inputBuffers_.resize(audioInputs_.size());
        outputBuffers_.resize(audioOutputs_.size());
        return true;
    }

    bool registerMidiPorts(const AudioPlugin &plugin, std::string &error)
    {
        const auto count = plugin.midiInputCount();
        if (count <= 0)
            return true;
        if (midiGetEventCount_ == nullptr || midiEventGet_ == nullptr)
        {
            error = "JACK runtime library is missing the MIDI input API";
            return false;
        }
        for (VstInt32 portIndex = 0; portIndex < count; ++portIndex)
        {
            const auto name = portIndex == 0 ? std::string("midi_in")
                                             : "midi_in_" + std::to_string(portIndex + 1);
            auto *port =
                portRegister_(client_, name.c_str(), JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
            if (port == nullptr)
            {
                error = "Unable to register JACK MIDI input ports";
                return false;
            }
            midiInputs_.push_back(port);
        }
        midiInputBuffers_.resize(midiInputs_.size());
        return true;
    }

    static std::string sanitizedPortName(std::string name)
    {
        for (auto &character : name)
        {
            if (character == ':' || character == '\\' || character == '/')
                character = '_';
        }
        return name;
    }

    static int process(jack_nframes_t frames, void *context) noexcept
    {
        auto *runtime = static_cast<JackRuntime *>(context);
        for (std::size_t index = 0; index < runtime->audioInputs_.size(); ++index)
            runtime->inputBuffers_[index] =
                static_cast<float *>(runtime->portGetBuffer_(runtime->audioInputs_[index], frames));
        for (std::size_t index = 0; index < runtime->audioOutputs_.size(); ++index)
        {
            runtime->outputBuffers_[index] = static_cast<float *>(
                runtime->portGetBuffer_(runtime->audioOutputs_[index], frames));
            if (runtime->outputBuffers_[index] != nullptr)
                std::fill(runtime->outputBuffers_[index], runtime->outputBuffers_[index] + frames,
                          0.0F);
        }
        runtime->midiEventCount_ = 0;
        for (std::size_t portIndex = 0; portIndex < runtime->midiInputs_.size(); ++portIndex)
        {
            runtime->midiInputBuffers_[portIndex] =
                runtime->portGetBuffer_(runtime->midiInputs_[portIndex], frames);
            auto *portBuffer = runtime->midiInputBuffers_[portIndex];
            if (portBuffer == nullptr)
                continue;
            const auto eventCount = runtime->midiGetEventCount_(portBuffer);
            for (uint32_t eventIndex = 0; eventIndex < eventCount; ++eventIndex)
            {
                if (runtime->midiEventCount_ >= kMaxMidiEventsPerBlock)
                    break;
                jack_midi_event_t event{};
                if (runtime->midiEventGet_(&event, portBuffer, eventIndex) != 0)
                    continue;
                const auto eventPosition = runtime->midiEventCount_;
                runtime->midiEvents_[eventPosition] = {
                    static_cast<std::int32_t>(portIndex),
                    static_cast<std::int32_t>(std::min<jack_nframes_t>(event.time, frames)),
                    eventPosition, event.buffer, event.size};
                ++runtime->midiEventCount_;
            }
        }
        if (runtime->midiEventCount_ > 1)
        {
            std::sort(runtime->midiEvents_.begin(),
                      runtime->midiEvents_.begin() + runtime->midiEventCount_,
                      [](const MidiInputEvent &left, const MidiInputEvent &right) {
                          if (left.sampleOffset != right.sampleOffset)
                              return left.sampleOffset < right.sampleOffset;
                          if (left.portIndex != right.portIndex)
                              return left.portIndex < right.portIndex;
                          return left.sequence < right.sequence;
                      });
        }
        if (runtime->plugin_ != nullptr)
        {
            runtime->plugin_->setSamplePosition(runtime->processedSamples_);
            runtime->plugin_->process(runtime->inputBuffers_.data(), runtime->outputBuffers_.data(),
                                      static_cast<VstInt32>(frames), runtime->midiEvents_.data(),
                                      runtime->midiEventCount_);
            runtime->processedSamples_ += frames;
        }
        return 0;
    }

    HMODULE library_{};
    uint64_t processedSamples_{};
    jack_client_t *client_{};
    bool active_{};
    ClientOpen clientOpen_{};
    ClientClose clientClose_{};
    Activate activate_{};
    Deactivate deactivate_{};
    SetProcessCallback setProcessCallback_{};
    PortRegister portRegister_{};
    PortGetBuffer portGetBuffer_{};
    MidiGetEventCount midiGetEventCount_{};
    MidiEventGet midiEventGet_{};
    SampleRate sampleRate_{};
    BufferSize bufferSize_{};
    AudioPlugin *plugin_{};
    std::vector<jack_port_t *> audioInputs_;
    std::vector<jack_port_t *> audioOutputs_;
    std::vector<jack_port_t *> midiInputs_;
    std::vector<float *> inputBuffers_;
    std::vector<float *> outputBuffers_;
    std::vector<void *> midiInputBuffers_;
    std::array<MidiInputEvent, kMaxMidiEventsPerBlock> midiEvents_{};
    VstInt32 midiEventCount_{};
};

class PipeServer final
{
  public:
    PipeServer(const std::wstring &name, JackRuntime &jack, AudioPlugin &plugin,
               VirtualRuntime *virtualRuntime = nullptr)
        : name_(normalizedPipeName(name)), jack_(jack), plugin_(plugin),
          virtualRuntime_(virtualRuntime)
    {
    }

    int run()
    {
        while (!stop_)
        {
            const HANDLE pipe = CreateNamedPipeW(
                name_.c_str(), PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
            if (pipe == INVALID_HANDLE_VALUE)
                return kJackError;
            const BOOL connected =
                ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
            if (connected)
                handleClient(pipe);
            CloseHandle(pipe);
        }
        return 0;
    }

  private:
    static bool writePresetFile(const std::wstring &path, AudioPlugin &plugin, std::string &error)
    {
        PresetState state;
        if (!plugin.getPresetState(state, error))
            return false;
        constexpr std::array<char, 8> magic{'W', 'J', 'N', 'P', 'S', 'T', '1', '\0'};
        constexpr std::uint32_t version = 1;
        const auto componentSize = static_cast<std::uint64_t>(state.component.size());
        const auto controllerSize = static_cast<std::uint64_t>(state.controller.size());
        std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
        if (!file)
        {
            error = "Unable to create the preset file";
            return false;
        }
        const auto write = [&file](const auto &value) {
            file.write(reinterpret_cast<const char *>(&value), sizeof(value));
        };
        file.write(magic.data(), static_cast<std::streamsize>(magic.size()));
        write(version);
        write(state.kind);
        write(componentSize);
        write(controllerSize);
        file.write(reinterpret_cast<const char *>(state.component.data()),
                   static_cast<std::streamsize>(state.component.size()));
        file.write(reinterpret_cast<const char *>(state.controller.data()),
                   static_cast<std::streamsize>(state.controller.size()));
        if (!file)
        {
            error = "Unable to write the preset file";
            return false;
        }
        return true;
    }

    static bool readPresetFile(const std::wstring &path, AudioPlugin &plugin, std::string &error)
    {
        constexpr std::array<char, 8> magic{'W', 'J', 'N', 'P', 'S', 'T', '1', '\0'};
        constexpr std::uint32_t version = 1;
        constexpr std::uint64_t maximumStateSize = 256ull * 1024 * 1024;
        std::ifstream file(std::filesystem::path(path), std::ios::binary);
        std::array<char, magic.size()> fileMagic{};
        std::uint32_t fileVersion{};
        PresetState state;
        std::uint64_t componentSize{};
        std::uint64_t controllerSize{};
        const auto read = [&file](auto &value) {
            file.read(reinterpret_cast<char *>(&value), sizeof(value));
        };
        file.read(fileMagic.data(), static_cast<std::streamsize>(fileMagic.size()));
        read(fileVersion);
        read(state.kind);
        read(componentSize);
        read(controllerSize);
        if (!file || fileMagic != magic || fileVersion != version ||
            componentSize > maximumStateSize || controllerSize > maximumStateSize ||
            componentSize + controllerSize > maximumStateSize)
        {
            error = "The preset file is invalid or unsupported";
            return false;
        }
        state.component.resize(static_cast<std::size_t>(componentSize));
        state.controller.resize(static_cast<std::size_t>(controllerSize));
        file.read(reinterpret_cast<char *>(state.component.data()),
                  static_cast<std::streamsize>(state.component.size()));
        file.read(reinterpret_cast<char *>(state.controller.data()),
                  static_cast<std::streamsize>(state.controller.size()));
        if (!file)
        {
            error = "The preset file is incomplete";
            return false;
        }
        return plugin.setPresetState(state, error);
    }

    void handleClient(HANDLE pipe)
    {
        char request[4096]{};
        DWORD bytesRead{};
        if (!ReadFile(pipe, request, sizeof(request) - 1, &bytesRead, nullptr))
            return;
        const std::string_view command(request, bytesRead);
        std::unique_lock<std::mutex> control;
        if (virtualRuntime_)
            control = std::unique_lock<std::mutex>(virtualRuntime_->controlMutex());
        const char *response = "{\"ok\":false,\"error\":\"unknown command\"}\n";
        if (command.find("ping") != std::string_view::npos)
            response = "{\"ok\":true,\"reply\":\"pong\"}\n";
        else if (command.find("status") != std::string_view::npos)
        {
            responseStorage_ =
                (virtualRuntime_ ? virtualRuntime_->isActive() : jack_.isActive())
                    ? "{\"ok\":true,\"active\":true,\"inputs\":" +
                          std::to_string(plugin_.inputCount()) +
                          ",\"outputs\":" + std::to_string(plugin_.outputCount()) +
                          ",\"midiInputs\":" + std::to_string(plugin_.midiInputCount()) +
                          ",\"inputLabels\":" + jsonArray(plugin_.inputBusNames()) +
                          ",\"outputLabels\":" + jsonArray(plugin_.outputBusNames()) +
                          ",\"virtualProtocol\":" +
                          std::to_string(virtualRuntime_ ? IPC::version : 0) + "}\n"
                    : "{\"ok\":true,\"active\":false}\n";
            response = responseStorage_.c_str();
        }
        else if (command.find("reset-blocks") != std::string_view::npos && virtualRuntime_)
        {
            std::string error;
            responseStorage_ = virtualRuntime_->reset(error)
                                   ? "{\"ok\":true}\n"
                                   : "{\"ok\":false,\"error\":" + jsonString(error) + "}\n";
            response = responseStorage_.c_str();
        }
        else if (command.find("open-editor") != std::string_view::npos)
        {
            std::string error;
            responseStorage_ = plugin_.openEditor(error)
                                   ? "{\"ok\":true,\"editor\":\"separate-window\"}\n"
                                   : "{\"ok\":false,\"error\":\"" + error + "\"}\n";
            response = responseStorage_.c_str();
        }
        else if (command.rfind("save-preset ", 0) == 0)
        {
            std::string error;
            const auto commandEnd = command.find('\n');
            const auto path = wideFromUtf8(
                command.substr(std::string_view("save-preset ").size(),
                               commandEnd == std::string_view::npos
                                   ? std::string_view::npos
                                   : commandEnd - std::string_view("save-preset ").size()));
            if (path.empty())
                error = "The preset path is invalid";
            responseStorage_ = !path.empty() && writePresetFile(path, plugin_, error)
                                   ? "{\"ok\":true}\n"
                                   : "{\"ok\":false,\"error\":" + jsonString(error) + "}\n";
            response = responseStorage_.c_str();
        }
        else if (command.rfind("load-preset ", 0) == 0)
        {
            std::string error;
            const auto commandEnd = command.find('\n');
            const auto path = wideFromUtf8(
                command.substr(std::string_view("load-preset ").size(),
                               commandEnd == std::string_view::npos
                                   ? std::string_view::npos
                                   : commandEnd - std::string_view("load-preset ").size()));
            if (path.empty())
                error = "The preset path is invalid";
            responseStorage_ = !path.empty() && readPresetFile(path, plugin_, error)
                                   ? "{\"ok\":true}\n"
                                   : "{\"ok\":false,\"error\":" + jsonString(error) + "}\n";
            response = responseStorage_.c_str();
        }
        else if (command.find("quit") != std::string_view::npos)
        {
            stop_ = true;
            response = "{\"ok\":true,\"stopping\":true}\n";
        }
        DWORD bytesWritten{};
        WriteFile(pipe, response, static_cast<DWORD>(std::char_traits<char>::length(response)),
                  &bytesWritten, nullptr);
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
    }

    std::wstring name_;
    JackRuntime &jack_;
    AudioPlugin &plugin_;
    VirtualRuntime *virtualRuntime_{};
    std::atomic<bool> stop_{};
    std::string responseStorage_;
};

}

int wmain(int argc, wchar_t *argv[])
{
    Options options;
    if (!parseOptions(argc, argv, options))
    {
        printUsage();
        return kArgumentError;
    }
    if (options.help)
    {
        printUsage();
        return 0;
    }
    if (options.pipeName.empty())
    {
        std::wcerr << L"VSTProcess requires --pipe so its JACK client has a controlled lifetime.\n";
        printUsage();
        return kArgumentError;
    }
    if (options.parentPid != 0)
    {
        const auto parent = OpenProcess(SYNCHRONIZE, FALSE, options.parentPid);
        if (!parent)
            return kArgumentError;
        const auto watcher = CreateThread(nullptr, 0, &watchParentProcess, parent, 0, nullptr);
        if (!watcher)
        {
            CloseHandle(parent);
            return kArgumentError;
        }
        CloseHandle(watcher);
    }
    const auto extension = std::filesystem::path(options.path).extension().wstring();
    std::unique_ptr<AudioPlugin> plugin;
    std::string error;
    if (options.bypass)
    {
        plugin = std::make_unique<BypassPlugin>(options.bypassInputs, options.bypassOutputs,
                                                options.bypassMidiInputs, extension == L".vst3",
                                                std::move(options.bypassInputLabels),
                                                std::move(options.bypassOutputLabels));
    }
    else if (extension == L".vst3")
    {
        auto vst3 = std::make_unique<Vst3Plugin>();
        if (!vst3->load(options.path, options.internalPlugin, error))
        {
            std::cerr << "Unable to load VST3: " << error << '\n';
            return kPluginLoadError;
        }
        plugin = std::move(vst3);
#if WJN_ENABLE_VST2
    }
    else if (extension == L".dll")
    {
        auto vst2 = std::make_unique<Vst2Plugin>();
        if (!vst2->load(options.path, options.internalPlugin, error))
        {
            std::cerr << "Unable to load VST2: " << error << '\n';
            return kPluginLoadError;
        }
        plugin = std::move(vst2);
#endif
    }
    else
    {
        std::wcerr << L"Unsupported plugin path; expected a .vst3"
#if WJN_ENABLE_VST2
                   << L" or .dll"
#endif
                   << L".\n";
        return kPluginLoadError;
    }

    JackRuntime jack;
    VirtualRuntime virtualRuntime;
    if (!options.virtualMapping.empty())
    {
        if (!virtualRuntime.start(options.virtualMapping, *plugin, error))
        {
            std::cerr << "Unable to start VIRT processing: " << error << '\n';
            return kPluginLoadError;
        }
    }
    else if (!jack.open(options.clientName, options.portDisplayMode, *plugin, error))
    {
        std::cerr << "Unable to start JACK client: " << error << '\n';
        return kJackError;
    }

    const auto exitCode = PipeServer(options.pipeName, jack, *plugin,
                                     options.virtualMapping.empty() ? nullptr : &virtualRuntime)
                              .run();
    virtualRuntime.stop();
    jack.deactivate();
    plugin->closeEditor();
    jack.close();
    plugin.reset();
    return exitCode;
}
