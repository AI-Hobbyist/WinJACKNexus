/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <WinJACKNexus/NativeFx/NativeFxApi.h>

#include <array>
#include <cstddef>

namespace WinJACKNexus::Common
{

inline constexpr std::size_t graphicEqBandCount = 31;
inline constexpr std::array<float, graphicEqBandCount> graphicEqFlatGains{};
inline constexpr std::array<float, graphicEqBandCount> graphicEqFrequencies{
    20,   25,   31.5F, 40,   50,   63,    80,    100,   125,  160,  200,
    250,  315,  400,   500,  630,  800,   1000,  1250,  1600, 2000, 2500,
    3150, 4000, 5000,  6300, 8000, 10000, 12500, 16000, 20000};

// Optional factory response available in the preset selector; the actual default is flat.
inline constexpr std::array<float, graphicEqBandCount> graphicEqVocalBrightGains{
    -2.4F, -2.5F, -2.3F, -1.5F, -0.1F, 1.1F,  2.2F,  3.0F,  3.5F,  4.0F,  4.5F,
    4.7F,  4.4F,  3.4F,  2.0F,  0.7F,  -0.8F, -1.8F, -2.8F, -3.3F, -2.9F, -2.3F,
    -1.3F, -0.1F, 1.2F,  2.2F,  3.2F,  3.7F,  3.8F,  2.7F,  1.2F};

inline constexpr std::array<float, graphicEqBandCount> graphicEqBassBoostGains{
    5.0F,  4.7F,  4.4F, 4.0F, 3.6F, 3.0F, 2.4F, 1.6F, 0.8F, 0.0F, -0.6F,
    -0.8F, -0.6F, 0.0F, 0.4F, 0.4F, 0.2F, 0.0F, 0.0F, 0.0F, 0.0F, 0.2F,
    0.4F,  0.6F,  0.8F, 0.8F, 0.6F, 0.4F, 0.2F, 0.0F, 0.0F};

inline constexpr std::array<float, graphicEqBandCount> graphicEqClassicalGains{
    1.5F,  1.5F,  1.5F,  1.5F,  1.5F,  1.5F,  1.5F,  1.5F,  1.5F,  1.0F,  0.5F,
    0.0F,  -0.2F, -0.4F, -0.5F, -0.5F, -0.5F, -0.5F, -0.5F, -0.5F, -0.5F, -0.5F,
    -0.2F, 0.0F,  0.2F,  0.5F,  0.8F,  1.0F,  1.0F,  1.0F,  1.0F};

struct GraphicEqParameters final
{
    std::array<float, graphicEqBandCount> bandGainsDb = graphicEqFlatGains;
    float outputGainDb = 0.0F;
    bool enabled = true;
};

// A mono 31-band graphic EQ. Change parameters outside the real-time callback.
class WJN_FX_API GraphicEqProcessor final
{
  public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setParameters(const GraphicEqParameters &parameters) noexcept;
    float process(float sample) noexcept;

  private:
    struct Biquad final
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;
        bool settling = false;
    };
    void updateFilters() noexcept;
    void rebuildActiveFilterList() noexcept;
    GraphicEqParameters parameters_{};
    std::array<Biquad, graphicEqBandCount> filters_{};
    std::array<std::size_t, graphicEqBandCount> activeFilterIndices_{};
    std::size_t activeFilterCount_ = 0;
    double sampleRate_ = 48000.0;
    float outputGain_ = 1.0F;
};

}
