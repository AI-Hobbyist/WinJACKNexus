/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include "Dsp.hpp"

#include <algorithm>
#include <cmath>

namespace WinJACKNexus::Common
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr float minimumLevel = 1.0e-6F;

float safeValue(float value, float fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

float timeAlpha(float milliseconds, double sampleRate) noexcept
{
    return static_cast<float>(
        std::exp(-1.0 / (std::max(0.01F, milliseconds) * 0.001 * sampleRate)));
}

float toDecibels(float amplitude) noexcept
{
    return amplitude > minimumLevel ? 20.0F * std::log10(amplitude) : -120.0F;
}
}

void CompressorProcessor::prepare(double sampleRate) noexcept
{
    sampleRate_ = std::isfinite(sampleRate) && sampleRate >= 8000.0 ? sampleRate : 48000.0;
    reset();
    setParameters(parameters_);
}

void CompressorProcessor::reset() noexcept
{
    highPassInput_ = 0.0F;
    highPassOutput_ = 0.0F;
    lowPassOutput_ = 0.0F;
    detectorEnvelope_ = 0.0F;
    inputMeterEnvelope_ = 0.0F;
    outputMeterEnvelope_ = 0.0F;
    gainReductionDb_ = 0.0F;
}

void CompressorProcessor::setParameters(const CompressorParameters &values) noexcept
{
    const auto previousRms = parameters_.rms;
    const auto previousSidechainFilter = parameters_.sidechainFilter;
    parameters_ = values;
    parameters_.thresholdDb = std::clamp(safeValue(parameters_.thresholdDb, -18.0F), -60.0F, 0.0F);
    parameters_.ratio = std::clamp(safeValue(parameters_.ratio, 3.0F), 1.0F, 20.0F);
    parameters_.kneeDb = std::clamp(safeValue(parameters_.kneeDb, 3.0F), 0.0F, 24.0F);
    parameters_.attackMs = std::clamp(safeValue(parameters_.attackMs, 10.0F), 0.1F, 200.0F);
    parameters_.releaseMs = std::clamp(safeValue(parameters_.releaseMs, 100.0F), 1.0F, 2000.0F);
    parameters_.makeupDb = std::clamp(safeValue(parameters_.makeupDb, 0.0F), -12.0F, 24.0F);
    parameters_.highPassHz = std::clamp(safeValue(parameters_.highPassHz, 80.0F), 20.0F, 2000.0F);
    parameters_.lowPassHz = std::clamp(safeValue(parameters_.lowPassHz, 8000.0F), 500.0F, 20000.0F);

    const auto maximumFrequency = static_cast<float>(sampleRate_ * 0.44);
    const auto highPassHz = std::min(parameters_.highPassHz, maximumFrequency);
    const auto lowPassHz = std::min(parameters_.lowPassHz, maximumFrequency);
    highPassAlpha_ = static_cast<float>(std::exp(-2.0 * pi * highPassHz / sampleRate_));
    lowPassAlpha_ = 1.0F - static_cast<float>(std::exp(-2.0 * pi * lowPassHz / sampleRate_));
    attackAlpha_ = timeAlpha(parameters_.attackMs, sampleRate_);
    releaseAlpha_ = timeAlpha(parameters_.releaseMs, sampleRate_);
    makeupDb_ = parameters_.autoMakeup
                    ? std::clamp(-parameters_.thresholdDb * (1.0F - 1.0F / parameters_.ratio),
                                 -12.0F, 24.0F)
                    : parameters_.makeupDb;

    if (parameters_.rms != previousRms)
    {
        detectorEnvelope_ = 0.0F;
        inputMeterEnvelope_ = 0.0F;
        outputMeterEnvelope_ = 0.0F;
    }
    if (parameters_.sidechainFilter != previousSidechainFilter)
        highPassInput_ = highPassOutput_ = lowPassOutput_ = 0.0F;
}

