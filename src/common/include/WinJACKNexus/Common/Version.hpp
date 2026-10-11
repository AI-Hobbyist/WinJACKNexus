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

#include <WinJACKNexus/Common/Export.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace WinJACKNexus::Common
{

WINJACK_COMMON_API const char *libraryVersion() noexcept;

struct WINJACK_COMMON_API VersionInfo final
{
    std::uint32_t major{};
    std::uint32_t minor{};
    std::uint32_t patch{};

    VersionInfo() noexcept = default;
    VersionInfo(std::uint32_t majorVersion, std::uint32_t minorVersion, std::uint32_t patchVersion,
                std::string_view gitHashValue = {}) noexcept;

    std::string toString() const;
    std::string gitHashString() const;
    static std::optional<VersionInfo> fromString(std::string_view value);

    bool operator==(const VersionInfo &) const noexcept = default;

  private:
    std::array<char, 65> gitHash_{};
};

}