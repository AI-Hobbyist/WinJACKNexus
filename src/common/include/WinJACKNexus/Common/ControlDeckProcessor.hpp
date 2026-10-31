/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <span>

namespace WinJACKNexus::Common::ControlDeck
{
struct Gate
{
    std::atomic<std::uint64_t> command{0};
    std::atomic<std::uint64_t> acknowledged{0};
    std::atomic<bool> running{false};

    void request(bool enabled) noexcept
    {
        const auto previous = command.load(std::memory_order_relaxed);
        if (bool(previous & 1) != enabled)
            command.store(((previous + 2) & ~std::uint64_t(1)) | std::uint64_t(enabled), std::memory_order_release);
    }
    bool quiet() const noexcept
    {
        const auto requested = command.load(std::memory_order_acquire);
        return !(requested & 1) &&
               (!running.load(std::memory_order_acquire) || acknowledged.load(std::memory_order_acquire) == requested);
    }
};

inline void audio(bool enabled, const float *const *inputs, float *const *outputs, unsigned channels,
                  unsigned frames) noexcept
{
    for (unsigned channel = 0; channel < channels; ++channel)
    {
        auto *output = outputs[channel];
        if (!output)
            continue;
        if (enabled && inputs[channel])
            std::copy_n(inputs[channel], frames, output);
        else
            std::fill_n(output, frames, 0.0f);
    }
}

inline void audioGain(float gain, const float *const *inputs, float *const *outputs, unsigned channels,
                      unsigned frames) noexcept
{
    for (unsigned channel = 0; channel < channels; ++channel)
        if (outputs[channel])
            for (unsigned frame = 0; frame < frames; ++frame)
                outputs[channel][frame] = inputs[channel] ? inputs[channel][frame] * gain : 0;
}

inline void capturePeak(const float *input, unsigned frames, std::atomic<float> &target) noexcept
{
    float peak{};
    if (input)
        for (unsigned frame = 0; frame < frames; ++frame)
            peak = std::max(peak, std::abs(input[frame]));
    auto previous = target.load(std::memory_order_relaxed);
    while (peak > previous && !target.compare_exchange_weak(previous, peak, std::memory_order_relaxed))
    {
    }
}

struct Event
{
    unsigned frame{};
    std::span<const std::uint8_t> bytes;
};

class Midi
{
  public:
    void reset() noexcept
    {
        notes_ = {};
        sustain_ = {};
        releasing_ = false;
    }

    // The caller supplies bounded storage through emit. Failed cleanup remains pending.
    template <class Read, class Emit>
    bool process(bool enabled, unsigned frames, unsigned count, Read read, Emit writeEvent) noexcept
    {
        if (!enabled)
            releasing_ = true;
        if (releasing_)
        {
            if (!frames || !release(writeEvent))
                return false;
            releasing_ = false;
        }
        if (!enabled)
            return true;
        for (unsigned index = 0; index < count; ++index)
        {
            const auto event = read(index);
            if (event.frame >= frames || event.bytes.empty() || !writeEvent(event))
                continue;
            track(event.bytes);
        }
        return false;
    }

  private:
    std::array<std::array<std::uint32_t, 128>, 16> notes_{};
    std::array<bool, 16> sustain_{};
    bool releasing_{};

    void track(std::span<const std::uint8_t> bytes) noexcept
    {
        if (bytes.size() < 3 || bytes[1] >= 128 || bytes[2] >= 128)
            return;
        const auto channel = bytes[0] & 15;
        const auto type = bytes[0] & 0xf0;
        auto &count = notes_[channel][bytes[1]];
        if (type == 0x90 && bytes[2])
            ++count;
        else if ((type == 0x80 || (type == 0x90 && !bytes[2])) && count)
            --count;
        else if (type == 0xb0 && bytes[1] == 64)
            sustain_[channel] = bytes[2] >= 64;
        else if (type == 0xb0 && (bytes[1] == 120 || bytes[1] == 123))
            notes_[channel] = {};
        else if (type == 0xb0 && bytes[1] == 121)
            sustain_[channel] = false;
    }

    template <class Emit> bool release(Emit writeEvent) noexcept
    {
        for (unsigned channel = 0; channel < 16; ++channel)
        {
            if (sustain_[channel])
            {
                const std::uint8_t bytes[]{std::uint8_t(0xb0 | channel), 64, 0};
                if (!writeEvent(Event{0, bytes}))
                    return false;
                sustain_[channel] = false;
            }
            for (unsigned note = 0; note < 128; ++note)
            {
                auto &count = notes_[channel][note];
                while (count)
                {
                    const std::uint8_t bytes[]{std::uint8_t(0x80 | channel), std::uint8_t(note), 0};
                    if (!writeEvent(Event{0, bytes}))
                        return false;
                    --count;
                }
            }
        }
        return true;
    }
};
} // namespace WinJACKNexus::Common::ControlDeck
