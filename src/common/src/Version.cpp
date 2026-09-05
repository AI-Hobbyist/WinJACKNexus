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
#include <WinJACKNexus/Common/Version.hpp>

#include <algorithm>
#include <charconv>
#include <cstring>

namespace WinJACKNexus::Common {

VersionInfo::VersionInfo(std::uint32_t majorVersion,
                         std::uint32_t minorVersion,
                         std::uint32_t patchVersion,
                         std::string_view gitHashValue) noexcept
    : major(majorVersion)
    , minor(minorVersion)
    , patch(patchVersion)
{
    const auto length = std::min(gitHashValue.size(), gitHash_.size() - 1);
    std::memcpy(gitHash_.data(), gitHashValue.data(), length);
    gitHash_[length] = '\0';
}

std::string VersionInfo::toString() const
{
    std::string result = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    if (gitHash_[0] != '\0')
        result += "+" + gitHashString();
    return result;
}

std::string VersionInfo::gitHashString() const
{
    return gitHash_.data();
}

std::optional<VersionInfo> VersionInfo::fromString(std::string_view value)
{
    const auto separator = value.find('+');
    const auto versionPart = value.substr(0, separator);
    const auto hashPart = separator == std::string_view::npos
        ? std::string_view {}
        : value.substr(separator + 1);

    std::uint32_t components[3] {};
    std::size_t componentStart = 0;
    for (std::size_t index = 0; index < 3; ++index) {
        const auto componentEnd = versionPart.find('.', componentStart);
        if (index < 2 && componentEnd == std::string_view::npos)
            return std::nullopt;
        if (index == 2 && componentEnd != std::string_view::npos)
            return std::nullopt;

        const auto tokenEnd = componentEnd == std::string_view::npos
            ? versionPart.size()
            : componentEnd;
        if (componentStart == tokenEnd)
            return std::nullopt;

        const auto* begin = versionPart.data() + componentStart;
        const auto* end = versionPart.data() + tokenEnd;
        const auto parsed = std::from_chars(begin, end, components[index]);
        if (parsed.ec != std::errc {} || parsed.ptr != end)
            return std::nullopt;

        componentStart = tokenEnd + (componentEnd == std::string_view::npos ? 0 : 1);
    }

    if (componentStart != versionPart.size())
        return std::nullopt;

    return VersionInfo(components[0], components[1], components[2], hashPart);
}

}