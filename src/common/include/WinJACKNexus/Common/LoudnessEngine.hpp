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

#include <WinJACKNexus/Common/ChannelLayout.hpp>
#include <WinJACKNexus/Common/Export.hpp>
#include <WinJACKNexus/Common/MeterSnapshot.hpp>

#include <cstddef>

namespace WinJACKNexus::Common
{

struct BiquadCoefficients final
{
    double b0{};
    double b1{};
    double b2{};
    double a1{};
    double a2{};
};

struct KWeightingCoefficients final
{
    BiquadCoefficients shelf;
    BiquadCoefficients highPass;
    bool valid{};
};

WINJACK_COMMON_API KWeightingCoefficients kWeightingCoefficients(double sampleRate) noexcept;

struct LoudnessEngineConfiguration final
{
    double sampleRate{48000.0};
    bool enableTruePeak{true};
};

class WINJACK_COMMON_API LoudnessEngine final
{
  public:
    LoudnessEngine();
    ~LoudnessEngine();

    LoudnessEngine(const LoudnessEngine &) = delete;
    LoudnessEngine &operator=(const LoudnessEngine &) = delete;

    bool prepare(const LoudnessEngineConfiguration &configuration,
                 const MeterChannelLayout &layout) noexcept;
    bool isPrepared() const noexcept;
    void reset() noexcept;
    void process(const float *const *channels, std::size_t channelCount,
                 std::size_t frames) noexcept;
    MeterSnapshot snapshot() const noexcept;

  private:
    struct Impl;
    Impl *impl_{};
};

}