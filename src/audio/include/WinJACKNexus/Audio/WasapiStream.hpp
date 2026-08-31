#pragma once

#include <WinJACKNexus/Audio/Export.hpp>
#include <WinJACKNexus/Audio/WasapiFormat.hpp>

#include <cstdint>

namespace WinJACKNexus::Audio {

struct WasapiStreamOptions final {
    const wchar_t* deviceId = nullptr;
    std::uint32_t requestedSampleRate = 0;
    std::uint16_t requestedChannels = 0;
};

using WasapiRenderCallback = void (*)(float* interleavedSamples,
                                      std::uint32_t frames,
                                      void* userData) noexcept;
using WasapiCaptureCallback = void (*)(const float* interleavedSamples,
                                        std::uint32_t frames,
                                        void* userData) noexcept;

class WINJACK_AUDIO_API WasapiStream final {
public:
    struct Impl;

    WasapiStream() noexcept;
    ~WasapiStream() noexcept;

    WasapiStream(const WasapiStream&) = delete;
    WasapiStream& operator=(const WasapiStream&) = delete;
    WasapiStream(WasapiStream&& other) noexcept;
    WasapiStream& operator=(WasapiStream&& other) noexcept;

    bool open(WasapiStreamDirection direction,
              const WasapiStreamOptions& options = {}) noexcept;
    bool start(WasapiRenderCallback callback, void* userData = nullptr) noexcept;
    bool start(WasapiCaptureCallback callback, void* userData = nullptr) noexcept;
    bool stop() noexcept;
    void close() noexcept;

    bool isOpen() const noexcept;
    bool isRunning() const noexcept;
    WasapiStreamDirection direction() const noexcept;
    WasapiStreamFormat format() const noexcept;
    std::uint32_t bufferFrames() const noexcept;
    std::int32_t lastError() const noexcept;

private:
    Impl* impl_ = nullptr;
};

}