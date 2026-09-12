/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <WinJACKNexus/Common/LoudnessEngine.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>

namespace WinJACKNexus::Common {

namespace {

constexpr double loudnessOffset = -0.691;
constexpr double absoluteGateLufs = -70.0;
constexpr double integratedRelativeGateLu = 10.0;
constexpr double lraRelativeGateLu = 20.0;
constexpr double histogramMaximumLufs = 24.0;
constexpr double histogramResolutionLu = 0.01;
constexpr std::size_t histogramBinCount = static_cast<std::size_t>(
    (histogramMaximumLufs - absoluteGateLufs) / histogramResolutionLu) + 1;
constexpr std::size_t momentaryBlockCount = 4;
constexpr std::size_t shortTermBlockCount = 30;
constexpr std::size_t truePeakTapCount = 33;

double energyForLoudness(double loudness) noexcept
{
    return std::pow(10.0, (loudness - loudnessOffset) / 10.0);
}

double loudnessForEnergy(double energy) noexcept
{
    return energy > 0.0 ? loudnessOffset + 10.0 * std::log10(energy)
                        : -std::numeric_limits<double>::infinity();
}

MeterValue decibelsForAmplitude(double amplitude) noexcept
{
    return MeterValue { amplitude > 0.0 ? 20.0 * std::log10(amplitude)
                                         : -std::numeric_limits<double>::infinity(),
                        true };
}

MeterValue loudnessValue(double energy) noexcept
{
    return MeterValue { loudnessForEnergy(energy), true };
}

struct BiquadState final {
    double firstDelay {};
    double secondDelay {};

    void reset() noexcept
    {
        firstDelay = 0.0;
        secondDelay = 0.0;
    }

    double process(double input, const BiquadCoefficients& coefficients) noexcept
    {
        const auto output = coefficients.b0 * input + firstDelay;
        firstDelay = coefficients.b1 * input - coefficients.a1 * output + secondDelay;
        secondDelay = coefficients.b2 * input - coefficients.a2 * output;
        return output;
    }
};

class KWeightingFilter final {
public:
    void configure(const KWeightingCoefficients& coefficients) noexcept
    {
        coefficients_ = coefficients;
        reset();
    }

    void reset() noexcept
    {
        for (auto& state : shelfStates_)
            state.reset();
        for (auto& state : highPassStates_)
            state.reset();
    }

    double process(std::size_t channel, double input) noexcept
    {
        const auto shelfOutput = shelfStates_[channel].process(input, coefficients_.shelf);
        return highPassStates_[channel].process(shelfOutput, coefficients_.highPass);
    }

private:
    KWeightingCoefficients coefficients_ {};
    std::array<BiquadState, MeterChannelLayout::maximumChannelCount> shelfStates_ {};
    std::array<BiquadState, MeterChannelLayout::maximumChannelCount> highPassStates_ {};
};

class TruePeakEstimator final {
public:
    void configure(double sampleRate, bool enabled) noexcept
    {
        enabled_ = enabled;
        oversamplingFactor_ = sampleRate <= 48000.0 ? 4 : sampleRate <= 96000.0 ? 2 : 1;
        configureCoefficients();
        reset();
    }

    void reset() noexcept
    {
        for (auto& channelHistory : histories_)
            channelHistory.fill(0.0);
        writeIndices_.fill(0);
    }

