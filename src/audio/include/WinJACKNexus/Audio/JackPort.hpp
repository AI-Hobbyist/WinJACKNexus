#pragma once

#include <WinJACKNexus/Audio/Export.hpp>
#include <WinJACKNexus/Audio/JackClient.hpp>

#include <cstddef>

namespace WinJACKNexus::Audio {

using JackPortNameVisitor = bool (*)(const char* name, void* userData) noexcept;

class WINJACK_AUDIO_API JackPort final {
public:
    JackPort() noexcept = default;
    ~JackPort() noexcept;

    JackPort(const JackPort&) = delete;
    JackPort& operator=(const JackPort&) = delete;
    JackPort(JackPort&& other) noexcept;
    JackPort& operator=(JackPort&& other) noexcept;

    bool registerPort(JackClient& client,
                      const char* portName,
                      const char* portType,
                      unsigned long flags,
                      unsigned long bufferSize = 0) noexcept;
    bool unregisterPort() noexcept;

    bool isValid() const noexcept;
    jack_port_t* nativeHandle() const noexcept;
    void* buffer(jack_nframes_t frames) const noexcept;
    jack_default_audio_sample_t* audioBuffer(jack_nframes_t frames) const noexcept;
    void* midiBuffer(jack_nframes_t frames) const noexcept;
    const char* name() const noexcept;
    const char* shortName() const noexcept;
    const char* type() const noexcept;
    int flags() const noexcept;
    bool isInput() const noexcept;
    bool isOutput() const noexcept;
    std::size_t visitConnections(JackPortNameVisitor visitor, void* userData) const noexcept;

private:
    JackClient* client_ = nullptr;
    jack_port_t* port_ = nullptr;
};

}