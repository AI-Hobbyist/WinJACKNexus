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

#include <cstdint>

namespace WinJACKNexus::Audio {

struct JackServerOptions final {
    const wchar_t* jackdPath = nullptr;
    const wchar_t* backend = L"dummy";
    std::uint32_t sampleRate = 48000;
    std::uint32_t periodFrames = 256;
    bool enableRealtime = false;
};

class WINJACK_AUDIO_API JackServerGuard final {
public:
    JackServerGuard() noexcept;
    ~JackServerGuard() noexcept;

    JackServerGuard(const JackServerGuard&) = delete;
    JackServerGuard& operator=(const JackServerGuard&) = delete;
    JackServerGuard(JackServerGuard&& other) noexcept;
    JackServerGuard& operator=(JackServerGuard&& other) noexcept;

    bool locate(const wchar_t* explicitPath = nullptr) noexcept;
    bool start(const JackServerOptions& options = {}) noexcept;
    bool waitUntilReady(std::uint32_t timeoutMilliseconds = 5000) noexcept;
    bool stop() noexcept;
    bool isRunning() const noexcept;
    const wchar_t* executablePath() const noexcept;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}