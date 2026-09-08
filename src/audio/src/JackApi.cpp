/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <WinJACKNexus/Audio/JackApi.hpp>

#include <WinJACKNexus/Common/Logger.hpp>
#include <WinJACKNexus/Common/PathUtils.hpp>

#include <Windows.h>

#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <new>
#include <string>
#include <vector>

namespace WinJACKNexus::Audio {

namespace {

template <typename Function>
bool loadFunction(HMODULE module, const char* name, Function& function) noexcept
{
    function = reinterpret_cast<Function>(GetProcAddress(module, name));
    return function != nullptr;
}

std::vector<std::wstring> candidatePaths(const wchar_t* explicitPath)
{
    std::vector<std::wstring> candidates;
    if (explicitPath != nullptr && *explicitPath != L'\0')
        candidates.emplace_back(explicitPath);

    if (const auto* environmentPath = _wgetenv(L"WJN_JACK_DLL"); environmentPath != nullptr && *environmentPath != L'\0')
        candidates.emplace_back(environmentPath);

    candidates.emplace_back(L"libjack64.dll");

    const auto moduleDirectory = Common::PathUtils::moduleDirectory();
    candidates.emplace_back((moduleDirectory / L"libjack64.dll").wstring());
    candidates.emplace_back((moduleDirectory / L"lib" / L"libjack64.dll").wstring());
    candidates.emplace_back((moduleDirectory / L".." / L"libjack64.dll").lexically_normal().wstring());

    if (const auto* programFiles = _wgetenv(L"ProgramFiles"); programFiles != nullptr && *programFiles != L'\0') {
        const auto jackDirectory = std::filesystem::path(programFiles) / L"JACK2";
        candidates.emplace_back((jackDirectory / L"lib" / L"libjack64.dll").wstring());
        candidates.emplace_back((jackDirectory / L"libjack64.dll").wstring());
    }
    if (const auto* systemRoot = _wgetenv(L"SystemRoot"); systemRoot != nullptr && *systemRoot != L'\0')
        candidates.emplace_back(std::filesystem::path(systemRoot) / L"libjack64.dll");

    return candidates;
}

}

struct JackApi::Impl final {
    using ClientOpen = decltype(&jack_client_open);
    using ClientClose = decltype(&jack_client_close);
    using ClientNameSize = decltype(&jack_client_name_size);
    using GetClientName = decltype(&jack_get_client_name);
    using Activate = decltype(&jack_activate);
    using Deactivate = decltype(&jack_deactivate);
    using SetShutdownCallback = decltype(&jack_on_shutdown);
    using SetProcessCallback = decltype(&jack_set_process_callback);
    using SetBufferSizeCallback = decltype(&jack_set_buffer_size_callback);
    using SetSampleRateCallback = decltype(&jack_set_sample_rate_callback);
    using SetGraphOrderCallback = decltype(&jack_set_graph_order_callback);
    using SetXRunCallback = decltype(&jack_set_xrun_callback);
    using GetSampleRate = decltype(&jack_get_sample_rate);
    using GetBufferSize = decltype(&jack_get_buffer_size);
    using CpuLoad = decltype(&jack_cpu_load);
    using PortRegister = decltype(&jack_port_register);
    using PortRename = decltype(&jack_port_rename);
    using PortUnregister = decltype(&jack_port_unregister);
    using PortGetBuffer = decltype(&jack_port_get_buffer);
    using PortName = decltype(&jack_port_name);
    using PortShortName = decltype(&jack_port_short_name);
    using PortFlags = decltype(&jack_port_flags);
    using PortType = decltype(&jack_port_type);
    using PortGetConnections = decltype(&jack_port_get_connections);
    using PortByName = decltype(&jack_port_by_name);
    using GetPorts = decltype(&jack_get_ports);
    using Connect = decltype(&jack_connect);
    using Disconnect = decltype(&jack_disconnect);
    using Free = decltype(&jack_free);
    using MidiEventCount = decltype(&jack_midi_get_event_count);
    using MidiEventGet = decltype(&jack_midi_event_get);
    using MidiClearBuffer = decltype(&jack_midi_clear_buffer);
    using MidiMaxEventSize = decltype(&jack_midi_max_event_size);
    using MidiEventReserve = decltype(&jack_midi_event_reserve);
    using MidiEventWrite = decltype(&jack_midi_event_write);
    using MidiLostEventCount = decltype(&jack_midi_get_lost_event_count);
    using RingbufferCreate = decltype(&jack_ringbuffer_create);
    using RingbufferFree = decltype(&jack_ringbuffer_free);
    using RingbufferGetReadVector = decltype(&jack_ringbuffer_get_read_vector);
    using RingbufferGetWriteVector = decltype(&jack_ringbuffer_get_write_vector);
    using RingbufferRead = decltype(&jack_ringbuffer_read);
    using RingbufferPeek = decltype(&jack_ringbuffer_peek);
    using RingbufferReadAdvance = decltype(&jack_ringbuffer_read_advance);
    using RingbufferReadSpace = decltype(&jack_ringbuffer_read_space);
    using RingbufferMlock = decltype(&jack_ringbuffer_mlock);
    using RingbufferReset = decltype(&jack_ringbuffer_reset);
    using RingbufferWrite = decltype(&jack_ringbuffer_write);
    using RingbufferWriteAdvance = decltype(&jack_ringbuffer_write_advance);
    using RingbufferWriteSpace = decltype(&jack_ringbuffer_write_space);

