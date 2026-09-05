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

#include <cstddef>
#include <cstdint>

namespace WinJACKNexus::Audio {

constexpr std::size_t wasapiDeviceStringCapacity = 512;

struct WasapiDeviceInfo final {
    std::uint32_t index = 0;
    wchar_t id[wasapiDeviceStringCapacity] {};
    wchar_t name[wasapiDeviceStringCapacity] {};
    bool isDefault = false;
    bool isInput = false;
    bool isOutput = false;
};

using WasapiDeviceVisitor = bool (*)(const WasapiDeviceInfo& device, void* userData) noexcept;

class WINJACK_AUDIO_API WasapiDeviceEnumerator final {
public:
    WasapiDeviceEnumerator() noexcept = default;

    std::size_t enumerate(WasapiStreamDirection direction,
                          WasapiDeviceVisitor visitor,
                          void* userData) noexcept;
    bool defaultDevice(WasapiStreamDirection direction, WasapiDeviceInfo& result) noexcept;
    std::int32_t lastError() const noexcept;

private:
    std::int32_t lastError_ = 0;
};

}