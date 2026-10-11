/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include "Dsp.hpp"
#include <algorithm>
#include <cmath>

namespace WinJACKNexus::Common
{
float DelayProcessor::delayMilliseconds(const DelayParameters &p) noexcept
{
    if (!p.sync)
        return std::clamp(p.timeMs, 1.0F, 2000.0F);
    constexpr float beats[] = {.25F, .5F, 1.0F, 2.0F, 4.0F};
    return 60000.0F / std::clamp(p.bpm, 30.0F, 300.0F) * beats[std::clamp(p.note, 0, 4)] *
           (p.dotted ? 1.5F : 1.0F);
}
void DelayProcessor::setSampleRate(float rate)
{
    sampleRate_ = std::clamp(rate, 8000.0F, 192000.0F);
    ring_.assign(static_cast<std::size_t>(sampleRate_ * 12.1F) + 2, 0.0F);
    reset();
}
void DelayProcessor::reset() noexcept
{
    std::fill(ring_.begin(), ring_.end(), 0.0F);
    write_ = 0;
    highPassState_ = lowPassState_ = previousInput_ = 0;
}
void DelayProcessor::process(const float *input, float *output, unsigned frames,
                             const DelayParameters &p) noexcept
{
    if (!output)
        return;
    if (ring_.empty())
    {
        if (input)
            std::copy_n(input, frames, output);
        else
            std::fill_n(output, frames, 0.0F);
        return;
    }
    const auto size = static_cast<unsigned>(ring_.size());
    const unsigned delay =
        std::clamp(static_cast<unsigned>(delayMilliseconds(p) * sampleRate_ * .001F), 1u, size - 1);
    const unsigned half = std::max(1u, delay / 2);
    const float feedback = std::clamp(p.feedback * .01F, 0.0F, .95F);
    const float wet = std::exp2(p.wetDb / 6.020599913F), dry = std::exp2(p.dryDb / 6.020599913F);
    const float hpCoefficient =
        std::exp(-6.283185307F * std::clamp(p.highPassHz, 20.0F, 2000.0F) / sampleRate_);
    const float lpCoefficient =
        std::exp(-6.283185307F * std::clamp(p.lowPassHz, 500.0F, 20000.0F) / sampleRate_);
    for (unsigned i = 0; i < frames; ++i)
    {
        const float x = input ? input[i] : 0.0F;
        if (!p.enabled)
        {
            output[i] = x;
            ring_[write_] = 0;
            write_ = (write_ + 1) % size;
            continue;
        }
        const auto read = (write_ + size - delay) % size;
        const float echo = ring_[read];
        const float second = p.mixMode == 1 ? ring_[(write_ + size - half) % size] * .35F : 0.0F;
        float fed = echo;
        if (p.highPass)
        {
            highPassState_ = hpCoefficient * (highPassState_ + fed - previousInput_);
            previousInput_ = fed;
            fed = highPassState_;
        }
        if (p.lowPass)
        {
            lowPassState_ = (1 - lpCoefficient) * fed + lpCoefficient * lowPassState_;
            fed = lowPassState_;
        }
        ring_[write_] = std::clamp(x + fed * feedback, -16.0F, 16.0F);
        output[i] = x * dry + (echo + second) * wet;
        write_ = (write_ + 1) % size;
    }
}
}