    struct Functions final {
        decltype(&jack_get_version_string) getVersionString = nullptr;
        ClientOpen clientOpen = nullptr;
        ClientClose clientClose = nullptr;
        ClientNameSize clientNameSize = nullptr;
        GetClientName getClientName = nullptr;
        Activate activate = nullptr;
        Deactivate deactivate = nullptr;
        SetShutdownCallback setShutdownCallback = nullptr;
        SetProcessCallback setProcessCallback = nullptr;
        SetBufferSizeCallback setBufferSizeCallback = nullptr;
        SetSampleRateCallback setSampleRateCallback = nullptr;
        SetGraphOrderCallback setGraphOrderCallback = nullptr;
        SetXRunCallback setXRunCallback = nullptr;
        GetSampleRate getSampleRate = nullptr;
        GetBufferSize getBufferSize = nullptr;
        CpuLoad cpuLoad = nullptr;
        PortRegister portRegister = nullptr;
        PortRename portRename = nullptr;
        PortUnregister portUnregister = nullptr;
        PortGetBuffer portGetBuffer = nullptr;
        PortName portName = nullptr;
        PortShortName portShortName = nullptr;
        PortFlags portFlags = nullptr;
        PortType portType = nullptr;
        PortGetConnections portGetConnections = nullptr;
        PortByName portByName = nullptr;
        GetPorts getPorts = nullptr;
        Connect connect = nullptr;
        Disconnect disconnect = nullptr;
        Free free = nullptr;
        MidiEventCount midiEventCount = nullptr;
        MidiEventGet midiEventGet = nullptr;
        MidiClearBuffer midiClearBuffer = nullptr;
        MidiMaxEventSize midiMaxEventSize = nullptr;
        MidiEventReserve midiEventReserve = nullptr;
        MidiEventWrite midiEventWrite = nullptr;
        MidiLostEventCount midiLostEventCount = nullptr;
        RingbufferCreate ringbufferCreate = nullptr;
        RingbufferFree ringbufferFree = nullptr;
        RingbufferGetReadVector ringbufferGetReadVector = nullptr;
        RingbufferGetWriteVector ringbufferGetWriteVector = nullptr;
        RingbufferRead ringbufferRead = nullptr;
        RingbufferPeek ringbufferPeek = nullptr;
        RingbufferReadAdvance ringbufferReadAdvance = nullptr;
        RingbufferReadSpace ringbufferReadSpace = nullptr;
        RingbufferMlock ringbufferMlock = nullptr;
        RingbufferReset ringbufferReset = nullptr;
        RingbufferWrite ringbufferWrite = nullptr;
        RingbufferWriteAdvance ringbufferWriteAdvance = nullptr;
        RingbufferWriteSpace ringbufferWriteSpace = nullptr;
    } functions;

