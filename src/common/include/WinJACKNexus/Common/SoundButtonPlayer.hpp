/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace WinJACKNexus::Common::SignalSwitch
{
struct SoundClip
{
    // Immutable, interleaved stereo at 48 kHz. Decoded outside the audio callback.
    std::vector<float> samples;
};

class SoundButtonPlayer
{
  public:
    static_assert(std::atomic<SoundClip *>::is_always_lock_free);
    static_assert(std::atomic<uint64_t>::is_always_lock_free);

    void publish(SoundClip *clip) noexcept
    {
        stop();
        clip_.store(clip);
    }
    bool inUse(const SoundClip *clip) const noexcept
    {
        return hazard_.load() == clip;
    }
    void trigger() noexcept
    {
        command_.store(((command_.load() >> 1) + 1) * 2 + 1);
    }
    void stop() noexcept
    {
        command_.store(((command_.load() >> 1) + 1) * 2);
    }
    bool playing() const noexcept
    {
        return playing_.load(std::memory_order_relaxed);
    }
    void render(float *left, float *right, unsigned frames, double sampleRate) noexcept
    {
        if (left)
            std::fill_n(left, frames, 0.0f);
        if (right)
            std::fill_n(right, frames, 0.0f);
        SoundClip *clip;
        do
        {
            clip = clip_.load();
            hazard_.store(clip);
        } while (clip != clip_.load());
        const auto command = command_.load();
        if (command != observed_)
        {
            observed_ = command;
            cursor_ = 0;
            active_ = (command & 1) && clip;
        }
        const auto count = clip ? clip->samples.size() / 2 : 0;
        if (active_ && count && sampleRate > 0 && std::isfinite(sampleRate))
        {
            const double step = 48000.0 / sampleRate;
            for (unsigned frame = 0; frame < frames && cursor_ < double(count); ++frame)
            {
                const auto index = size_t(cursor_);
                const auto next = std::min(index + 1, count - 1);
                const float fraction = float(cursor_ - double(index));
                const auto value = [&](unsigned channel) {
                    const auto first = clip->samples[index * 2 + channel];
                    return first + (clip->samples[next * 2 + channel] - first) * fraction;
                };
                if (left)
                    left[frame] = value(0);
                if (right)
                    right[frame] = value(1);
                cursor_ += step;
            }
            active_ = cursor_ < double(count);
        }
        else
            active_ = false;
        playing_.store(active_, std::memory_order_relaxed);
        hazard_.store(nullptr);
    }

  private:
    // The GUI owns clips and reclaims retired clips only after checking hazard_.
    std::atomic<SoundClip *> clip_{};
    std::atomic<SoundClip *> hazard_{};
    std::atomic<uint64_t> command_{};
    std::atomic_bool playing_{};
    uint64_t observed_{};
    double cursor_{};
    bool active_{};
};
} // namespace WinJACKNexus::Common::SignalSwitch
