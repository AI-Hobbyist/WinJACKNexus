#pragma once

#include <WinJACKNexus/Audio/Export.hpp>

#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>

#include <cstddef>
#include <cstdint>

namespace WinJACKNexus::Audio {

class WINJACK_AUDIO_API JackApi final {
public:
    static JackApi& instance() noexcept;

    bool load(const wchar_t* explicitPath = nullptr) noexcept;
    void unload() noexcept;
    bool isLoaded() const noexcept;
    const wchar_t* loadedPath() const noexcept;
    const char* versionString() const noexcept;

    jack_client_t* clientOpen(const char* clientName,
                              jack_options_t options,
                              jack_status_t* status,
                              const char* serverName = nullptr) const noexcept;
    int clientClose(jack_client_t* client) const noexcept;
    int clientNameSize() const noexcept;
    char* clientName(jack_client_t* client) const noexcept;
    int activate(jack_client_t* client) const noexcept;
    int deactivate(jack_client_t* client) const noexcept;

    void setShutdownCallback(jack_client_t* client,
                             JackShutdownCallback callback,
                             void* argument) const noexcept;
    int setProcessCallback(jack_client_t* client,
                           JackProcessCallback callback,
                           void* argument) const noexcept;
    int setBufferSizeCallback(jack_client_t* client,
                              JackBufferSizeCallback callback,
                              void* argument) const noexcept;
    int setSampleRateCallback(jack_client_t* client,
                              JackSampleRateCallback callback,
                              void* argument) const noexcept;
    int setGraphOrderCallback(jack_client_t* client,
                              JackGraphOrderCallback callback,
                              void* argument) const noexcept;
    jack_nframes_t sampleRate(jack_client_t* client) const noexcept;
    jack_nframes_t bufferSize(jack_client_t* client) const noexcept;
    float cpuLoad(jack_client_t* client) const noexcept;

    jack_port_t* portRegister(jack_client_t* client,
                              const char* portName,
                              const char* portType,
                              unsigned long flags,
                              unsigned long bufferSize) const noexcept;
    int portUnregister(jack_client_t* client, jack_port_t* port) const noexcept;
    void* portGetBuffer(jack_port_t* port, jack_nframes_t frames) const noexcept;
    const char* portName(const jack_port_t* port) const noexcept;
    const char* portShortName(const jack_port_t* port) const noexcept;
    int portFlags(const jack_port_t* port) const noexcept;
    const char* portType(const jack_port_t* port) const noexcept;
    const char** portGetConnections(const jack_port_t* port) const noexcept;
    jack_port_t* portByName(jack_client_t* client, const char* portName) const noexcept;
    const char** getPorts(jack_client_t* client,
                          const char* portNamePattern,
                          const char* typeNamePattern,
                          unsigned long flags) const noexcept;
    int connect(jack_client_t* client,
                const char* sourcePort,
                const char* destinationPort) const noexcept;
    int disconnect(jack_client_t* client,
                   const char* sourcePort,
                   const char* destinationPort) const noexcept;

    void freeMemory(void* memory) const noexcept;

    std::uint32_t midiEventCount(void* portBuffer) const noexcept;
    int midiEventGet(jack_midi_event_t* event,
                     void* portBuffer,
                     std::uint32_t eventIndex) const noexcept;
    void midiClearBuffer(void* portBuffer) const noexcept;
    std::size_t midiMaxEventSize(void* portBuffer) const noexcept;
    jack_midi_data_t* midiEventReserve(void* portBuffer,
                                       jack_nframes_t time,
                                       std::size_t dataSize) const noexcept;
    int midiEventWrite(void* portBuffer,
                       jack_nframes_t time,
                       const jack_midi_data_t* data,
                       std::size_t dataSize) const noexcept;
    std::uint32_t midiLostEventCount(void* portBuffer) const noexcept;

    jack_ringbuffer_t* ringbufferCreate(std::size_t size) const noexcept;
    void ringbufferFree(jack_ringbuffer_t* ringbuffer) const noexcept;
    void ringbufferGetReadVector(const jack_ringbuffer_t* ringbuffer,
                                 jack_ringbuffer_data_t vectors[2]) const noexcept;
    void ringbufferGetWriteVector(const jack_ringbuffer_t* ringbuffer,
                                  jack_ringbuffer_data_t vectors[2]) const noexcept;
    std::size_t ringbufferRead(jack_ringbuffer_t* ringbuffer,
                               char* destination,
                               std::size_t count) const noexcept;
    std::size_t ringbufferPeek(jack_ringbuffer_t* ringbuffer,
                               char* destination,
                               std::size_t count) const noexcept;
    void ringbufferReadAdvance(jack_ringbuffer_t* ringbuffer,
                               std::size_t count) const noexcept;
    std::size_t ringbufferReadSpace(const jack_ringbuffer_t* ringbuffer) const noexcept;
    int ringbufferMlock(jack_ringbuffer_t* ringbuffer) const noexcept;
    void ringbufferReset(jack_ringbuffer_t* ringbuffer) const noexcept;
    std::size_t ringbufferWrite(jack_ringbuffer_t* ringbuffer,
                                const char* source,
                                std::size_t count) const noexcept;
    void ringbufferWriteAdvance(jack_ringbuffer_t* ringbuffer,
                                std::size_t count) const noexcept;
    std::size_t ringbufferWriteSpace(const jack_ringbuffer_t* ringbuffer) const noexcept;

private:
    JackApi();
    ~JackApi() noexcept;

    JackApi(const JackApi&) = delete;
    JackApi& operator=(const JackApi&) = delete;

    struct Impl;
    Impl* impl_;
};

}