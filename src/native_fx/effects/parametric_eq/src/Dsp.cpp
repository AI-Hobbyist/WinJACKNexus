/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include "Dsp.hpp"
#include <algorithm>
#include <cmath>

namespace WinJACKNexus::Common
{
namespace
{
constexpr double pi = 3.14159265358979323846;
using Shape = ParametricEqShape;

template <typename Filter>
void configure(Filter &filter, Shape shape, float frequency, float q, float gain,
               double rate) noexcept
{
    const double f = std::clamp<double>(frequency, 10.0, rate * 0.49);
    const double w = 2.0 * pi * f / rate;
    const double cs = std::cos(w), sn = std::sin(w);
    const double a = std::pow(10.0, std::clamp<double>(gain, -24.0, 24.0) / 40.0);
    const double alpha = sn / (2.0 * std::clamp<double>(q, 0.1, 20.0));
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (shape)
    {
    case Shape::bell:
        b0 = 1 + alpha * a;
        b1 = -2 * cs;
        b2 = 1 - alpha * a;
        a0 = 1 + alpha / a;
        a1 = -2 * cs;
        a2 = 1 - alpha / a;
        break;
    case Shape::lowShelf:
    case Shape::highShelf: {
        const double root = 2 * std::sqrt(a) * alpha;
        const double plus = (a + 1) + (a - 1) * cs;
        const double minus = (a + 1) - (a - 1) * cs;
        if (shape == Shape::lowShelf)
        {
            b0 = a * (minus + root);
            b1 = 2 * a * ((a - 1) - (a + 1) * cs);
            b2 = a * (minus - root);
            a0 = plus + root;
            a1 = -2 * ((a - 1) + (a + 1) * cs);
            a2 = plus - root;
        }
        else
        {
            b0 = a * (plus + root);
            b1 = -2 * a * ((a - 1) + (a + 1) * cs);
            b2 = a * (plus - root);
            a0 = minus + root;
            a1 = 2 * ((a - 1) - (a + 1) * cs);
            a2 = minus - root;
        }
        break;
    }
    case Shape::lowPass:
        b0 = (1 - cs) / 2;
        b1 = 1 - cs;
        b2 = b0;
        a0 = 1 + alpha;
        a1 = -2 * cs;
        a2 = 1 - alpha;
        break;
    case Shape::highPass:
        b0 = (1 + cs) / 2;
        b1 = -(1 + cs);
        b2 = b0;
        a0 = 1 + alpha;
        a1 = -2 * cs;
        a2 = 1 - alpha;
        break;
    }
    filter.b0 = b0 / a0;
    filter.b1 = b1 / a0;
    filter.b2 = b2 / a0;
    filter.a1 = a1 / a0;
    filter.a2 = a2 / a0;
}

template <typename Filter, typename Push>
void build(const ParametricEqParameters &p, double rate, Filter &filter, Push push) noexcept
{
    if (!p.enabled)
        return;
    if (p.highPassEnabled)
    {
        const int sections = std::clamp(p.highPassSlope / 12, 1, 4);
        for (int i = 0; i < sections; ++i)
        {
            configure(filter, Shape::highPass, p.highPassHz, 0.7071F, 0, rate);
            push(filter);
        }
    }
    for (const auto &band : p.bands)
    {
        if (!band.enabled)
            continue;
        configure(filter, band.shape, band.frequencyHz, band.q, band.gainDb, rate);
        push(filter);
    }
    if (p.lowPassEnabled)
    {
        const int sections = std::clamp(p.lowPassSlope / 12, 1, 4);
        for (int i = 0; i < sections; ++i)
        {
            configure(filter, Shape::lowPass, p.lowPassHz, 0.7071F, 0, rate);
            push(filter);
        }
    }
}
}

float ParametricEqProcessor::Biquad::process(float input) noexcept
{
    const double output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    if (std::abs(z1) < 1e-25)
        z1 = 0;
    if (std::abs(z2) < 1e-25)
        z2 = 0;
    return static_cast<float>(output);
}

double ParametricEqProcessor::Biquad::magnitude(double radians) const noexcept
{
    const double c1 = std::cos(radians), s1 = std::sin(radians);
    const double c2 = std::cos(2 * radians), s2 = std::sin(2 * radians);
    const double nr = b0 + b1 * c1 + b2 * c2, ni = -b1 * s1 - b2 * s2;
    const double dr = 1 + a1 * c1 + a2 * c2, di = -a1 * s1 - a2 * s2;
    return std::sqrt((nr * nr + ni * ni) / std::max(1e-30, dr * dr + di * di));
}

void ParametricEqProcessor::prepare(double rate) noexcept
{
    sampleRate_ = std::max(1000.0, rate);
    reset();
    rebuild();
}

void ParametricEqProcessor::reset() noexcept
{
    for (auto &filter : filters_)
        filter.z1 = filter.z2 = 0;
}

void ParametricEqProcessor::setParameters(const ParametricEqParameters &parameters) noexcept
{
    parameters_ = parameters;
    rebuild();
}

void ParametricEqProcessor::rebuild() noexcept
{
    filterCount_ = 0;
    Biquad next;
    build(parameters_, sampleRate_, next, [this](const Biquad &coefficients) {
        if (filterCount_ >= filters_.size())
            return;
        auto &filter = filters_[filterCount_++];
        filter.b0 = coefficients.b0;
        filter.b1 = coefficients.b1;
        filter.b2 = coefficients.b2;
        filter.a1 = coefficients.a1;
        filter.a2 = coefficients.a2;
    });
    outputGain_ = parameters_.enabled ? std::pow(10.0F, parameters_.outputGainDb / 20.0F) : 1.0F;
}

float ParametricEqProcessor::process(float sample) noexcept
{
    for (std::size_t i = 0; i < filterCount_; ++i)
        sample = filters_[i].process(sample);
    return sample * outputGain_;
}

float ParametricEqProcessor::responseDb(const ParametricEqParameters &parameters, double sampleRate,
                                        float frequencyHz) noexcept
{
    if (!parameters.enabled)
        return 0;
    Biquad filter;
    double magnitude = 1;
    build(parameters, std::max(1000.0, sampleRate), filter, [&](const Biquad &value) {
        magnitude *= value.magnitude(2.0 * pi * std::min<double>(frequencyHz, sampleRate * 0.49) /
                                     sampleRate);
    });
    return static_cast<float>(20.0 * std::log10(std::max(1e-12, magnitude)) +
                              parameters.outputGainDb);
}
}
