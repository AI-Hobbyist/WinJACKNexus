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

#include <cstdint>

namespace WinJACKNexus::Patchbay::Core {

enum class PortKind : std::uint8_t {
    audio,
    midi,
    cv,
    osc,
    unknown
};

enum class Direction : std::uint8_t {
    input,
    output,
    unknown
};

enum class PortGroupHint : std::uint32_t {
    none = 0,
    mainInput = 1u << 0,
    mainOutput = 1u << 1,
    stereo = 1u << 2,
    midSide = 1u << 3
};

constexpr PortGroupHint operator|(PortGroupHint left, PortGroupHint right) noexcept
{
    return static_cast<PortGroupHint>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

constexpr bool hasHint(PortGroupHint value, PortGroupHint hint) noexcept
{
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(hint)) != 0;
}

}