    HMODULE module = nullptr;
    std::wstring path;
    std::mutex mutex;
};

JackApi& JackApi::instance() noexcept
{
    static JackApi api;
    return api;
}

JackApi::JackApi()
    : impl_(new Impl {})
{
}

JackApi::~JackApi() noexcept
{
    unload();
    delete impl_;
}

bool JackApi::load(const wchar_t* explicitPath) noexcept
{
    std::lock_guard lock(impl_->mutex);
    if (impl_->module != nullptr)
        return true;

    try {
        for (const auto& candidate : candidatePaths(explicitPath)) {
            const auto module = LoadLibraryW(candidate.c_str());
            if (module == nullptr)
                continue;

            Impl::Functions functions;
#define WJN_LOAD_JACK_FUNCTION(field, symbol) \
    if (!loadFunction(module, symbol, functions.field)) { \
        FreeLibrary(module); \
        WJN_LOG_ERROR("JACK runtime is missing symbol %s", symbol); \
        continue; \
    }
            WJN_LOAD_JACK_FUNCTION(getVersionString, "jack_get_version_string");
            WJN_LOAD_JACK_FUNCTION(clientOpen, "jack_client_open");
            WJN_LOAD_JACK_FUNCTION(clientClose, "jack_client_close");
            WJN_LOAD_JACK_FUNCTION(clientNameSize, "jack_client_name_size");
            WJN_LOAD_JACK_FUNCTION(getClientName, "jack_get_client_name");
            WJN_LOAD_JACK_FUNCTION(activate, "jack_activate");
            WJN_LOAD_JACK_FUNCTION(deactivate, "jack_deactivate");
            WJN_LOAD_JACK_FUNCTION(setShutdownCallback, "jack_on_shutdown");
            WJN_LOAD_JACK_FUNCTION(setProcessCallback, "jack_set_process_callback");
            WJN_LOAD_JACK_FUNCTION(setBufferSizeCallback, "jack_set_buffer_size_callback");
            WJN_LOAD_JACK_FUNCTION(setSampleRateCallback, "jack_set_sample_rate_callback");
            WJN_LOAD_JACK_FUNCTION(setGraphOrderCallback, "jack_set_graph_order_callback");
            WJN_LOAD_JACK_FUNCTION(setXRunCallback, "jack_set_xrun_callback");
            WJN_LOAD_JACK_FUNCTION(getSampleRate, "jack_get_sample_rate");
            WJN_LOAD_JACK_FUNCTION(getBufferSize, "jack_get_buffer_size");
            WJN_LOAD_JACK_FUNCTION(cpuLoad, "jack_cpu_load");
            WJN_LOAD_JACK_FUNCTION(portRegister, "jack_port_register");
            loadFunction(module, "jack_port_rename", functions.portRename);
            WJN_LOAD_JACK_FUNCTION(portUnregister, "jack_port_unregister");
            WJN_LOAD_JACK_FUNCTION(portGetBuffer, "jack_port_get_buffer");
            WJN_LOAD_JACK_FUNCTION(portName, "jack_port_name");
            WJN_LOAD_JACK_FUNCTION(portShortName, "jack_port_short_name");
            WJN_LOAD_JACK_FUNCTION(portFlags, "jack_port_flags");
            WJN_LOAD_JACK_FUNCTION(portType, "jack_port_type");
            WJN_LOAD_JACK_FUNCTION(portGetConnections, "jack_port_get_connections");
            WJN_LOAD_JACK_FUNCTION(portByName, "jack_port_by_name");
            WJN_LOAD_JACK_FUNCTION(getPorts, "jack_get_ports");
            WJN_LOAD_JACK_FUNCTION(connect, "jack_connect");
            WJN_LOAD_JACK_FUNCTION(disconnect, "jack_disconnect");
            WJN_LOAD_JACK_FUNCTION(free, "jack_free");
            WJN_LOAD_JACK_FUNCTION(midiEventCount, "jack_midi_get_event_count");
            WJN_LOAD_JACK_FUNCTION(midiEventGet, "jack_midi_event_get");
            WJN_LOAD_JACK_FUNCTION(midiClearBuffer, "jack_midi_clear_buffer");
            WJN_LOAD_JACK_FUNCTION(midiMaxEventSize, "jack_midi_max_event_size");
            WJN_LOAD_JACK_FUNCTION(midiEventReserve, "jack_midi_event_reserve");
            WJN_LOAD_JACK_FUNCTION(midiEventWrite, "jack_midi_event_write");
            WJN_LOAD_JACK_FUNCTION(midiLostEventCount, "jack_midi_get_lost_event_count");
            WJN_LOAD_JACK_FUNCTION(ringbufferCreate, "jack_ringbuffer_create");
            WJN_LOAD_JACK_FUNCTION(ringbufferFree, "jack_ringbuffer_free");
            WJN_LOAD_JACK_FUNCTION(ringbufferGetReadVector, "jack_ringbuffer_get_read_vector");
            WJN_LOAD_JACK_FUNCTION(ringbufferGetWriteVector, "jack_ringbuffer_get_write_vector");
            WJN_LOAD_JACK_FUNCTION(ringbufferRead, "jack_ringbuffer_read");
            WJN_LOAD_JACK_FUNCTION(ringbufferPeek, "jack_ringbuffer_peek");
            WJN_LOAD_JACK_FUNCTION(ringbufferReadAdvance, "jack_ringbuffer_read_advance");
            WJN_LOAD_JACK_FUNCTION(ringbufferReadSpace, "jack_ringbuffer_read_space");
            WJN_LOAD_JACK_FUNCTION(ringbufferMlock, "jack_ringbuffer_mlock");
            WJN_LOAD_JACK_FUNCTION(ringbufferReset, "jack_ringbuffer_reset");
            WJN_LOAD_JACK_FUNCTION(ringbufferWrite, "jack_ringbuffer_write");
            WJN_LOAD_JACK_FUNCTION(ringbufferWriteAdvance, "jack_ringbuffer_write_advance");
            WJN_LOAD_JACK_FUNCTION(ringbufferWriteSpace, "jack_ringbuffer_write_space");
#undef WJN_LOAD_JACK_FUNCTION

            impl_->functions = functions;
            impl_->module = module;
            wchar_t loadedPath[32768] {};
            const auto pathLength = GetModuleFileNameW(module, loadedPath, static_cast<DWORD>(std::size(loadedPath)));
            impl_->path.assign(loadedPath, pathLength);
            WJN_LOG_INFO("Loaded JACK runtime");
            return true;
        }
    } catch (...) {
    }

    WJN_LOG_WARN("JACK runtime could not be loaded");
    return false;
}

void JackApi::unload() noexcept
{
    std::lock_guard lock(impl_->mutex);
    if (impl_->module == nullptr)
        return;
    FreeLibrary(impl_->module);
    impl_->module = nullptr;
    impl_->functions = {};
    impl_->path.clear();
}

bool JackApi::isLoaded() const noexcept
{
    return impl_->module != nullptr;
}

const wchar_t* JackApi::loadedPath() const noexcept
{
    return impl_->path.empty() ? nullptr : impl_->path.c_str();
}

const char* JackApi::versionString() const noexcept
{
    return impl_->functions.getVersionString != nullptr ? impl_->functions.getVersionString() : nullptr;
}

jack_client_t* JackApi::clientOpen(const char* clientName,
                                   jack_options_t options,
                                   jack_status_t* status,
                                   const char* serverName) const noexcept
{
    if (impl_->functions.clientOpen == nullptr)
        return nullptr;
    if (serverName != nullptr) {
        options = static_cast<jack_options_t>(options | JackServerName);
        return impl_->functions.clientOpen(clientName, options, status, serverName);
    }
    return impl_->functions.clientOpen(clientName, options, status);
}

int JackApi::clientClose(jack_client_t* client) const noexcept
{
    return impl_->functions.clientClose != nullptr ? impl_->functions.clientClose(client) : -1;
}

int JackApi::clientNameSize() const noexcept
{
    return impl_->functions.clientNameSize != nullptr ? impl_->functions.clientNameSize() : 0;
}

char* JackApi::clientName(jack_client_t* client) const noexcept
{
    return impl_->functions.getClientName != nullptr ? impl_->functions.getClientName(client) : nullptr;
}

int JackApi::activate(jack_client_t* client) const noexcept
{
    return impl_->functions.activate != nullptr ? impl_->functions.activate(client) : -1;
}

int JackApi::deactivate(jack_client_t* client) const noexcept
{
    return impl_->functions.deactivate != nullptr ? impl_->functions.deactivate(client) : -1;
}

void JackApi::setShutdownCallback(jack_client_t* client,
                                  JackShutdownCallback callback,
                                  void* argument) const noexcept
{
    if (impl_->functions.setShutdownCallback != nullptr)
        impl_->functions.setShutdownCallback(client, callback, argument);
}

int JackApi::setProcessCallback(jack_client_t* client,
                                JackProcessCallback callback,
                                void* argument) const noexcept
{
    return impl_->functions.setProcessCallback != nullptr
        ? impl_->functions.setProcessCallback(client, callback, argument)
        : -1;
}

int JackApi::setBufferSizeCallback(jack_client_t* client,
                                   JackBufferSizeCallback callback,
                                   void* argument) const noexcept
{
    return impl_->functions.setBufferSizeCallback != nullptr
        ? impl_->functions.setBufferSizeCallback(client, callback, argument)
        : -1;
}

int JackApi::setSampleRateCallback(jack_client_t* client,
                                   JackSampleRateCallback callback,
                                   void* argument) const noexcept
{
    return impl_->functions.setSampleRateCallback != nullptr
        ? impl_->functions.setSampleRateCallback(client, callback, argument)
        : -1;
}

int JackApi::setGraphOrderCallback(jack_client_t* client,
                                   JackGraphOrderCallback callback,
                                   void* argument) const noexcept
{
    return impl_->functions.setGraphOrderCallback != nullptr
        ? impl_->functions.setGraphOrderCallback(client, callback, argument)
        : -1;
}

