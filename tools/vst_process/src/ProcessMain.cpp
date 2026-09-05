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

struct Options final {
    std::wstring path;
    std::wstring clientName { L"WinJACKNexus-VST" };
    std::wstring pipeName;
    std::wstring internalPlugin;
    bool help {};
};

void printUsage()
{
    std::wcout << L"Usage: VSTProcess --path <plugin.vst3> [--client <name>] [--pipe <name>]"
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

class Vst3Plugin final {
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

private:
    VST3::Hosting::Module::Ptr module_;
    Steinberg::IPtr<Steinberg::Vst::HostApplication> host_;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component_;
};

class JackRuntime final {
public:
    ~JackRuntime()
    {
        close();
    }

    bool open(const std::wstring& requestedClientName, Steinberg::Vst::IComponent& component, std::string& error)
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
        if (!registerAudioPorts(component, error))
            return false;
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
        audioInputs_.clear();
        audioOutputs_.clear();
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
        if (clientOpen_ != nullptr && clientClose_ != nullptr && activate_ != nullptr && deactivate_ != nullptr
            && setProcessCallback_ != nullptr && portRegister_ != nullptr && portGetBuffer_ != nullptr)
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

    bool registerAudioPorts(Steinberg::Vst::IComponent& component, std::string& error)
    {
        const auto registerDirection = [&] (Steinberg::Vst::BusDirection direction,
                                             unsigned long jackFlags,
                                             const char* prefix,
                                             std::vector<jack_port_t*>& ports) {
            const auto busCount = component.getBusCount(Steinberg::Vst::kAudio, direction);
            int channel = 1;
            for (Steinberg::int32 busIndex = 0; busIndex < busCount; ++busIndex) {
                Steinberg::Vst::BusInfo bus {};
                if (component.getBusInfo(Steinberg::Vst::kAudio, direction, busIndex, bus) != Steinberg::kResultOk)
                    continue;
                for (Steinberg::int32 busChannel = 0; busChannel < bus.channelCount; ++busChannel) {
                    const auto name = std::string(prefix) + '_' + std::to_string(channel++);
                    auto* port = portRegister_(client_, name.c_str(), JACK_DEFAULT_AUDIO_TYPE, jackFlags, 0);
                    if (port == nullptr)
                        return false;
                    ports.push_back(port);
                }
            }
            return true;
        };

        if (!registerDirection(Steinberg::Vst::kInput, JackPortIsInput, "in", audioInputs_)
            || !registerDirection(Steinberg::Vst::kOutput, JackPortIsOutput, "out", audioOutputs_)) {
            error = "Unable to register JACK audio ports";
            return false;
        }
        return true;
    }

    static int process(jack_nframes_t frames, void* context) noexcept
    {
        auto* runtime = static_cast<JackRuntime*>(context);
        for (auto* output : runtime->audioOutputs_) {
            auto* buffer = static_cast<jack_default_audio_sample_t*>(runtime->portGetBuffer_(output, frames));
            if (buffer != nullptr)
                std::fill(buffer, buffer + frames, 0.0F);
        }
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
    std::vector<jack_port_t*> audioInputs_;
    std::vector<jack_port_t*> audioOutputs_;
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

BOOL WINAPI handleConsoleControl(DWORD controlType)
{
    return controlType == CTRL_C_EVENT || controlType == CTRL_BREAK_EVENT || controlType == CTRL_CLOSE_EVENT;
}

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
    if (std::filesystem::path(options.path).extension() != L".vst3") {
        std::wcerr << L"V2 only supports VST3 loading; VST2 hosting is implemented in V3.\n";
        return kPluginLoadError;
    }

    Vst3Plugin plugin;
    std::string error;
    if (!plugin.load(options.path, error)) {
        std::cerr << "Unable to load VST3: " << error << '\n';
        return kPluginLoadError;
    }

    JackRuntime jack;
    if (!jack.open(options.clientName, *plugin.component(), error)) {
        std::cerr << "Unable to start JACK client: " << error << '\n';
        return kJackError;
    }

    if (options.pipeName.empty()) {
        SetConsoleCtrlHandler(handleConsoleControl, TRUE);
        std::cout << "VSTProcess active; use --pipe for IPC control.\n";
        return 0;
    }
    return PipeServer(options.pipeName, jack).run();
}