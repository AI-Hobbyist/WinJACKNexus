#pragma once

#include <WinJACKNexus/Audio/Export.hpp>
#include <WinJACKNexus/Audio/JackApi.hpp>

namespace WinJACKNexus::Audio {

struct JackClientOptions final {
    const char* name { "WinJACKNexus" };
    bool autoStartServer { true };
    bool useExactName { false };
    const char* serverName { nullptr };
};

class WINJACK_AUDIO_API JackClient final {
public:
    JackClient() noexcept;
    ~JackClient() noexcept;

    JackClient(const JackClient&) = delete;
    JackClient& operator=(const JackClient&) = delete;
    JackClient(JackClient&& other) noexcept;
    JackClient& operator=(JackClient&& other) noexcept;

    bool open(const JackClientOptions& options = {}) noexcept;
    bool close() noexcept;
    bool activate() noexcept;
    bool deactivate() noexcept;

    bool isOpen() const noexcept;
    bool isActive() const noexcept;
    jack_client_t* nativeHandle() const noexcept;
    const char* name() const noexcept;
    jack_status_t lastStatus() const noexcept;

    void setShutdownCallback(JackShutdownCallback callback, void* argument) noexcept;
    int setProcessCallback(JackProcessCallback callback, void* argument) noexcept;
    int setBufferSizeCallback(JackBufferSizeCallback callback, void* argument) noexcept;
    int setSampleRateCallback(JackSampleRateCallback callback, void* argument) noexcept;
    int setGraphOrderCallback(JackGraphOrderCallback callback, void* argument) noexcept;

    jack_nframes_t sampleRate() const noexcept;
    jack_nframes_t bufferSize() const noexcept;
    float cpuLoad() const noexcept;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}