// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace WinJACKNexus::Common::Broadcast
{

enum class Layout : unsigned
{
    mono = 1,
    stereo = 2,
    surround51 = 6,
    surround71 = 8
};

constexpr unsigned channelCount(Layout layout) noexcept
{
    return static_cast<unsigned>(layout);
}

// Prepared on the control thread, copied as one snapshot into audio processing.
// Network identity, strings and ownership stay outside the realtime callback.
struct Controls
{
    Layout layout = Layout::stereo;
    float gain = 1;
    bool mute = false;
    bool listen = false;
    bool solo = false;
};

struct GainSmoothing
{
    double value = 1;
    double coefficient = 1;
};

// Planar JACK input/monitor and interleaved publishing use the canonical project
// order. The caller supplies all storage and computes soloActive for this node
// instance only. This function never allocates, locks, or accesses network/UI.
inline float process(const Controls &controls, bool soloActive, const float *const *inputs,
                     float *const *monitor, float *broadcast, std::uint32_t frames,
                     GainSmoothing *smoothing = nullptr) noexcept
{
    const auto channels = channelCount(controls.layout);
    const bool monitoring = controls.listen && (!soloActive || controls.solo);
    float peak = 0;
    for (std::uint32_t frame = 0; frame < frames; ++frame)
    {
        if (smoothing)
            smoothing->value += (controls.gain - smoothing->value) * smoothing->coefficient;
        const float gain = smoothing ? static_cast<float>(smoothing->value) : controls.gain;
        for (unsigned channel = 0; channel < channels; ++channel)
        {
            const float input = inputs && inputs[channel] ? inputs[channel][frame] : 0;
            const float value = controls.mute || !std::isfinite(input) ? 0 : input * gain;
            peak = std::max(peak, std::abs(value));
            if (broadcast)
                broadcast[frame * channels + channel] = value;
            if (monitor && monitor[channel])
                monitor[channel][frame] = monitoring ? value : 0;
        }
    }
    return peak;
}

} // namespace WinJACKNexus::Common::Broadcast
