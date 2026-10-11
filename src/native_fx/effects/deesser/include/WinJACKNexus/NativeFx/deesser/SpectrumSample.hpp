/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace WinJACKNexus::Common
{

// Kept as a pair in the SPSC queue so input and processed spectra always use
// samples from the same audio frames.
struct DeEsserSpectrumSample final
{
    float input = 0.0F;
    float processed = 0.0F;
};

struct DeEsserLiveSpectrumFrame final
{
    std::array<std::array<DeEsserSpectrumSample, 512>, 8> samples{};
    std::array<std::size_t, 8> counts{};
    std::uint32_t sampleRate{48000};
};

}
