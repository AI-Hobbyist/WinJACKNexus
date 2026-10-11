/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#include <array>
#include <cstddef>

namespace WinJACKNexus::Common
{

enum class ParametricEqShape : int
{
    lowShelf,
    bell,
    highShelf,
    lowPass,
    highPass
};

struct ParametricEqBand final
{
    float frequencyHz = 1000.0F;
    float q = 1.0F;
    float gainDb = 0.0F;
    ParametricEqShape shape = ParametricEqShape::bell;
    bool enabled = true;
};

struct ParametricEqParameters final
{
    std::array<ParametricEqBand, 4> bands{
        {{80.0F, 0.7F, 0.0F, ParametricEqShape::lowShelf, true},
         {200.0F, 1.0F, 0.0F, ParametricEqShape::bell, true},
         {3500.0F, 2.0F, 0.0F, ParametricEqShape::bell, true},
         {10000.0F, 0.7F, 0.0F, ParametricEqShape::highShelf, true}}};
    float highPassHz = 30.0F;
    float lowPassHz = 20000.0F;
    int highPassSlope = 12;
    int lowPassSlope = 12;
    float outputGainDb = 0.0F;
    bool highPassEnabled = false;
    bool lowPassEnabled = false;
    bool enabled = true;
};

class WJN_FX_API ParametricEqProcessor final
{
  public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setParameters(const ParametricEqParameters &parameters) noexcept;
    float process(float sample) noexcept;
    static float responseDb(const ParametricEqParameters &parameters, double sampleRate,
                            float frequencyHz) noexcept;

  private:
    struct Biquad final
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        float process(float input) noexcept;
        double magnitude(double radians) const noexcept;
    };
    void rebuild() noexcept;
    std::array<Biquad, 12> filters_{};
    std::size_t filterCount_ = 0;
    ParametricEqParameters parameters_{};
    double sampleRate_ = 48000.0;
    float outputGain_ = 1.0F;
};

}
