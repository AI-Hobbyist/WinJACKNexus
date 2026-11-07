// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace WinJACKNexus::Common
{
enum class SpectrogramPalette
{
    rainbow,
    fire,
    blueCyan,
    grayscale,
    custom
};
inline constexpr double spectrogramMinimumDb = -72;
inline constexpr double spectrogramMaximumDb = 12;
inline double spectrogramColorAmount(double db)
{
    return std::clamp((db - spectrogramMinimumDb) / -spectrogramMinimumDb, 0.0, 1.0);
}

// Fixed visualization LUT data. Custom is resolved by the UI's theme tokens.
inline std::array<double, 3> spectrogramPaletteColor(SpectrogramPalette palette, double amount)
{
    // Uniform samples from the user-provided reference color bands.
    static constexpr std::array<std::uint32_t, 9> rainbow{0x17104e, 0x2335b0, 0x2173e0, 0x16c5ec, 0x2ed88d,
                                                          0x87e35a, 0xf6db49, 0xf98538, 0xf33d64};
    static constexpr std::array<std::uint32_t, 9> fire{0x080913, 0x2f154e, 0x542088, 0x892f89, 0xc73f64,
                                                       0xea634d, 0xfb9540, 0xffc471, 0xfff3b8};
    static constexpr std::array<std::uint32_t, 9> blueCyan{0x071021, 0x0c224a, 0x103472, 0x125092, 0x1272ac,
                                                           0x1b96c5, 0x48c1dc, 0x84e5f0, 0xdffefe};
    static constexpr std::array<std::uint32_t, 2> grayscale{0x000000, 0xffffff};
    std::span<const std::uint32_t> stops;
    switch (palette)
    {
    case SpectrogramPalette::rainbow:
        stops = rainbow;
        break;
    case SpectrogramPalette::fire:
        stops = fire;
        break;
    case SpectrogramPalette::blueCyan:
        stops = blueCyan;
        break;
    case SpectrogramPalette::grayscale:
        stops = grayscale;
        break;
    case SpectrogramPalette::custom:
        return {};
    }
    const double position = std::clamp(amount, 0.0, 1.0) * (stops.size() - 1);
    const auto index = std::min(unsigned(position), unsigned(stops.size() - 2));
    const double blend = position - index;
    std::array<double, 3> result{};
    for (unsigned channel = 0; channel < result.size(); ++channel)
    {
        const auto shift = 16 - channel * 8;
        const double first = (stops[index] >> shift) & 255;
        const double second = (stops[index + 1] >> shift) & 255;
        result[channel] = (first + (second - first) * blend) / 255.0;
    }
    return result;
}
} // namespace WinJACKNexus::Common