    double process(std::size_t channel, double input) noexcept
    {
        auto peak = std::abs(input);
        if (!enabled_ || oversamplingFactor_ == 1)
            return peak;

        const auto writeIndex = writeIndices_[channel];
        histories_[channel][writeIndex] = input;
        writeIndices_[channel] = (writeIndex + 1) % truePeakTapCount;

        for (std::size_t phase = 1; phase < oversamplingFactor_; ++phase) {
            double interpolated {};
            for (std::size_t tap = 0; tap < truePeakTapCount; ++tap) {
                const auto historyIndex = (writeIndex + truePeakTapCount - tap) % truePeakTapCount;
                interpolated += coefficients_[phase][tap] * histories_[channel][historyIndex];
            }
            peak = std::max(peak, std::abs(interpolated));
        }
        return peak;
    }

private:
    void configureCoefficients() noexcept
    {
        for (auto& phase : coefficients_)
            phase.fill(0.0);

        const auto delay = static_cast<double>(truePeakTapCount - 1) / 2.0;
        for (std::size_t phase = 1; phase < oversamplingFactor_; ++phase) {
            double coefficientSum {};
            for (std::size_t tap = 0; tap < truePeakTapCount; ++tap) {
                const auto position = delay - static_cast<double>(tap)
                    - static_cast<double>(phase) / static_cast<double>(oversamplingFactor_);
                const auto sinc = std::abs(position) < 1.0e-12
                    ? 1.0
                    : std::sin(std::numbers::pi_v<double> * position)
                        / (std::numbers::pi_v<double> * position);
                const auto normalizedTap = static_cast<double>(tap)
                    / static_cast<double>(truePeakTapCount - 1);
                const auto window = 0.42
                    - 0.5 * std::cos(2.0 * std::numbers::pi_v<double> * normalizedTap)
                    + 0.08 * std::cos(4.0 * std::numbers::pi_v<double> * normalizedTap);
                coefficients_[phase][tap] = sinc * window;
                coefficientSum += coefficients_[phase][tap];
            }
            if (std::abs(coefficientSum) > 1.0e-12) {
                for (auto& coefficient : coefficients_[phase])
                    coefficient /= coefficientSum;
            }
        }
    }

    bool enabled_ {};
    std::size_t oversamplingFactor_ { 1 };
    std::array<std::array<double, truePeakTapCount>, 4> coefficients_ {};
    std::array<std::array<double, truePeakTapCount>, MeterChannelLayout::maximumChannelCount> histories_ {};
    std::array<std::size_t, MeterChannelLayout::maximumChannelCount> writeIndices_ {};
};

struct HistogramBin final {
    std::uint64_t count {};
    double energySum {};
};

struct HistogramStatistics final {
    std::uint64_t count {};
    double energySum {};
};

class LoudnessHistogram final {
public:
    void reset() noexcept
    {
        bins_.fill({});
    }

    void add(double energy) noexcept
    {
        if (!std::isfinite(energy) || energy < energyForLoudness(absoluteGateLufs))
            return;

        const auto loudness = std::clamp(loudnessForEnergy(energy), absoluteGateLufs, histogramMaximumLufs);
        const auto index = static_cast<std::size_t>(std::llround(
            (loudness - absoluteGateLufs) / histogramResolutionLu));
        auto& bin = bins_[std::min(index, bins_.size() - 1)];
        ++bin.count;
        bin.energySum += energy;
    }

    HistogramStatistics statisticsAtOrAbove(double thresholdLufs) const noexcept
    {
        HistogramStatistics statistics;
        const auto firstIndex = indexAtOrAbove(thresholdLufs);
        for (std::size_t index = firstIndex; index < bins_.size(); ++index) {
            statistics.count += bins_[index].count;
            statistics.energySum += bins_[index].energySum;
        }
        return statistics;
    }

    double percentileAtOrAbove(double thresholdLufs, double percentile) const noexcept
    {
        const auto statistics = statisticsAtOrAbove(thresholdLufs);
        if (statistics.count == 0)
            return -std::numeric_limits<double>::infinity();

        const auto clampedPercentile = std::clamp(percentile, 0.0, 1.0);
        const auto rank = std::max<std::uint64_t>(
            1,
            static_cast<std::uint64_t>(std::ceil(clampedPercentile * statistics.count)));
        std::uint64_t accumulated {};
        const auto firstIndex = indexAtOrAbove(thresholdLufs);
        for (std::size_t index = firstIndex; index < bins_.size(); ++index) {
            accumulated += bins_[index].count;
            if (accumulated >= rank)
                return absoluteGateLufs + static_cast<double>(index) * histogramResolutionLu;
        }
        return histogramMaximumLufs;
    }

private:
    static std::size_t indexAtOrAbove(double thresholdLufs) noexcept
    {
        if (thresholdLufs <= absoluteGateLufs)
            return 0;
        if (thresholdLufs >= histogramMaximumLufs)
            return histogramBinCount - 1;
        return static_cast<std::size_t>(std::ceil(
            (thresholdLufs - absoluteGateLufs) / histogramResolutionLu));
    }

