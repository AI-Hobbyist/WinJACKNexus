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