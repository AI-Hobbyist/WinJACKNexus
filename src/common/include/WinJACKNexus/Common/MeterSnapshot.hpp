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

namespace WinJACKNexus::Common
{

struct MeterValue final
{
    double value{};
    bool valid{};
};

struct MeterSnapshot final
{
    MeterValue samplePeakDbfs;
    MeterValue rmsDbfs;
    MeterValue truePeakDbtp;
    MeterValue lfePeakDbfs;
    MeterValue momentaryLufs;
    MeterValue shortTermLufs;
    MeterValue integratedLufs;
    MeterValue loudnessRangeLu;
    std::uint64_t processedFrames{};
    std::uint64_t invalidSampleCount{};
};

}