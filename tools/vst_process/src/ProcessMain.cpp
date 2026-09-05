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
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"

#if WJN_ENABLE_VST2
#include "pluginterfaces/vst2.x/aeffect.h"
#include "pluginterfaces/vst2.x/aeffectx.h"
#endif

#include <jack/jack.h>

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr int kArgumentError = 1;
constexpr int kPluginLoadError = 2;
constexpr int kJackError = 3;
constexpr std::string_view kVst3AudioModuleClass = "Audio Module Class";

#if WJN_ENABLE_VST2
VstIntPtr currentVst2UniqueId = 0;
double currentVst2SampleRate = 48000.0;
VstIntPtr currentVst2BlockSize = 512;

VstIntPtr VSTCALLBACK vst2HostCallback(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    if (opcode == audioMasterVersion)
        return 2400;
    if (opcode == audioMasterCurrentId)
        return currentVst2UniqueId;
    if (opcode == audioMasterGetSampleRate)
        return static_cast<VstIntPtr>(currentVst2SampleRate);
    if (opcode == audioMasterGetBlockSize)
        return currentVst2BlockSize;
    return 0;
}

using Vst2EntryProc = AEffect* (VSTCALLBACK*) (audioMasterCallback);
#endif

struct Options final {
    std::wstring path;
    std::wstring clientName { L"WinJACKNexus-VST" };
    std::wstring pipeName;
    std::wstring internalPlugin;
    bool help {};
};

void printUsage()
{
    std::wcout << L"Usage: VSTProcess --path <plugin.vst3"
#if WJN_ENABLE_VST2
               << L"|plugin.dll"
#endif
               << L"> --pipe <name> [--client <name>]"
               << L" [--internal_plugin <name>]\n";
}

bool readOptionValue(int& index, int argc, wchar_t* argv[], std::wstring& value)
{
    if (++index >= argc)
        return false;
    value = argv[index];
    return !value.empty();
}

bool parseOptions(int argc, wchar_t* argv[], Options& options)
{
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view argument = argv[index];
        if (argument == L"--help" || argument == L"-h") {
            options.help = true;
            return true;
        }
        if (argument == L"--path") {
            if (!readOptionValue(index, argc, argv, options.path))
                return false;
            continue;
        }
        if (argument == L"--client") {
            if (!readOptionValue(index, argc, argv, options.clientName))
                return false;
            continue;
        }
        if (argument == L"--pipe" || argument == L"--port") {
            if (!readOptionValue(index, argc, argv, options.pipeName))
                return false;
            continue;
        }
        if (argument == L"--internal_plugin") {
            if (!readOptionValue(index, argc, argv, options.internalPlugin))
                return false;
            continue;
        }
        return false;
    }
    return !options.path.empty();
}

