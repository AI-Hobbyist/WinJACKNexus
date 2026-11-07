// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <WinJACKNexus/Common/AudioRingBuffer.hpp>
#include <WinJACKNexus/Common/BroadcastProcessor.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <span>
#include <stdexcept>

namespace WinJACKNexus::Common::Broadcast
{
struct QueuedFrame
{
    std::array<float, 8> samples{};
    std::uint64_t position = 0;
    std::uint64_t createdNanoseconds = 0;
    std::uint64_t generation = 0;
};

// Exactly one audio producer and one publishing-worker consumer. Allocation and
// preparation happen before processing; all render scratch is owned in advance.
class AudioStream final
{
  public:
    AudioStream(std::uint32_t rate, const Controls &initial)
        : rate_(checkedRate(rate)), layout_(initial.layout), queue_(rate_ / 10),
          smoothing_{initial.gain, 1 - std::exp(-1.0 / (rate_ * 0.005))}
    {
        const auto channels = channelCount(layout_);
        if (channels != 1 && channels != 2 && channels != 6 && channels != 8)
            throw std::invalid_argument("Unsupported broadcast audio layout");
    }

    // Consumer only. A generation tag invalidates any old chunk that the
    // producer had already begun when publishing was stopped/restarted.
    std::uint64_t arm() noexcept
    {
        queue_.clear();
        const auto generation = ++nextGeneration_;
        generation_.store(generation, std::memory_order_release);
        return generation;
    }

    void disarm() noexcept
    {
        generation_.store(0, std::memory_order_release);
        queue_.clear();
    }

    std::size_t read(std::span<QueuedFrame> output) noexcept
    {
        return queue_.read(output.data(), output.size());
    }

    float render(const Controls &controls, bool soloActive, const float *const *inputs, float *const *monitor,
                 std::uint32_t frames) noexcept
    {
        const auto channels = channelCount(layout_);
        const auto created = nowNanoseconds();
        float peak = 0;
        for (std::uint32_t offset = 0; offset < frames;)
        {
            const auto count = std::min<std::uint32_t>(frames - offset, scratchFrames);
            std::array<const float *, 8> input{};
            std::array<float *, 8> output{};
            for (unsigned channel = 0; channel < channels; ++channel)
            {
                input[channel] = inputs && inputs[channel] ? inputs[channel] + offset : nullptr;
                output[channel] = monitor && monitor[channel] ? monitor[channel] + offset : nullptr;
            }
            peak = std::max(peak, process(controls, soloActive, input.data(), output.data(),
                                          broadcast_.data(), count, &smoothing_));
            const auto generation = generation_.load(std::memory_order_acquire);
            if (generation)
            {
                if (queue_.freeSpace() < count)
                    droppedFrames_.fetch_add(count, std::memory_order_relaxed);
                else
                {
                    for (unsigned frame = 0; frame < count; ++frame)
                    {
                        auto &queued = queued_[frame];
                        std::copy_n(broadcast_.data() + frame * channels, channels, queued.samples.begin());
                        queued.position = position_ + offset + frame;
                        queued.createdNanoseconds = created;
                        queued.generation = generation;
                    }
                    queue_.write(queued_.data(), count);
                }
            }
            offset += count;
        }
        position_ += frames;
        return peak;
    }

    std::uint32_t rate() const noexcept
    {
        return rate_;
    }
    Layout layout() const noexcept
    {
        return layout_;
    }
    std::uint64_t droppedFrames() const noexcept
    {
        return droppedFrames_.load(std::memory_order_relaxed);
    }
    static std::uint64_t nowNanoseconds() noexcept
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                              std::chrono::steady_clock::now().time_since_epoch())
                                              .count());
    }

  private:
    static std::uint32_t checkedRate(std::uint32_t rate)
    {
        if (rate < 8000 || rate > 192000)
            throw std::invalid_argument("Unsupported broadcast source rate");
        return rate;
    }
    static constexpr unsigned scratchFrames = 256;
    const std::uint32_t rate_;
    const Layout layout_;
    AudioRingBuffer<QueuedFrame> queue_;
    GainSmoothing smoothing_;
    std::array<float, scratchFrames * 8> broadcast_{};
    std::array<QueuedFrame, scratchFrames> queued_{};
    std::uint64_t position_ = 0, nextGeneration_ = 0;
    std::atomic<std::uint64_t> generation_{0}, droppedFrames_{0};
};
} // namespace WinJACKNexus::Common::Broadcast