    std::array<HistogramBin, histogramBinCount> bins_ {};
};

struct EnergyBlock final {
    double weightedEnergySum {};
    double rawEnergySum {};
    std::size_t frames {};
};

struct EnergyWindow final {
    double weightedEnergy {};
    double rawEnergy {};
    std::size_t frames {};
};

}

KWeightingCoefficients kWeightingCoefficients(double sampleRate) noexcept
{
    if (!std::isfinite(sampleRate) || sampleRate <= 2.0 * 1681.974450955533)
        return {};

    const auto makeHighShelf = [sampleRate] {
        constexpr double frequency = 1681.974450955533;
        constexpr double gainDb = 3.999843853973347;
        constexpr double quality = 0.7071752369554196;
        constexpr double shelfExponent = 0.4996667741545416;
        const auto tangent = std::tan(std::numbers::pi_v<double> * frequency / sampleRate);
        const auto highGain = std::pow(10.0, gainDb / 20.0);
        const auto midGain = std::pow(highGain, shelfExponent);
        const auto denominator = 1.0 + tangent / quality + tangent * tangent;
        return BiquadCoefficients {
            (highGain + midGain * tangent / quality + tangent * tangent) / denominator,
            2.0 * (tangent * tangent - highGain) / denominator,
            (highGain - midGain * tangent / quality + tangent * tangent) / denominator,
            2.0 * (tangent * tangent - 1.0) / denominator,
            (1.0 - tangent / quality + tangent * tangent) / denominator
        };
    };
    const auto makeHighPass = [sampleRate] {
        constexpr double frequency = 38.13547087602444;
        constexpr double quality = 0.5003270373238773;
        const auto tangent = std::tan(std::numbers::pi_v<double> * frequency / sampleRate);
        const auto denominator = 1.0 + tangent / quality + tangent * tangent;
        return BiquadCoefficients {
            1.0,
            -2.0,
            1.0,
            2.0 * (tangent * tangent - 1.0) / denominator,
            (1.0 - tangent / quality + tangent * tangent) / denominator
        };
    };

    return KWeightingCoefficients { makeHighShelf(), makeHighPass(), true };
}

struct LoudnessEngine::Impl final {
    bool prepare(const LoudnessEngineConfiguration& newConfiguration,
                 const MeterChannelLayout& newLayout) noexcept
    {
        if (!std::isfinite(newConfiguration.sampleRate) || newConfiguration.sampleRate <= 0.0
            || !newLayout.hasMeasuredChannels())
            return false;

        const auto newFramesPerBlock = static_cast<std::size_t>(std::llround(newConfiguration.sampleRate / 10.0));
        if (newFramesPerBlock == 0
            || std::abs(newConfiguration.sampleRate - static_cast<double>(newFramesPerBlock) * 10.0) > 1.0e-6)
            return false;

        const auto coefficients = kWeightingCoefficients(newConfiguration.sampleRate);
        if (!coefficients.valid)
            return false;

        configuration = newConfiguration;
        layout = newLayout;
        framesPerBlock = newFramesPerBlock;
        hasLfe = false;
        for (std::size_t channel = 0; channel < layout.channelCount(); ++channel)
            hasLfe = hasLfe || layout.isLfe(channel);
        kWeighting.configure(coefficients);
        truePeak.configure(configuration.sampleRate, configuration.enableTruePeak);
        prepared = true;
        reset();
        return true;
    }

