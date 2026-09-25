/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include "Dsp.hpp"

#include <algorithm>
#include <cmath>

namespace WinJACKNexus::Common {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr float minimumGain = 0.12589254F; // -18 dB

float timeCoefficient(float milliseconds, double sampleRate) noexcept
{
    if (milliseconds <= 0.0F)
        return 0.0F;
    return static_cast<float>(std::exp(-1.0 / (sampleRate * milliseconds * 0.001)));
}
}

void DeEsserProcessor::configureBandPass(Biquad& filter, double frequency,
                                         double q, double sampleRate) noexcept
{
    const auto omega = 2.0 * pi * frequency / sampleRate;
    const auto cosine = std::cos(omega);
    const auto alpha = std::sin(omega) / (2.0 * q);
    const auto inverseA0 = 1.0 / (1.0 + alpha);
    filter.b0 = alpha * inverseA0;
    filter.b1 = 0.0;
    filter.b2 = -alpha * inverseA0;
    filter.a1 = -2.0 * cosine * inverseA0;
    filter.a2 = (1.0 - alpha) * inverseA0;
}

float DeEsserProcessor::Biquad::process(float input) noexcept
{
    const double output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    return static_cast<float>(output);
}

void DeEsserProcessor::prepare(double sampleRate) noexcept
{
    sampleRate_ = std::isfinite(sampleRate) && sampleRate >= 8000.0 ? sampleRate : 48000.0;
    updateFilters();
    reset();
}

void DeEsserProcessor::reset() noexcept
{
    detectorFilter_.z1 = detectorFilter_.z2 = 0.0;
    treatmentFilter_.z1 = treatmentFilter_.z2 = 0.0;
    envelope_ = 0.0F;
    gain_ = 1.0F;
}

void DeEsserProcessor::setParameters(const DeEsserParameters& parameters) noexcept
{
    const auto previousDetector = parameters_.detector;
    parameters_ = parameters;
    parameters_.centerFrequencyHz = std::clamp(parameters_.centerFrequencyHz, 1000.0F,
                                                static_cast<float>(sampleRate_ * 0.44));
    parameters_.bandwidthHz = std::clamp(parameters_.bandwidthHz, 250.0F,
                                          static_cast<float>(sampleRate_ * 0.4));
    parameters_.thresholdDb = std::clamp(parameters_.thresholdDb, -60.0F, 0.0F);
    parameters_.attackMs = std::clamp(parameters_.attackMs, 0.0F, 100.0F);
    parameters_.releaseMs = std::clamp(parameters_.releaseMs, 5.0F, 1000.0F);
    parameters_.outputGainDb = std::clamp(parameters_.outputGainDb, -24.0F, 12.0F);
    if (parameters_.mode > DeEsserMode::DynamicEq) parameters_.mode = DeEsserMode::Split;
    if (parameters_.detector > DeEsserDetector::Peak) parameters_.detector = DeEsserDetector::Rms;
    if (parameters_.listen > DeEsserListen::Original) parameters_.listen = DeEsserListen::Off;
    if (parameters_.detector != previousDetector) envelope_ = 0.0F;
    outputGain_ = std::pow(10.0F, parameters_.outputGainDb / 20.0F);
    updateFilters();
}

void DeEsserProcessor::updateFilters() noexcept
{
    const auto frequency = std::clamp<double>(parameters_.centerFrequencyHz, 1000.0, sampleRate_ * 0.44);
    const auto bandwidth = std::clamp<double>(parameters_.bandwidthHz, 250.0, sampleRate_ * 0.4);
    const auto q = std::clamp(frequency / bandwidth, 0.35, 12.0);
    configureBandPass(detectorFilter_, frequency, q, sampleRate_);
    const auto treatmentQ = parameters_.mode == DeEsserMode::DynamicEq ? q * 1.5 : q;
    configureBandPass(treatmentFilter_, frequency, std::min(treatmentQ, 18.0), sampleRate_);
}

float DeEsserProcessor::process(float sample) noexcept
{
    if (!parameters_.enabled)
        return sample;

    const auto detector = detectorFilter_.process(sample);
    const float detectorLevel = parameters_.detector == DeEsserDetector::Rms
        ? detector * detector : std::abs(detector);
    const float coefficient = timeCoefficient(detectorLevel > envelope_ ? parameters_.attackMs
                                                                        : parameters_.releaseMs,
                                              sampleRate_);
    envelope_ = coefficient * envelope_ + (1.0F - coefficient) * detectorLevel;
    const float measuredLevel = parameters_.detector == DeEsserDetector::Rms
        ? std::sqrt(std::max(envelope_, 0.0F)) : envelope_;
    const float envelopeDb = 20.0F * std::log10(std::max(measuredLevel, 1.0e-8F));
    const float excessDb = std::max(0.0F, envelopeDb - parameters_.thresholdDb);
    const float targetGain = std::max(minimumGain, std::pow(10.0F, -(excessDb * 0.75F) / 20.0F));
    const float gainCoefficient = timeCoefficient(targetGain < gain_ ? parameters_.attackMs
                                                                      : parameters_.releaseMs,
                                                  sampleRate_);
    gain_ = gainCoefficient * gain_ + (1.0F - gainCoefficient) * targetGain;

    if (parameters_.listen == DeEsserListen::Sibilance)
        return detector * outputGain_;

    const auto treatmentBand = treatmentFilter_.process(sample);
    const float output = parameters_.mode == DeEsserMode::Wideband
        ? sample * gain_
        : sample + treatmentBand * (gain_ - 1.0F);
    if (parameters_.listen == DeEsserListen::Original)
        return sample;
    return output * outputGain_;
}

}
