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

#include <cstddef>
#include <cstdint>

namespace WinJACKNexus::Audio {

constexpr std::size_t midiEventDataCapacity = 1024;

struct WINJACK_AUDIO_API MidiEvent final {
    std::uint32_t timestampMilliseconds = 0;
    std::uint16_t size = 0;
    std::uint8_t data[midiEventDataCapacity] {};

    bool assign(const std::uint8_t* source,
                std::size_t sourceSize,
                std::uint32_t timestamp = 0) noexcept;
    bool assignShortMessage(std::uint32_t message,
                            std::uint32_t timestamp = 0) noexcept;
};

}