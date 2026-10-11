/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include "Dsp.hpp"
#include <algorithm>
#include <cmath>

namespace WinJACKNexus::Common
{
namespace
{
constexpr float pi = 3.14159265358979323846F;
float gain(float db) noexcept
{
    return std::pow(10.0F, db / 20.0F);
}
}
void ReverbProcessor::setSampleRate(float rate)
{
    rate_ = std::clamp(rate, 8000.0F, 192000.0F);
    constexpr float combSeconds[] = {.0297F, .0371F, .0411F, .0437F};
    constexpr float diffuseSeconds[] = {.0050F, .0017F};
    for (std::size_t i = 0; i < combs_.size(); ++i)
        combs_[i].data.assign(std::max(1u, unsigned(rate_ * combSeconds[i] * 1.8F)), 0);
    for (std::size_t i = 0; i < diffusers_.size(); ++i)
        diffusers_[i].data.assign(std::max(1u, unsigned(rate_ * diffuseSeconds[i])), 0);
    preDelay_.assign(std::max(1u, unsigned(rate_ * .2F) + 1), 0);
    reset();
}
void ReverbProcessor::reset() noexcept
{
    for (auto &line : combs_)
    {
        std::fill(line.data.begin(), line.data.end(), 0.0F);
        line.at = 0;
        line.filter = 0;
    }
    for (auto &line : diffusers_)
    {
        std::fill(line.data.begin(), line.data.end(), 0.0F);
        line.at = 0;
        line.filter = 0;
    }
    std::fill(preDelay_.begin(), preDelay_.end(), 0.0F);
    preAt_ = 0;
    lowState_ = highState_ = 0;
}
void ReverbProcessor::process(const float *input, float *output, unsigned frames,
                              const ReverbParameters &p) noexcept
{
    if (!output)
        return;
    if (preDelay_.empty())
    {
        for (unsigned i = 0; i < frames; ++i)
            output[i] = input ? input[i] : 0;
        return;
    }
    const float size = std::clamp(p.size, 0.0F, 100.0F);
    constexpr float sceneScale[] = {.75F, 1.0F, .7F, 1.35F, .55F, .8F, 1.0F};
    const float geometry = sceneScale[std::clamp(p.scene, 0, 6)];
    const float decay = std::clamp(p.decay, .1F, 15.0F);
    const float wet = std::clamp(p.wet, 0.0F, 100.0F) * .01F;
    const float dry = gain(std::clamp(p.dryDb, -60.0F, 6.0F));
    const float outputGain = gain(std::clamp(p.outputDb, -24.0F, 12.0F));
    const float early = gain(std::clamp(p.earlyDb, -60.0F, 6.0F));
    const float highGain = gain(std::clamp(p.highDampDb, -24.0F, 0.0F));
    const float lowGain = gain(std::clamp(p.lowDampDb, -24.0F, 0.0F));
    const float highAlpha =
        1 - std::exp(-2 * pi * std::clamp(p.highFreqHz, 500.0F, 16000.0F) / rate_);
    const float lowAlpha = 1 - std::exp(-2 * pi * std::clamp(p.lowFreqHz, 20.0F, 2000.0F) / rate_);
    const unsigned preSamples =
        std::clamp(unsigned(std::clamp(p.preDelayMs, 0.0F, 150.0F) * rate_ * .001F), 0u,
                   unsigned(preDelay_.size() - 1));
    const unsigned earlySamples =
        std::clamp(unsigned(std::clamp(p.earlyTimeMs, 1.0F, 150.0F) * rate_ * .001F), 1u,
                   unsigned(preDelay_.size() - 1));
    const float diffuse = std::clamp(p.diffusion, 0.0F, 100.0F) * .0068F;
    for (unsigned n = 0; n < frames; ++n)
    {
        const float x = input ? input[n] : 0;
        const float delayed =
            preSamples ? preDelay_[(preAt_ + preDelay_.size() - preSamples) % preDelay_.size()] : x;
        const float first =
            preDelay_[(preAt_ + preDelay_.size() - earlySamples) % preDelay_.size()];
        preDelay_[preAt_] = x;
        preAt_ = (preAt_ + 1) % preDelay_.size();
        float sum = 0;
        for (std::size_t i = 0; i < combs_.size(); ++i)
        {
            auto &line = combs_[i];
            const unsigned length = std::max(
                1u, std::min(unsigned(line.data.size()),
                             unsigned(line.data.size() * (.55F + .45F * size * .01F) * geometry)));
            const unsigned read = (line.at + line.data.size() - length) % line.data.size();
            const float tap = line.data[read];
            line.filter += highAlpha * (tap - line.filter);
            const float colored = line.filter + (tap - line.filter) * highGain;
            const float feedback = std::pow(.001F, float(length) / (rate_ * decay));
            line.data[line.at] = std::clamp(delayed + colored * feedback, -8.0F, 8.0F);
            line.at = (line.at + 1) % line.data.size();
            sum += tap * .25F;
        }
        for (auto &line : diffusers_)
        {
            const float tap = line.data[line.at];
            const float next = sum - tap * diffuse;
            line.data[line.at] = next;
            line.at = (line.at + 1) % line.data.size();
            sum = tap + next * diffuse;
        }
        lowState_ += lowAlpha * (sum - lowState_);
        sum = (sum - lowState_) + lowState_ * lowGain;
        const float reverberated = first * early * .25F + sum;
        output[n] = p.enabled ? (x * dry + reverberated * wet) * outputGain : x;
    }
}
}
