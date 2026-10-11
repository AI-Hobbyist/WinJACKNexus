/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <WinJACKNexus/Common/DigitalMixerState.hpp>
#include <WinJACKNexus/Common/Export.hpp>
#include <WinJACKNexus/Common/LockFreeSpscQueue.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace WinJACKNexus::Common
{

struct WINJACK_COMMON_API MixerPolarSnapshot final
{
    std::uint64_t sourceId{};
    std::uint64_t targetId{};
    std::uint64_t topologyVersion{};
    std::uint64_t firstSample{};
    std::uint64_t endSample{};
    std::uint32_t sampleRate{};
    std::uint32_t sampleCount{};
    MixerLayout sourceLayout{MixerLayout::stereo};
    MixerLayout targetLayout{MixerLayout::stereo};
    std::array<double, 8> inputSumSquares{};
    std::array<double, 8> outputSumSquares{};
    std::array<float, 8> inputPeak{};
    std::array<float, 8> outputPeak{};
    bool valid{true};
};

// One audio producer and one UI reader. The producer never waits for the UI;
// a full queue drops analysis windows without affecting the audio path.
class WINJACK_COMMON_API MixerPolarAnalyzer final
{
  public:
    MixerPolarAnalyzer(std::uint64_t sourceId, std::uint64_t targetId, MixerLayout sourceLayout,
                       MixerLayout targetLayout, std::uint64_t topologyVersion,
                       std::uint32_t sampleRate) noexcept;
    void push(std::span<const float *const> input, std::span<const float *const> output,
              std::size_t frames) noexcept;
    bool readLatest(MixerPolarSnapshot &snapshot) noexcept;
    std::uint64_t droppedWindows() const noexcept
    {
        return dropped_.load(std::memory_order_acquire);
    }

  private:
    void finishWindow() noexcept;
    MixerPolarSnapshot accumulating_;
    std::uint32_t windowSamples_{};
    std::uint64_t sampleIndex_{};
    LockFreeSpscQueue<MixerPolarSnapshot, 4> completed_;
    std::atomic<std::uint64_t> dropped_{0};
};

}
