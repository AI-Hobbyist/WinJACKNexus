/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <array>
#include <cstddef>
#include <string>
namespace WinJACKNexus::Common
{
// Vocabulary order does not change a layout's sample-buffer order.
inline constexpr std::array<const char *, 16> standardChannelNames{
    "L",   "C",   "R",   "Lw",  "Rw",  "Ls",  "Rs",  "Lrb",
    "Rrb", "LFE", "Ltf", "Rtf", "Ltm", "Rtm", "Ltr", "Rtr"};
inline constexpr std::array<const char *, 8> surroundChannelNames{
    standardChannelNames[0], standardChannelNames[2], standardChannelNames[1],
    standardChannelNames[9], standardChannelNames[5], standardChannelNames[6],
    standardChannelNames[7], standardChannelNames[8]};
inline constexpr std::array<const char *, 9> surroundChannelChoices{"All",
                                                                    surroundChannelNames[0],
                                                                    surroundChannelNames[1],
                                                                    surroundChannelNames[2],
                                                                    surroundChannelNames[3],
                                                                    surroundChannelNames[4],
                                                                    surroundChannelNames[5],
                                                                    surroundChannelNames[6],
                                                                    surroundChannelNames[7]};
inline std::string standardChannelName(std::size_t index, std::size_t count)
{
    if (count == 1 && index == 0)
        return "M";
    if (count == 4 && index < 4)
    {
        constexpr std::array<std::size_t, 4> quad{0, 1, 6, 7};
        return surroundChannelNames[quad[index]];
    }
    if (index < 8)
        return surroundChannelNames[index];
    constexpr std::array<std::size_t, 8> extended{3, 4, 10, 11, 12, 13, 14, 15};
    if (index < 16)
        return standardChannelNames[extended[index - 8]];
    return "Ch_" + std::to_string(index + 1);
}
}
