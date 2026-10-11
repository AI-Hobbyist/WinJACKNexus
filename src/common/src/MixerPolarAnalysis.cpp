/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Common/MixerPolarAnalysis.hpp>

#include <algorithm>
#include <cmath>

namespace WinJACKNexus::Common
{

MixerPolarAnalyzer::MixerPolarAnalyzer(std::uint64_t sourceId, std::uint64_t targetId,
                                       MixerLayout sourceLayout, MixerLayout targetLayout,
                                       std::uint64_t topologyVersion,
                                       std::uint32_t sampleRate) noexcept
    : windowSamples_(std::max(
          1U, static_cast<std::uint32_t>(std::lround(static_cast<double>(sampleRate) * .02))))
{
    accumulating_.sourceId = sourceId;
    accumulating_.targetId = targetId;
    accumulating_.sourceLayout = sourceLayout;
    accumulating_.targetLayout = targetLayout;
    accumulating_.topologyVersion = topologyVersion;
    accumulating_.sampleRate = sampleRate;
}

void MixerPolarAnalyzer::push(std::span<const float *const> input,
                              std::span<const float *const> output, std::size_t frames) noexcept
{
    if (input.size() > 8 || output.size() > 8)
        return;
    for (std::size_t frame = 0; frame < frames; ++frame)
    {
        for (std::size_t channel = 0; channel < input.size(); ++channel)
        {
            const float value = input[channel] ? input[channel][frame] : 0.0F;
            if (!std::isfinite(value))
            {
                accumulating_.valid = false;
                continue;
            }
            accumulating_.inputSumSquares[channel] += static_cast<double>(value) * value;
            accumulating_.inputPeak[channel] =
                std::max(accumulating_.inputPeak[channel], std::abs(value));
        }
        for (std::size_t channel = 0; channel < output.size(); ++channel)
        {
            const float value = output[channel] ? output[channel][frame] : 0.0F;
            if (!std::isfinite(value))
            {
                accumulating_.valid = false;
                continue;
            }
            accumulating_.outputSumSquares[channel] += static_cast<double>(value) * value;
            accumulating_.outputPeak[channel] =
                std::max(accumulating_.outputPeak[channel], std::abs(value));
        }
        ++accumulating_.sampleCount;
        ++sampleIndex_;
        if (accumulating_.sampleCount == windowSamples_)
            finishWindow();
    }
}

void MixerPolarAnalyzer::finishWindow() noexcept
{
    accumulating_.endSample = sampleIndex_;
    if (!completed_.try_push(accumulating_))
        dropped_.fetch_add(1, std::memory_order_relaxed);
    const auto first = sampleIndex_;
    auto next = accumulating_;
    next.firstSample = first;
    next.endSample = first;
    next.sampleCount = 0;
    next.inputSumSquares.fill(0.0);
    next.outputSumSquares.fill(0.0);
    next.inputPeak.fill(0.0F);
    next.outputPeak.fill(0.0F);
    next.valid = true;
    accumulating_ = next;
}

bool MixerPolarAnalyzer::readLatest(MixerPolarSnapshot &snapshot) noexcept
{
    bool received = false;
    while (completed_.try_pop(snapshot))
        received = true;
    return received;
}

}