float CompressorProcessor::transferOutputDb(float inputDb) const noexcept
{
    const float halfKnee = parameters_.kneeDb * 0.5F;
    const float overThreshold = inputDb - parameters_.thresholdDb;
    if (parameters_.kneeDb <= 0.0F || overThreshold >= halfKnee)
        return parameters_.thresholdDb + overThreshold / parameters_.ratio;
    if (overThreshold <= -halfKnee)
        return inputDb;

    const float kneePosition = overThreshold + halfKnee;
    const float compression = (1.0F / parameters_.ratio - 1.0F) * kneePosition * kneePosition /
                              (2.0F * parameters_.kneeDb);
    return inputDb + compression;
}

float CompressorProcessor::process(float sample) noexcept
{
    if (!std::isfinite(sample))
        sample = 0.0F;
    const float boundedSample = std::clamp(sample, -64.0F, 64.0F);

    float detector = boundedSample;
    if (parameters_.sidechainFilter)
    {
        highPassOutput_ = highPassAlpha_ * (highPassOutput_ + boundedSample - highPassInput_);
        highPassInput_ = boundedSample;
        lowPassOutput_ += lowPassAlpha_ * (highPassOutput_ - lowPassOutput_);
        detector = lowPassOutput_;
    }

    const float detectorValue = parameters_.rms ? detector * detector : std::abs(detector);
    const float coefficient = detectorValue > detectorEnvelope_ ? attackAlpha_ : releaseAlpha_;
    detectorEnvelope_ = coefficient * detectorEnvelope_ + (1.0F - coefficient) * detectorValue;

    const float inputValue =
        parameters_.rms ? boundedSample * boundedSample : std::abs(boundedSample);
    const float inputMeterCoefficient =
        inputValue > inputMeterEnvelope_ ? attackAlpha_ : releaseAlpha_;
    inputMeterEnvelope_ =
        inputMeterCoefficient * inputMeterEnvelope_ + (1.0F - inputMeterCoefficient) * inputValue;

    const float measuredAmplitude =
        parameters_.rms ? std::sqrt(std::max(detectorEnvelope_, 0.0F)) : detectorEnvelope_;
    const float inputDb = toDecibels(measuredAmplitude);
    const float targetReduction = parameters_.enabled && parameters_.ratio > 1.0F
                                      ? std::max(0.0F, inputDb - transferOutputDb(inputDb))
                                      : 0.0F;
    const float gainCoefficient = targetReduction > gainReductionDb_ ? attackAlpha_ : releaseAlpha_;
    gainReductionDb_ =
        gainCoefficient * gainReductionDb_ + (1.0F - gainCoefficient) * targetReduction;

    float output = sample;
    if (parameters_.enabled)
    {
        const float gain = std::exp2((makeupDb_ - gainReductionDb_) * (1.0F / 6.020599913F));
        output = boundedSample * gain;
    }
    if (!std::isfinite(output))
        output = 0.0F;
    const float boundedOutput = std::clamp(output, -1.0e6F, 1.0e6F);
    const float outputValue =
        parameters_.rms ? boundedOutput * boundedOutput : std::abs(boundedOutput);
    const float outputMeterCoefficient =
        outputValue > outputMeterEnvelope_ ? attackAlpha_ : releaseAlpha_;
    outputMeterEnvelope_ = outputMeterCoefficient * outputMeterEnvelope_ +
                           (1.0F - outputMeterCoefficient) * outputValue;
    return output;
}

float CompressorProcessor::inputLevelDb() const noexcept
{
    const float amplitude =
        parameters_.rms ? std::sqrt(std::max(inputMeterEnvelope_, 0.0F)) : inputMeterEnvelope_;
    return toDecibels(amplitude);
}

float CompressorProcessor::outputLevelDb() const noexcept
{
    const float amplitude =
        parameters_.rms ? std::sqrt(std::max(outputMeterEnvelope_, 0.0F)) : outputMeterEnvelope_;
    return toDecibels(amplitude);
}

float CompressorProcessor::detectorLevelDb() const noexcept
{
    const float amplitude =
        parameters_.rms ? std::sqrt(std::max(detectorEnvelope_, 0.0F)) : detectorEnvelope_;
    return toDecibels(amplitude);
}

float CompressorProcessor::gainReductionDb() const noexcept
{
    return parameters_.enabled ? gainReductionDb_ : 0.0F;
}

}