    int JackApi::setXRunCallback(jack_client_t* client,
                     JackXRunCallback callback,
                     void* argument) const noexcept
    {
        return impl_->functions.setXRunCallback != nullptr
        ? impl_->functions.setXRunCallback(client, callback, argument)
        : -1;
    }

jack_nframes_t JackApi::sampleRate(jack_client_t* client) const noexcept
{
    return impl_->functions.getSampleRate != nullptr ? impl_->functions.getSampleRate(client) : 0;
}

jack_nframes_t JackApi::bufferSize(jack_client_t* client) const noexcept
{
    return impl_->functions.getBufferSize != nullptr ? impl_->functions.getBufferSize(client) : 0;
}

float JackApi::cpuLoad(jack_client_t* client) const noexcept
{
    return impl_->functions.cpuLoad != nullptr ? impl_->functions.cpuLoad(client) : 0.0f;
}

jack_port_t* JackApi::portRegister(jack_client_t* client,
                                   const char* portName,
                                   const char* portType,
                                   unsigned long flags,
                                   unsigned long bufferSize) const noexcept
{
    return impl_->functions.portRegister != nullptr
        ? impl_->functions.portRegister(client, portName, portType, flags, bufferSize)
        : nullptr;
}

int JackApi::portRename(jack_client_t* client, jack_port_t* port, const char* portName) const noexcept
{
    return impl_->functions.portRename != nullptr
        ? impl_->functions.portRename(client, port, portName)
        : -1;
}

int JackApi::portUnregister(jack_client_t* client, jack_port_t* port) const noexcept
{
    return impl_->functions.portUnregister != nullptr ? impl_->functions.portUnregister(client, port) : -1;
}

void* JackApi::portGetBuffer(jack_port_t* port, jack_nframes_t frames) const noexcept
{
    return impl_->functions.portGetBuffer != nullptr ? impl_->functions.portGetBuffer(port, frames) : nullptr;
}

const char* JackApi::portName(const jack_port_t* port) const noexcept
{
    return impl_->functions.portName != nullptr ? impl_->functions.portName(port) : nullptr;
}

const char* JackApi::portShortName(const jack_port_t* port) const noexcept
{
    return impl_->functions.portShortName != nullptr ? impl_->functions.portShortName(port) : nullptr;
}

int JackApi::portFlags(const jack_port_t* port) const noexcept
{
    return impl_->functions.portFlags != nullptr ? impl_->functions.portFlags(port) : 0;
}

const char* JackApi::portType(const jack_port_t* port) const noexcept
{
    return impl_->functions.portType != nullptr ? impl_->functions.portType(port) : nullptr;
}

const char** JackApi::portGetConnections(const jack_port_t* port) const noexcept
{
    return impl_->functions.portGetConnections != nullptr ? impl_->functions.portGetConnections(port) : nullptr;
}

jack_port_t* JackApi::portByName(jack_client_t* client, const char* portName) const noexcept
{
    return impl_->functions.portByName != nullptr ? impl_->functions.portByName(client, portName) : nullptr;
}

const char** JackApi::getPorts(jack_client_t* client,
                               const char* portNamePattern,
                               const char* typeNamePattern,
                               unsigned long flags) const noexcept
{
    return impl_->functions.getPorts != nullptr
        ? impl_->functions.getPorts(client, portNamePattern, typeNamePattern, flags)
        : nullptr;
}

int JackApi::connect(jack_client_t* client,
                     const char* sourcePort,
                     const char* destinationPort) const noexcept
{
    return impl_->functions.connect != nullptr ? impl_->functions.connect(client, sourcePort, destinationPort) : -1;
}

int JackApi::disconnect(jack_client_t* client,
                        const char* sourcePort,
                        const char* destinationPort) const noexcept
{
    return impl_->functions.disconnect != nullptr
        ? impl_->functions.disconnect(client, sourcePort, destinationPort)
        : -1;
}

void JackApi::freeMemory(void* memory) const noexcept
{
    if (impl_->functions.free != nullptr && memory != nullptr)
        impl_->functions.free(memory);
}

std::uint32_t JackApi::midiEventCount(void* portBuffer) const noexcept
{
    return impl_->functions.midiEventCount != nullptr ? impl_->functions.midiEventCount(portBuffer) : 0;
}

int JackApi::midiEventGet(jack_midi_event_t* event,
                          void* portBuffer,
                          std::uint32_t eventIndex) const noexcept
{
    return impl_->functions.midiEventGet != nullptr
        ? impl_->functions.midiEventGet(event, portBuffer, eventIndex)
        : -1;
}

void JackApi::midiClearBuffer(void* portBuffer) const noexcept
{
    if (impl_->functions.midiClearBuffer != nullptr)
        impl_->functions.midiClearBuffer(portBuffer);
}

std::size_t JackApi::midiMaxEventSize(void* portBuffer) const noexcept
{
    return impl_->functions.midiMaxEventSize != nullptr ? impl_->functions.midiMaxEventSize(portBuffer) : 0;
}

jack_midi_data_t* JackApi::midiEventReserve(void* portBuffer,
                                            jack_nframes_t time,
                                            std::size_t dataSize) const noexcept
{
    return impl_->functions.midiEventReserve != nullptr
        ? impl_->functions.midiEventReserve(portBuffer, time, dataSize)
        : nullptr;
}

int JackApi::midiEventWrite(void* portBuffer,
                            jack_nframes_t time,
                            const jack_midi_data_t* data,
                            std::size_t dataSize) const noexcept
{
    return impl_->functions.midiEventWrite != nullptr
        ? impl_->functions.midiEventWrite(portBuffer, time, data, dataSize)
        : -1;
}

std::uint32_t JackApi::midiLostEventCount(void* portBuffer) const noexcept
{
    return impl_->functions.midiLostEventCount != nullptr ? impl_->functions.midiLostEventCount(portBuffer) : 0;
}

jack_ringbuffer_t* JackApi::ringbufferCreate(std::size_t size) const noexcept
{
    return impl_->functions.ringbufferCreate != nullptr ? impl_->functions.ringbufferCreate(size) : nullptr;
}

void JackApi::ringbufferFree(jack_ringbuffer_t* ringbuffer) const noexcept
{
    if (impl_->functions.ringbufferFree != nullptr && ringbuffer != nullptr)
        impl_->functions.ringbufferFree(ringbuffer);
}

void JackApi::ringbufferGetReadVector(const jack_ringbuffer_t* ringbuffer,
                                      jack_ringbuffer_data_t vectors[2]) const noexcept
{
    if (impl_->functions.ringbufferGetReadVector != nullptr)
        impl_->functions.ringbufferGetReadVector(ringbuffer, vectors);
}

void JackApi::ringbufferGetWriteVector(const jack_ringbuffer_t* ringbuffer,
                                       jack_ringbuffer_data_t vectors[2]) const noexcept
{
    if (impl_->functions.ringbufferGetWriteVector != nullptr)
        impl_->functions.ringbufferGetWriteVector(ringbuffer, vectors);
}

std::size_t JackApi::ringbufferRead(jack_ringbuffer_t* ringbuffer,
                                    char* destination,
                                    std::size_t count) const noexcept
{
    return impl_->functions.ringbufferRead != nullptr ? impl_->functions.ringbufferRead(ringbuffer, destination, count) : 0;
}

std::size_t JackApi::ringbufferPeek(jack_ringbuffer_t* ringbuffer,
                                    char* destination,
                                    std::size_t count) const noexcept
{
    return impl_->functions.ringbufferPeek != nullptr ? impl_->functions.ringbufferPeek(ringbuffer, destination, count) : 0;
}

void JackApi::ringbufferReadAdvance(jack_ringbuffer_t* ringbuffer, std::size_t count) const noexcept
{
    if (impl_->functions.ringbufferReadAdvance != nullptr)
        impl_->functions.ringbufferReadAdvance(ringbuffer, count);
}

std::size_t JackApi::ringbufferReadSpace(const jack_ringbuffer_t* ringbuffer) const noexcept
{
    return impl_->functions.ringbufferReadSpace != nullptr ? impl_->functions.ringbufferReadSpace(ringbuffer) : 0;
}

int JackApi::ringbufferMlock(jack_ringbuffer_t* ringbuffer) const noexcept
{
    return impl_->functions.ringbufferMlock != nullptr ? impl_->functions.ringbufferMlock(ringbuffer) : -1;
}

void JackApi::ringbufferReset(jack_ringbuffer_t* ringbuffer) const noexcept
{
    if (impl_->functions.ringbufferReset != nullptr)
        impl_->functions.ringbufferReset(ringbuffer);
}

std::size_t JackApi::ringbufferWrite(jack_ringbuffer_t* ringbuffer,
                                     const char* source,
                                     std::size_t count) const noexcept
{
    return impl_->functions.ringbufferWrite != nullptr ? impl_->functions.ringbufferWrite(ringbuffer, source, count) : 0;
}

void JackApi::ringbufferWriteAdvance(jack_ringbuffer_t* ringbuffer, std::size_t count) const noexcept
{
    if (impl_->functions.ringbufferWriteAdvance != nullptr)
        impl_->functions.ringbufferWriteAdvance(ringbuffer, count);
}

std::size_t JackApi::ringbufferWriteSpace(const jack_ringbuffer_t* ringbuffer) const noexcept
{
    return impl_->functions.ringbufferWriteSpace != nullptr ? impl_->functions.ringbufferWriteSpace(ringbuffer) : 0;
}

}