/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/NativeFx/graphic_eq/SpectrumAnalyzer.hpp>

#include <algorithm>
#include <cmath>
#include <complex>

namespace WinJACKNexus::Common {
namespace {

constexpr float pi = 3.14159265358979323846F;
constexpr float minimumSpectrumDb = -96.0F;

}

void GraphicEqSpectrumAnalyzer::setSampleRate(std::uint32_t sampleRate) noexcept
{
    sampleRate_.store(std::clamp(sampleRate, 1000U, 384000U), std::memory_order_relaxed);
}

void GraphicEqSpectrumAnalyzer::pushSamples(
    const std::array<float, graphicEqSpectrumChannelCount>& inputSamples,
    const std::array<float, graphicEqSpectrumChannelCount>& outputSamples) noexcept
{
    for (std::size_t channel = 0; channel < graphicEqSpectrumChannelCount; ++channel) {
        inputHistory_[channel][writePosition_] = std::isfinite(inputSamples[channel]) ? inputSamples[channel] : 0.0F;
        outputHistory_[channel][writePosition_] = std::isfinite(outputSamples[channel]) ? outputSamples[channel] : 0.0F;
    }
    writePosition_ = (writePosition_ + 1) % graphicEqSpectrumFftSize;

    if (samplesInHistory_ < graphicEqSpectrumFftSize) {
        ++samplesInHistory_;
        if (samplesInHistory_ == graphicEqSpectrumFftSize)
            publishFrame();
        return;
    }

    if (++samplesSinceFrame_ >= graphicEqSpectrumFftSize / 2) {
        samplesSinceFrame_ = 0;
        publishFrame();
    }
}

void GraphicEqSpectrumAnalyzer::pushChannelInput(std::size_t channel,
    const float* input, std::size_t frames) noexcept
{
    if (channel >= graphicEqSpectrumChannelCount || !input) return;
    auto position = channelWritePositions_[channel];
    for (std::size_t frame = 0; frame < frames; ++frame) {
        inputHistory_[channel][position] = std::isfinite(input[frame]) ? input[frame] : 0.0F;
        position = (position + 1) % graphicEqSpectrumFftSize;
    }
}

void GraphicEqSpectrumAnalyzer::pushChannelOutput(std::size_t channel,
    const float* output, std::size_t frames) noexcept
{
    if (channel >= graphicEqSpectrumChannelCount || !output) return;
    auto& position = channelWritePositions_[channel];
    for (std::size_t frame = 0; frame < frames; ++frame) {
        outputHistory_[channel][position] = std::isfinite(output[frame]) ? output[frame] : 0.0F;
        position = (position + 1) % graphicEqSpectrumFftSize;
    }
    // Channel zero clocks publication once per audio block. The other channels
    // retain their own positions and contribute their latest completed block.
    if (channel != 0) return;
    const auto previous = channelSamplesInHistory_;
    channelSamplesInHistory_ = std::min(graphicEqSpectrumFftSize, previous + frames);
    if (previous < graphicEqSpectrumFftSize
        && channelSamplesInHistory_ == graphicEqSpectrumFftSize) {
        publishChannelFrame();
        channelSamplesSinceFrame_ = 0;
    } else if (channelSamplesInHistory_ == graphicEqSpectrumFftSize) {
        channelSamplesSinceFrame_ += frames;
        if (channelSamplesSinceFrame_ >= graphicEqSpectrumFftSize / 2) {
            channelSamplesSinceFrame_ = 0;
            publishChannelFrame();
        }
    }
}

void GraphicEqSpectrumAnalyzer::publishChannelFrame() noexcept
{
    for (std::size_t channel = 0; channel < graphicEqSpectrumChannelCount; ++channel)
        for (std::size_t i = 0; i < graphicEqSpectrumFftSize; ++i) {
            const auto source = (channelWritePositions_[channel] + i) % graphicEqSpectrumFftSize;
            pendingFrame_.inputSamples[channel][i] = inputHistory_[channel][source];
            pendingFrame_.outputSamples[channel][i] = outputHistory_[channel][source];
        }
    pendingFrame_.sampleRate = sampleRate_.load(std::memory_order_relaxed);
    frames_.try_push(pendingFrame_);
}

void GraphicEqSpectrumAnalyzer::publishFrame() noexcept
{
    for (std::size_t channel = 0; channel < graphicEqSpectrumChannelCount; ++channel) {
        for (std::size_t i = 0; i < graphicEqSpectrumFftSize; ++i) {
            const auto source = (writePosition_ + i) % graphicEqSpectrumFftSize;
            pendingFrame_.inputSamples[channel][i] = inputHistory_[channel][source];
            pendingFrame_.outputSamples[channel][i] = outputHistory_[channel][source];
        }
    }
    pendingFrame_.sampleRate = sampleRate_.load(std::memory_order_relaxed);
    frames_.try_push(pendingFrame_);
}

bool GraphicEqSpectrumAnalyzer::tryPopLatest(GraphicEqSpectrumFrame& frame) noexcept
{
    bool received = false;
    while (frames_.try_pop(frame))
        received = true;
    return received;
}

namespace {

void calculateMagnitudes(
    const std::array<float, graphicEqSpectrumFftSize>& samples,
    std::uint32_t sampleRate,
    std::array<float, graphicEqSpectrumBinCount>& magnitudes,
    std::size_t fftSize) noexcept
{
    using Complex = std::complex<float>;
    std::array<Complex, graphicEqSpectrumFftSize> fft {};
    float windowSum = 0.0F;
    const auto offset = graphicEqSpectrumFftSize - fftSize;
    for (std::size_t i = 0; i < fftSize; ++i) {
        const auto window = 0.5F - 0.5F * std::cos(2.0F * pi * static_cast<float>(i)
            / static_cast<float>(fftSize - 1));
        fft[i] = Complex(samples[offset + i] * window, 0.0F);
        windowSum += window;
    }

    for (std::size_t i = 1, reversed = 0; i < fftSize; ++i) {
        std::size_t bit = fftSize >> 1;
        for (; reversed & bit; bit >>= 1)
            reversed ^= bit;
        reversed ^= bit;
        if (i < reversed)
            std::swap(fft[i], fft[reversed]);
    }

    for (std::size_t length = 2; length <= fftSize; length <<= 1) {
        const auto angle = -2.0F * pi / static_cast<float>(length);
        const Complex root(std::cos(angle), std::sin(angle));
        for (std::size_t start = 0; start < fftSize; start += length) {
            Complex rotation(1.0F, 0.0F);
            for (std::size_t offset = 0; offset < length / 2; ++offset) {
                const auto even = fft[start + offset];
                const auto odd = fft[start + offset + length / 2] * rotation;
                fft[start + offset] = even + odd;
                fft[start + offset + length / 2] = even - odd;
                rotation *= root;
            }
        }
    }

    sampleRate = std::max(sampleRate, 1000U);
    const auto nyquist = static_cast<float>(sampleRate) * 0.5F;
    for (std::size_t i = 0; i < graphicEqSpectrumBinCount; ++i) {
        const auto lowerFrequency = 20.0F * std::pow(1000.0F,
            static_cast<float>(i) / static_cast<float>(graphicEqSpectrumBinCount));
        const auto upperFrequency = 20.0F * std::pow(1000.0F,
            static_cast<float>(i + 1) / static_cast<float>(graphicEqSpectrumBinCount));
        if (lowerFrequency >= nyquist || windowSum <= 0.0F) {
            magnitudes[i] = 0.0F;
            continue;
        }

        const auto binScale = static_cast<float>(fftSize)
            / static_cast<float>(sampleRate);
        const auto lower = std::max<std::size_t>(1, static_cast<std::size_t>(
            std::floor(lowerFrequency * binScale)));
        const auto upper = std::min(fftSize / 2 - 1, static_cast<std::size_t>(
            std::ceil(std::min(upperFrequency, nyquist) * binScale)));
        float magnitude = 0.0F;
        for (auto bin = lower; bin <= upper; ++bin)
            magnitude = std::max(magnitude, std::abs(fft[bin]));
        magnitude *= 2.0F / windowSum;
        magnitudes[i] = magnitude;
    }
}

float inputRms(const std::array<float, graphicEqSpectrumFftSize>& samples) noexcept
{
    double power = 0.0;
    for (const auto sample : samples)
        power += static_cast<double>(sample) * sample;
    return static_cast<float>(std::sqrt(power / static_cast<double>(graphicEqSpectrumFftSize)));
}

void powersToDecibels(
    const std::array<float, graphicEqSpectrumBinCount>& powers,
    std::size_t channelCount,
    std::array<float, graphicEqSpectrumBinCount>& decibels) noexcept
{
    for (std::size_t i = 0; i < graphicEqSpectrumBinCount; ++i) {
        if (channelCount == 0) {
            decibels[i] = minimumSpectrumDb;
            continue;
        }
        const auto magnitude = std::sqrt(powers[i] / static_cast<float>(channelCount));
        const auto level = 20.0F * std::log10(std::max(magnitude, 1.0e-6F));
        decibels[i] = std::clamp(level, minimumSpectrumDb, 6.0F);
    }
}

}

void calculateGraphicEqSpectrum(
    const GraphicEqSpectrumFrame& frame,
    std::size_t selectedChannel,
    GraphicEqSpectrumPair& spectrum,
    std::size_t fftSize) noexcept
{
    if (fftSize != 1024 && fftSize != 2048 && fftSize != graphicEqSpectrumFftSize)
        fftSize = graphicEqSpectrumFftSize;
    constexpr float activeChannelRmsThreshold = 1.0e-5F;
    spectrum.activeChannelCount = 0;
    const bool allChannels = selectedChannel >= graphicEqSpectrumChannelCount;
    std::array<bool, graphicEqSpectrumChannelCount> included {};
    for (std::size_t channel = 0; channel < graphicEqSpectrumChannelCount; ++channel) {
        if (!allChannels) {
            included[channel] = channel == selectedChannel;
        } else {
            included[channel] = inputRms(frame.inputSamples[channel]) >= activeChannelRmsThreshold;
        }
        if (included[channel])
            ++spectrum.activeChannelCount;
    }

    std::array<float, graphicEqSpectrumBinCount> inputPowers {};
    std::array<float, graphicEqSpectrumBinCount> outputPowers {};
    std::array<float, graphicEqSpectrumBinCount> magnitudes {};
    for (std::size_t channel = 0; channel < graphicEqSpectrumChannelCount; ++channel) {
        if (!included[channel])
            continue;
        calculateMagnitudes(frame.inputSamples[channel], frame.sampleRate, magnitudes, fftSize);
        for (std::size_t bin = 0; bin < graphicEqSpectrumBinCount; ++bin)
            inputPowers[bin] += magnitudes[bin] * magnitudes[bin];
        calculateMagnitudes(frame.outputSamples[channel], frame.sampleRate, magnitudes, fftSize);
        for (std::size_t bin = 0; bin < graphicEqSpectrumBinCount; ++bin)
            outputPowers[bin] += magnitudes[bin] * magnitudes[bin];
    }

    powersToDecibels(inputPowers, spectrum.activeChannelCount, spectrum.inputDb);
    powersToDecibels(outputPowers, spectrum.activeChannelCount, spectrum.outputDb);
}

}
