/*
 * This file is part of WinJACKNexus.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "Dsp.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace WinJACKNexus::Common
{
namespace
{
constexpr double pi = 3.14159265358979323846;

float timeAlpha(float milliseconds, double sampleRate) noexcept
{
    return static_cast<float>(
        std::exp(-1.0 / (std::max(0.01F, milliseconds) * 0.001 * sampleRate)));
}
}

void GateProcessor::prepare(double sampleRate) noexcept
{
    sampleRate_ = std::isfinite(sampleRate) && sampleRate > 1000.0 ? sampleRate : 48000.0;
    reset();
    setParameters(parameters_);
}

void GateProcessor::reset() noexcept
{
    highPassInput_ = highPassOutput_ = lowPassOutput_ = envelope_ = 0.0F;
    gainDb_ = parameters_.floorDb;
    gateOpen_ = false;
}

void GateProcessor::setParameters(const GateParameters &values) noexcept
{
    parameters_ = values;
    parameters_.thresholdDb = std::clamp(parameters_.thresholdDb, -60.0F, 0.0F);
    parameters_.ratio = std::clamp(parameters_.ratio, 1.0F, 20.0F);
    parameters_.attackMs = std::clamp(parameters_.attackMs, 0.1F, 200.0F);
    parameters_.releaseMs = std::clamp(parameters_.releaseMs, 1.0F, 2000.0F);
    parameters_.floorDb = std::clamp(parameters_.floorDb, -90.0F, 0.0F);
    parameters_.holdDb = std::clamp(parameters_.holdDb, 0.0F, 24.0F);
    parameters_.highPassHz = std::clamp(parameters_.highPassHz, 20.0F, 2000.0F);
    parameters_.lowPassHz = std::clamp(parameters_.lowPassHz, 500.0F, 20000.0F);

    const auto hp = static_cast<float>(std::exp(-2.0 * pi * parameters_.highPassHz / sampleRate_));
    highPassAlpha_ = hp;
    lowPassAlpha_ =
        1.0F - static_cast<float>(std::exp(-2.0 * pi * parameters_.lowPassHz / sampleRate_));
    attackAlpha_ = timeAlpha(parameters_.attackMs, sampleRate_);
    releaseAlpha_ = timeAlpha(parameters_.releaseMs, sampleRate_);
    detectorAlpha_ = timeAlpha(5.0F, sampleRate_);
}

float GateProcessor::process(float sample) noexcept
{
    if (!std::isfinite(sample))
        sample = 0.0F;
    if (!parameters_.enabled)
        return sample;

    float detector = sample;
    if (parameters_.sidechainFilter)
    {
        highPassOutput_ = highPassAlpha_ * (highPassOutput_ + sample - highPassInput_);
        highPassInput_ = sample;
        lowPassOutput_ += lowPassAlpha_ * (highPassOutput_ - lowPassOutput_);
        detector = lowPassOutput_;
    }
    const float magnitude = std::abs(detector);
    const float detectorCoefficient = magnitude > envelope_ ? attackAlpha_ : detectorAlpha_;
    envelope_ = detectorCoefficient * envelope_ + (1.0F - detectorCoefficient) * magnitude;
    const float levelDb = 20.0F * std::log10(std::max(envelope_, 1.0e-9F));
    float desiredDb = 0.0F;
    if (levelDb >= parameters_.thresholdDb)
        gateOpen_ = true;
    else if (levelDb <= parameters_.thresholdDb - parameters_.holdDb)
        gateOpen_ = false;
    if (!gateOpen_)
    {
        desiredDb = std::max(parameters_.floorDb,
                             (levelDb - parameters_.thresholdDb) * (parameters_.ratio - 1.0F));
    }
    const float coefficient = desiredDb > gainDb_ ? attackAlpha_ : releaseAlpha_;
    gainDb_ = coefficient * gainDb_ + (1.0F - coefficient) * desiredDb;
    return sample * std::pow(10.0F, gainDb_ / 20.0F);
}

float GateProcessor::currentGainDb() const noexcept
{
    return gainDb_;
}
}
