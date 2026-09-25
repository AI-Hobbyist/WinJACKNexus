/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include "Dsp.hpp"

#include <algorithm>
#include <cmath>

namespace WinJACKNexus::Common {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr double inactiveStateThreshold = 1.0e-12;
}

void GraphicEqProcessor::prepare(double sampleRate) noexcept
{
    sampleRate_ = std::isfinite(sampleRate) && sampleRate > 1000.0 ? sampleRate : 48000.0;
    reset();
    updateFilters();
}

void GraphicEqProcessor::reset() noexcept
{
    for (auto& filter : filters_) {
        filter.z1 = filter.z2 = 0.0;
        filter.settling = false;
    }
    rebuildActiveFilterList();
}

void GraphicEqProcessor::setParameters(const GraphicEqParameters& parameters) noexcept
{
    parameters_ = parameters;
    for (auto& gain : parameters_.bandGainsDb)
        gain = std::clamp(gain, -12.0F, 12.0F);
    parameters_.outputGainDb = std::clamp(parameters_.outputGainDb, -24.0F, 12.0F);
    outputGain_ = std::pow(10.0F, parameters_.outputGainDb / 20.0F);
    updateFilters();
}

void GraphicEqProcessor::updateFilters() noexcept
{
    constexpr double q = 4.31847304696; // one-third-octave bandwidth
    for (std::size_t i = 0; i < filters_.size(); ++i) {
        auto& filter = filters_[i];
        const double frequency = std::clamp<double>(graphicEqFrequencies[i], 10.0, sampleRate_ * 0.45);
        const double amplitude = std::pow(10.0, static_cast<double>(parameters_.bandGainsDb[i]) / 40.0);
        const double omega = 2.0 * pi * frequency / sampleRate_;
        const double cosine = std::cos(omega);
        const double alpha = std::sin(omega) / (2.0 * q);
        const double a0 = 1.0 + alpha / amplitude;
        filter.b0 = (1.0 + alpha * amplitude) / a0;
        filter.b1 = (-2.0 * cosine) / a0;
        filter.b2 = (1.0 - alpha * amplitude) / a0;
        filter.a1 = (-2.0 * cosine) / a0;
        filter.a2 = (1.0 - alpha / amplitude) / a0;
        filter.settling = parameters_.bandGainsDb[i] == 0.0F
            && (std::abs(filter.z1) > inactiveStateThreshold
                || std::abs(filter.z2) > inactiveStateThreshold);
    }
    rebuildActiveFilterList();
}

void GraphicEqProcessor::rebuildActiveFilterList() noexcept
{
    activeFilterCount_ = 0;
    for (std::size_t i = 0; i < filters_.size(); ++i) {
        if (parameters_.bandGainsDb[i] != 0.0F || filters_[i].settling)
            activeFilterIndices_[activeFilterCount_++] = i;
    }
}

float GraphicEqProcessor::process(float sample) noexcept
{
    if (!parameters_.enabled)
        return sample;
    double value = sample;
    bool activeFilterListChanged = false;
    for (std::size_t active = 0; active < activeFilterCount_; ++active) {
        auto& filter = filters_[activeFilterIndices_[active]];
        const double output = filter.b0 * value + filter.z1;
        filter.z1 = filter.b1 * value - filter.a1 * output + filter.z2;
        filter.z2 = filter.b2 * value - filter.a2 * output;
        value = output;
        if (filter.settling
            && std::abs(filter.z1) <= inactiveStateThreshold
            && std::abs(filter.z2) <= inactiveStateThreshold) {
            filter.z1 = filter.z2 = 0.0;
            filter.settling = false;
            activeFilterListChanged = true;
        }
    }
    if (activeFilterListChanged)
        rebuildActiveFilterList();
    return static_cast<float>(value) * outputGain_;
}

}