    void reset() noexcept
    {
        if (!prepared)
            return;

        kWeighting.reset();
        truePeak.reset();
        blocks.fill({});
        blockWriteIndex = 0;
        availableBlocks = 0;
        currentBlock = {};
        integratedHistogram.reset();
        lraHistogram.reset();
        currentSnapshot = {};
    }

    EnergyWindow recentWindow(std::size_t blockCount) const noexcept
    {
        EnergyWindow window;
        if (availableBlocks < blockCount)
            return window;

        auto index = (blockWriteIndex + shortTermBlockCount - blockCount) % shortTermBlockCount;
        for (std::size_t block = 0; block < blockCount; ++block) {
            const auto& energyBlock = blocks[index];
            window.weightedEnergy += energyBlock.weightedEnergySum;
            window.rawEnergy += energyBlock.rawEnergySum;
            window.frames += energyBlock.frames;
            index = (index + 1) % shortTermBlockCount;
        }
        return window;
    }

    void updateIntegrated() noexcept
    {
        const auto absoluteStatistics = integratedHistogram.statisticsAtOrAbove(absoluteGateLufs);
        if (absoluteStatistics.count == 0) {
            currentSnapshot.integratedLufs = {};
            return;
        }

        const auto absoluteMean = absoluteStatistics.energySum / static_cast<double>(absoluteStatistics.count);
        const auto relativeGate = loudnessForEnergy(absoluteMean) - integratedRelativeGateLu;
        const auto statistics = integratedHistogram.statisticsAtOrAbove(
            std::max(absoluteGateLufs, relativeGate));
        currentSnapshot.integratedLufs = statistics.count == 0
            ? MeterValue {}
            : loudnessValue(statistics.energySum / static_cast<double>(statistics.count));
    }

    void updateLoudnessRange() noexcept
    {
        const auto absoluteStatistics = lraHistogram.statisticsAtOrAbove(absoluteGateLufs);
        if (absoluteStatistics.count == 0) {
            currentSnapshot.loudnessRangeLu = {};
            return;
        }

        const auto absoluteMean = absoluteStatistics.energySum / static_cast<double>(absoluteStatistics.count);
        const auto relativeGate = loudnessForEnergy(absoluteMean) - lraRelativeGateLu;
        const auto gate = std::max(absoluteGateLufs, relativeGate);
        const auto statistics = lraHistogram.statisticsAtOrAbove(gate);
        if (statistics.count == 0) {
            currentSnapshot.loudnessRangeLu = {};
            return;
        }

        const auto lower = lraHistogram.percentileAtOrAbove(gate, 0.10);
        const auto upper = lraHistogram.percentileAtOrAbove(gate, 0.95);
        currentSnapshot.loudnessRangeLu = MeterValue { std::max(0.0, upper - lower), true };
    }

    void completeBlock() noexcept
    {
        blocks[blockWriteIndex] = currentBlock;
        blockWriteIndex = (blockWriteIndex + 1) % shortTermBlockCount;
        availableBlocks = std::min(availableBlocks + 1, shortTermBlockCount);
        currentBlock = {};

        if (availableBlocks >= momentaryBlockCount) {
            const auto window = recentWindow(momentaryBlockCount);
            const auto weightedEnergy = window.weightedEnergy / static_cast<double>(window.frames);
            const auto rawEnergy = window.rawEnergy / static_cast<double>(window.frames);
            currentSnapshot.momentaryLufs = loudnessValue(weightedEnergy);
            currentSnapshot.rmsDbfs = MeterValue { rawEnergy > 0.0 ? 10.0 * std::log10(rawEnergy)
                                                                    : -std::numeric_limits<double>::infinity(),
                                                    true };
            integratedHistogram.add(weightedEnergy);
            updateIntegrated();
        }

        if (availableBlocks >= shortTermBlockCount) {
            const auto window = recentWindow(shortTermBlockCount);
            const auto weightedEnergy = window.weightedEnergy / static_cast<double>(window.frames);
            currentSnapshot.shortTermLufs = loudnessValue(weightedEnergy);
            lraHistogram.add(weightedEnergy);
            updateLoudnessRange();
        }
    }

