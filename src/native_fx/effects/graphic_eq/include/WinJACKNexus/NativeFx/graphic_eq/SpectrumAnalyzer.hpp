/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#include <WinJACKNexus/Common/LockFreeSpscQueue.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace WinJACKNexus::Common
{

inline constexpr std::size_t graphicEqSpectrumFftSize = 4096;
inline constexpr std::size_t graphicEqSpectrumBinCount = 96;
inline constexpr std::size_t graphicEqSpectrumChannelCount = 8;
inline constexpr std::size_t graphicEqSpectrumAllChannels = graphicEqSpectrumChannelCount;

struct GraphicEqSpectrumFrame final
{
    std::array<std::array<float, graphicEqSpectrumFftSize>, graphicEqSpectrumChannelCount>
        inputSamples{};
    std::array<std::array<float, graphicEqSpectrumFftSize>, graphicEqSpectrumChannelCount>
        outputSamples{};
    std::uint32_t sampleRate{48000};
};

struct GraphicEqSpectrumPair final
{
    std::array<float, graphicEqSpectrumBinCount> inputDb{};
    std::array<float, graphicEqSpectrumBinCount> outputDb{};
    std::size_t activeChannelCount{};
};

// Captures post-processed samples on the audio thread and transfers fixed-size
// frames to the UI through a bounded SPSC queue.
class WJN_FX_API GraphicEqSpectrumAnalyzer final
{
  public:
    void setSampleRate(std::uint32_t sampleRate) noexcept;
    void pushSamples(
        const std::array<float, graphicEqSpectrumChannelCount> &inputSamples,
        const std::array<float, graphicEqSpectrumChannelCount> &outputSamples) noexcept;
    void pushChannelInput(std::size_t channel, const float *input, std::size_t frames) noexcept;
    void pushChannelOutput(std::size_t channel, const float *output, std::size_t frames) noexcept;
    bool tryPopLatest(GraphicEqSpectrumFrame &frame) noexcept;

  private:
    void publishFrame() noexcept;
    void publishChannelFrame() noexcept;

    std::array<std::array<float, graphicEqSpectrumFftSize>, graphicEqSpectrumChannelCount>
        inputHistory_{};
    std::array<std::array<float, graphicEqSpectrumFftSize>, graphicEqSpectrumChannelCount>
        outputHistory_{};
    GraphicEqSpectrumFrame pendingFrame_{};
    LockFreeSpscQueue<GraphicEqSpectrumFrame, 3> frames_;
    std::atomic<std::uint32_t> sampleRate_{48000};
    std::size_t writePosition_{};
    std::size_t samplesInHistory_{};
    std::size_t samplesSinceFrame_{};
    std::array<std::size_t, graphicEqSpectrumChannelCount> channelWritePositions_{};
    std::size_t channelSamplesInHistory_{};
    std::size_t channelSamplesSinceFrame_{};
};

WJN_FX_API void calculateGraphicEqSpectrum(const GraphicEqSpectrumFrame &frame,
                                           std::size_t selectedChannel,
                                           GraphicEqSpectrumPair &spectrum,
                                           std::size_t fftSize = graphicEqSpectrumFftSize) noexcept;

}