std::string utf8FromWide(const std::wstring& value)
{
    if (value.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring normalizedPipeName(std::wstring value)
{
    constexpr std::wstring_view prefix = L"\\\\.\\pipe\\";
    if (value.rfind(prefix, 0) == 0)
        return value;
    return std::wstring(prefix) + value;
}

class AudioPlugin {
public:
    virtual ~AudioPlugin() = default;
    virtual bool prepare(double, VstInt32, std::string&) { return true; }
    virtual void process(float**, float**, VstInt32) noexcept = 0;
    virtual VstInt32 inputCount() const noexcept = 0;
    virtual VstInt32 outputCount() const noexcept = 0;
};

class Vst3Plugin final : public AudioPlugin {
public:
    bool load(const std::wstring& path, std::string& error)
    {
        module_ = VST3::Hosting::Module::create(utf8FromWide(path), error);
        if (!module_)
            return false;

        host_ = Steinberg::owned(new Steinberg::Vst::HostApplication);
        module_->getFactory().setHostContext(host_);
        for (const auto& classInfo : module_->getFactory().classInfos()) {
            if (classInfo.category() != kVst3AudioModuleClass)
                continue;
            component_ = module_->getFactory().createInstance<Steinberg::Vst::IComponent>(classInfo.ID());
            if (!component_)
                continue;
            if (component_->initialize(host_) == Steinberg::kResultOk)
                return true;
            component_ = nullptr;
        }

        error = "VST3 module does not expose an initializable audio component";
        unload();
        return false;
    }

    void unload()
    {
        if (component_)
            component_->terminate();
        component_ = nullptr;
        host_ = nullptr;
        module_.reset();
    }

    ~Vst3Plugin()
    {
        unload();
    }

    Steinberg::Vst::IComponent* component() const noexcept
    {
        return component_.get();
    }

    void process(float**, float**, VstInt32) noexcept override {}

    VstInt32 inputCount() const noexcept override
    {
        return channelCount(Steinberg::Vst::kInput);
    }

    VstInt32 outputCount() const noexcept override
    {
        return channelCount(Steinberg::Vst::kOutput);
    }

private:
    VstInt32 channelCount(Steinberg::Vst::BusDirection direction) const noexcept
    {
        if (!component_)
            return 0;
        VstInt32 channels = 0;
        const auto busCount = component_->getBusCount(Steinberg::Vst::kAudio, direction);
        for (Steinberg::int32 busIndex = 0; busIndex < busCount; ++busIndex) {
            Steinberg::Vst::BusInfo bus {};
            if (component_->getBusInfo(Steinberg::Vst::kAudio, direction, busIndex, bus) == Steinberg::kResultOk)
                channels += std::max<Steinberg::int32>(0, bus.channelCount);
        }
        return channels;
    }

    VST3::Hosting::Module::Ptr module_;
    Steinberg::IPtr<Steinberg::Vst::HostApplication> host_;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component_;
};

#if WJN_ENABLE_VST2
class Vst2Plugin final : public AudioPlugin {
public:
    bool load(const std::wstring& path, const std::wstring& internalPlugin, std::string& error)
    {
        module_ = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (module_ == nullptr) {
            error = "Unable to load VST2 module";
            return false;
        }
        auto entry = reinterpret_cast<Vst2EntryProc>(GetProcAddress(module_, "VSTPluginMain"));
        if (entry == nullptr)
            entry = reinterpret_cast<Vst2EntryProc>(GetProcAddress(module_, "main"));
        if (entry == nullptr) {
            error = "VST2 module has no VSTPluginMain/main entry point";
            unload();
            return false;
        }

        if (!internalPlugin.empty()) {
            try {
                currentVst2UniqueId = static_cast<VstIntPtr>(std::stoll(internalPlugin));
            } catch (const std::exception&) {
                auto* shell = entry(vst2HostCallback);
                if (shell == nullptr || shell->magic != kEffectMagic || shell->dispatcher == nullptr) {
                    error = "VST2 shell returned an invalid AEffect";
                    unload();
                    return false;
                }
                shell->dispatcher(shell, effOpen, 0, 0, nullptr, 0.0F);
                char shellName[kVstMaxProductStrLen + 1] {};
                const auto requestedName = utf8FromWide(internalPlugin);
                while (true) {
                    std::fill(std::begin(shellName), std::end(shellName), '\0');
                    const auto uniqueId = shell->dispatcher(shell, effShellGetNextPlugin, 0, 0, shellName, 0.0F);
                    if (uniqueId == 0)
                        break;
                    if (requestedName == shellName) {
                        currentVst2UniqueId = uniqueId;
                        break;
                    }
                }
                shell->dispatcher(shell, effClose, 0, 0, nullptr, 0.0F);
                if (currentVst2UniqueId == 0) {
                    error = "VST2 shell does not expose the requested internal_plugin";
                    unload();
                    return false;
                }
            }
        }
        effect_ = entry(vst2HostCallback);
        currentVst2UniqueId = 0;
        if (effect_ == nullptr || effect_->magic != kEffectMagic || effect_->dispatcher == nullptr) {
            error = "VST2 module returned an invalid AEffect";
            unload();
            return false;
        }
        effect_->dispatcher(effect_, effOpen, 0, 0, nullptr, 0.0F);
        opened_ = true;
        return true;
    }

    bool prepare(double sampleRate, VstInt32 blockSize, std::string& error) override
    {
        if (effect_ == nullptr)
            return false;
        currentVst2SampleRate = sampleRate;
        currentVst2BlockSize = blockSize;
        effect_->dispatcher(effect_, effSetSampleRate, 0, 0, nullptr, static_cast<float>(sampleRate));
        effect_->dispatcher(effect_, effSetBlockSize, 0, blockSize, nullptr, 0.0F);
        effect_->dispatcher(effect_, effMainsChanged, 0, 1, nullptr, 0.0F);
        processing_ = true;
        if (effect_->processReplacing == nullptr) {
            error = "VST2 plugin does not expose processReplacing";
            return false;
        }
        return true;
    }

    void process(float** inputs, float** outputs, VstInt32 frames) noexcept override
    {
        if (effect_ == nullptr)
            return;
        if (effect_->processReplacing != nullptr)
            effect_->processReplacing(effect_, inputs, outputs, frames);
    }

    VstInt32 inputCount() const noexcept override { return effect_ == nullptr ? 0 : std::max<VstInt32>(0, effect_->numInputs); }
    VstInt32 outputCount() const noexcept override { return effect_ == nullptr ? 0 : std::max<VstInt32>(0, effect_->numOutputs); }
    VstInt32 parameterCount() const noexcept { return effect_ == nullptr ? 0 : std::max<VstInt32>(0, effect_->numParams); }

    bool setParameter(VstInt32 index, float value) noexcept
    {
        if (effect_ == nullptr || effect_->setParameter == nullptr || index < 0 || index >= parameterCount())
            return false;
        effect_->setParameter(effect_, index, std::clamp(value, 0.0F, 1.0F));
        return true;
    }

    bool getParameter(VstInt32 index, float& value) const noexcept
    {
        if (effect_ == nullptr || effect_->getParameter == nullptr || index < 0 || index >= parameterCount())
            return false;
        value = effect_->getParameter(effect_, index);
        return true;
    }

    std::vector<std::byte> state() const
    {
        void* data = nullptr;
        const auto size = effect_ == nullptr ? 0 : effect_->dispatcher(effect_, effGetChunk, 0, 0, &data, 0.0F);
        if (size <= 0 || data == nullptr)
            return {};
        return { static_cast<const std::byte*>(data), static_cast<const std::byte*>(data) + size };
    }

    bool setState(const void* data, std::size_t size) noexcept
    {
        return effect_ != nullptr && effect_->dispatcher(effect_, effSetChunk, 0, static_cast<VstIntPtr>(size), const_cast<void*>(data), 0.0F) != 0;
    }

    void unload() noexcept
    {
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

    ~Vst2Plugin() { unload(); }

private:
    HMODULE module_ {};
    AEffect* effect_ {};
    bool opened_ {};
    bool processing_ {};
};
#endif

class JackRuntime final {
public:
    ~JackRuntime()
    {
        close();
    }

    bool open(const std::wstring& requestedClientName, AudioPlugin& plugin, std::string& error)
    {
        if (!loadApi(error))
            return false;

        const auto clientName = utf8FromWide(requestedClientName);
        jack_status_t status = JackFailure;
        client_ = clientOpen_(clientName.empty() ? "WinJACKNexus-VST" : clientName.c_str(),
                              JackNoStartServer,
                              &status);
        if (client_ == nullptr) {
            error = "Unable to open JACK client, status=" + std::to_string(status);
            return false;
        }
        if (setProcessCallback_(client_, &JackRuntime::process, this) != 0) {
            error = "Unable to set JACK process callback";
            return false;
        }
        if (!registerAudioPorts(plugin, error))
            return false;
        if (!plugin.prepare(sampleRate_(client_), static_cast<VstInt32>(bufferSize_(client_)), error))
            return false;
        plugin_ = &plugin;
        if (activate_(client_) != 0) {
            error = "Unable to activate JACK client";
            return false;
        }
        active_ = true;
        return true;
    }

    void close() noexcept
    {
        if (client_ != nullptr && active_)
            deactivate_(client_);
        active_ = false;
        if (client_ != nullptr)
            clientClose_(client_);
        client_ = nullptr;
        plugin_ = nullptr;
        audioInputs_.clear();
        audioOutputs_.clear();
        inputBuffers_.clear();
        outputBuffers_.clear();
        if (library_ != nullptr)
            FreeLibrary(library_);
        library_ = nullptr;
    }

    bool isActive() const noexcept
    {
        return active_;
    }

private:
    using ClientOpen = jack_client_t* (*) (const char*, jack_options_t, jack_status_t*, ...);
    using ClientClose = int (*) (jack_client_t*);
    using Activate = int (*) (jack_client_t*);
    using Deactivate = int (*) (jack_client_t*);
    using SetProcessCallback = int (*) (jack_client_t*, JackProcessCallback, void*);
    using PortRegister = jack_port_t* (*) (jack_client_t*, const char*, const char*, unsigned long, unsigned long);
    using PortGetBuffer = void* (*) (jack_port_t*, jack_nframes_t);
    using SampleRate = jack_nframes_t (*) (jack_client_t*);
    using BufferSize = jack_nframes_t (*) (jack_client_t*);

    bool loadApi(std::string& error)
    {
        if (library_ != nullptr)
            return true;

        constexpr const wchar_t* candidates64[] { L"libjack64.dll", L"libjack.dll" };
        constexpr const wchar_t* candidates32[] { L"libjack.dll" };
        const auto& candidates = sizeof(void*) == 8 ? candidates64 : candidates32;
        const auto candidateCount = sizeof(void*) == 8 ? std::size(candidates64) : std::size(candidates32);
        for (std::size_t index = 0; index < candidateCount && library_ == nullptr; ++index)
            library_ = LoadLibraryW(candidates[index]);
        if (library_ == nullptr) {
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
        sampleRate_ = loadFunction<SampleRate>("jack_get_sample_rate");
        bufferSize_ = loadFunction<BufferSize>("jack_get_buffer_size");
        if (clientOpen_ != nullptr && clientClose_ != nullptr && activate_ != nullptr && deactivate_ != nullptr
            && setProcessCallback_ != nullptr && portRegister_ != nullptr && portGetBuffer_ != nullptr
            && sampleRate_ != nullptr && bufferSize_ != nullptr)
            return true;

        error = "JACK runtime library is missing a required client API";
        close();
        return false;
    }

    template <typename Function>
    Function loadFunction(const char* name) const noexcept
    {
        return reinterpret_cast<Function>(GetProcAddress(library_, name));
    }

    bool registerAudioPorts(const AudioPlugin& plugin, std::string& error)
    {
        const auto registerDirection = [&] (VstInt32 count, unsigned long jackFlags, const char* prefix, std::vector<jack_port_t*>& ports) {
            for (VstInt32 channel = 0; channel < count; ++channel) {
                const auto name = std::string(prefix) + '_' + std::to_string(channel + 1);
                auto* port = portRegister_(client_, name.c_str(), JACK_DEFAULT_AUDIO_TYPE, jackFlags, 0);
                if (port == nullptr)
                    return false;
                ports.push_back(port);
            }
            return true;
        };

        if (!registerDirection(plugin.inputCount(), JackPortIsInput, "in", audioInputs_)
            || !registerDirection(plugin.outputCount(), JackPortIsOutput, "out", audioOutputs_)) {
            error = "Unable to register JACK audio ports";
            return false;
        }
        inputBuffers_.resize(audioInputs_.size());
        outputBuffers_.resize(audioOutputs_.size());
        return true;
    }

    static int process(jack_nframes_t frames, void* context) noexcept
    {
        auto* runtime = static_cast<JackRuntime*>(context);
        for (std::size_t index = 0; index < runtime->audioInputs_.size(); ++index)
            runtime->inputBuffers_[index] = static_cast<float*>(runtime->portGetBuffer_(runtime->audioInputs_[index], frames));
        for (std::size_t index = 0; index < runtime->audioOutputs_.size(); ++index) {
            runtime->outputBuffers_[index] = static_cast<float*>(runtime->portGetBuffer_(runtime->audioOutputs_[index], frames));
            if (runtime->outputBuffers_[index] != nullptr)
                std::fill(runtime->outputBuffers_[index], runtime->outputBuffers_[index] + frames, 0.0F);
        }
        if (runtime->plugin_ != nullptr)
            runtime->plugin_->process(runtime->inputBuffers_.data(), runtime->outputBuffers_.data(), static_cast<VstInt32>(frames));
        return 0;
    }

    HMODULE library_ {};
    jack_client_t* client_ {};
    bool active_ {};
    ClientOpen clientOpen_ {};
    ClientClose clientClose_ {};
    Activate activate_ {};
    Deactivate deactivate_ {};
    SetProcessCallback setProcessCallback_ {};
    PortRegister portRegister_ {};
    PortGetBuffer portGetBuffer_ {};
    SampleRate sampleRate_ {};
    BufferSize bufferSize_ {};
    AudioPlugin* plugin_ {};
    std::vector<jack_port_t*> audioInputs_;
    std::vector<jack_port_t*> audioOutputs_;
    std::vector<float*> inputBuffers_;
    std::vector<float*> outputBuffers_;
};

class PipeServer final {
public:
    PipeServer(const std::wstring& name, JackRuntime& jack)
        : name_(normalizedPipeName(name))
        , jack_(jack)
    {
    }

    int run()
    {
        while (!stop_) {
            const HANDLE pipe = CreateNamedPipeW(name_.c_str(),
                                                 PIPE_ACCESS_DUPLEX,
                                                 PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                                 1,
                                                 4096,
                                                 4096,
                                                 0,
                                                 nullptr);
            if (pipe == INVALID_HANDLE_VALUE)
                return kJackError;
            const BOOL connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
            if (connected)
                handleClient(pipe);
            CloseHandle(pipe);
        }
        return 0;
    }

private:
    void handleClient(HANDLE pipe)
    {
        char request[4096] {};
        DWORD bytesRead {};
        if (!ReadFile(pipe, request, sizeof(request) - 1, &bytesRead, nullptr))
            return;
        const std::string_view command(request, bytesRead);
        const char* response = "{\"ok\":false,\"error\":\"unknown command\"}\n";
        if (command.find("ping") != std::string_view::npos)
            response = "{\"ok\":true,\"reply\":\"pong\"}\n";
        else if (command.find("status") != std::string_view::npos)
            response = jack_.isActive() ? "{\"ok\":true,\"active\":true}\n"
                                    : "{\"ok\":true,\"active\":false}\n";
        else if (command.find("quit") != std::string_view::npos) {
            stop_ = true;
            response = "{\"ok\":true,\"stopping\":true}\n";
        }
        DWORD bytesWritten {};
        WriteFile(pipe, response, static_cast<DWORD>(std::char_traits<char>::length(response)), &bytesWritten, nullptr);
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
    }

    std::wstring name_;
    JackRuntime& jack_;
    std::atomic<bool> stop_ {};
};

}

int wmain(int argc, wchar_t* argv[])
{
    Options options;
    if (!parseOptions(argc, argv, options)) {
        printUsage();
        return kArgumentError;
    }
    if (options.help) {
        printUsage();
        return 0;
    }
    if (options.pipeName.empty()) {
        std::wcerr << L"VSTProcess requires --pipe so its JACK client has a controlled lifetime.\n";
        printUsage();
        return kArgumentError;
    }
    const auto extension = std::filesystem::path(options.path).extension().wstring();
    std::unique_ptr<AudioPlugin> plugin;
    std::string error;
    if (extension == L".vst3") {
        auto vst3 = std::make_unique<Vst3Plugin>();
        if (!vst3->load(options.path, error)) {
            std::cerr << "Unable to load VST3: " << error << '\n';
            return kPluginLoadError;
        }
        plugin = std::move(vst3);
#if WJN_ENABLE_VST2
    } else if (extension == L".dll") {
        auto vst2 = std::make_unique<Vst2Plugin>();
        if (!vst2->load(options.path, options.internalPlugin, error)) {
            std::cerr << "Unable to load VST2: " << error << '\n';
            return kPluginLoadError;
        }
        plugin = std::move(vst2);
#endif
    } else {
        std::wcerr << L"Unsupported plugin path; expected a .vst3"
#if WJN_ENABLE_VST2
                   << L" or .dll"
#endif
                   << L".\n";
        return kPluginLoadError;
    }

    JackRuntime jack;
    if (!jack.open(options.clientName, *plugin, error)) {
        std::cerr << "Unable to start JACK client: " << error << '\n';
        return kJackError;
    }

    return PipeServer(options.pipeName, jack).run();
}