    void process(const float* const* channels, std::size_t inputChannelCount, std::size_t frames) noexcept
    {
        if (!prepared || frames == 0)
            return;

        double samplePeak {};
        double truePeakValue {};
        double lfePeak {};
        for (std::size_t frame = 0; frame < frames; ++frame) {
            double weightedEnergy {};
            double rawEnergy {};
            for (std::size_t channel = 0; channel < layout.channelCount(); ++channel) {
                double sample {};
                if (channels == nullptr || channel >= inputChannelCount || channels[channel] == nullptr) {
                    ++currentSnapshot.invalidSampleCount;
                } else {
                    sample = static_cast<double>(channels[channel][frame]);
                    if (!std::isfinite(sample)) {
                        sample = 0.0;
                        ++currentSnapshot.invalidSampleCount;
                    }
                }

                const auto magnitude = std::abs(sample);
                if (layout.isLfe(channel)) {
                    lfePeak = std::max(lfePeak, magnitude);
                    continue;
                }

                const auto weight = layout.measurementWeight(channel);
                samplePeak = std::max(samplePeak, magnitude);
                rawEnergy += weight * sample * sample;
                const auto weightedSample = kWeighting.process(channel, sample);
                weightedEnergy += weight * weightedSample * weightedSample;
                truePeakValue = std::max(truePeakValue, truePeak.process(channel, sample));
            }

            currentBlock.weightedEnergySum += weightedEnergy;
            currentBlock.rawEnergySum += rawEnergy;
            ++currentBlock.frames;
            ++currentSnapshot.processedFrames;
            if (currentBlock.frames == framesPerBlock)
                completeBlock();
        }

        currentSnapshot.samplePeakDbfs = decibelsForAmplitude(samplePeak);
        currentSnapshot.truePeakDbtp = configuration.enableTruePeak
            ? decibelsForAmplitude(truePeakValue)
            : MeterValue {};
        currentSnapshot.lfePeakDbfs = hasLfe ? decibelsForAmplitude(lfePeak) : MeterValue {};
    }

    LoudnessEngineConfiguration configuration {};
    MeterChannelLayout layout {};
    KWeightingFilter kWeighting;
    TruePeakEstimator truePeak;
    LoudnessHistogram integratedHistogram;
    LoudnessHistogram lraHistogram;
    std::array<EnergyBlock, shortTermBlockCount> blocks {};
    EnergyBlock currentBlock {};
    MeterSnapshot currentSnapshot {};
    std::size_t framesPerBlock {};
    std::size_t blockWriteIndex {};
    std::size_t availableBlocks {};
    bool hasLfe {};
    bool prepared {};
};

LoudnessEngine::LoudnessEngine()
    : impl_(new Impl {})
{
}

LoudnessEngine::~LoudnessEngine()
{
    delete impl_;
}

bool LoudnessEngine::prepare(const LoudnessEngineConfiguration& configuration,
                             const MeterChannelLayout& layout) noexcept
{
    return impl_ != nullptr && impl_->prepare(configuration, layout);
}

bool LoudnessEngine::isPrepared() const noexcept
{
    return impl_ != nullptr && impl_->prepared;
}

void LoudnessEngine::reset() noexcept
{
    if (impl_ != nullptr)
        impl_->reset();
}

void LoudnessEngine::process(const float* const* channels,
                             std::size_t channelCount,
                             std::size_t frames) noexcept
{
    if (impl_ != nullptr)
        impl_->process(channels, channelCount, frames);
}

MeterSnapshot LoudnessEngine::snapshot() const noexcept
{
    return impl_ != nullptr ? impl_->currentSnapshot : MeterSnapshot {};
}

}