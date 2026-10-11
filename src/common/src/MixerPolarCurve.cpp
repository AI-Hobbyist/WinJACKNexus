/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Common/MixerPolarCurve.hpp>
#include <WinJACKNexus/Common/AuxMixerState.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace WinJACKNexus::Common
{
namespace
{
constexpr double pi = 3.14159265358979323846;
struct Direction
{
    double angle{};
    double power{};
};

AuxChannelLayout auxLayout(MixerLayout layout) noexcept
{
    switch (layout)
    {
    case MixerLayout::mono:
        return AuxChannelLayout::mono;
    case MixerLayout::stereo:
        return AuxChannelLayout::stereo;
    case MixerLayout::surround51:
        return AuxChannelLayout::surround51;
    case MixerLayout::surround71:
        return AuxChannelLayout::surround71;
    }
    return AuxChannelLayout::stereo;
}

std::array<float, 360> interpolate(MixerLayout layout, const std::array<double, 8> &powers) noexcept
{
    std::array<float, 360> radii{};
    const auto points = auxLayoutReferencePoints(auxLayout(layout));
    std::array<Direction, 8> directions{};
    std::size_t count = 0;
    for (int channel = 0; channel < points.size(); ++channel)
    {
        if (!auxHasSpatialPoint(auxLayout(layout), channel))
            continue;
        const auto point = points[channel];
        double angle = std::atan2(point.x(), -point.y()) * 180.0 / pi;
        if (angle < 0.0)
            angle += 360.0;
        directions[count++] = {angle, std::max(0.0, powers[static_cast<std::size_t>(channel)])};
    }
    if (count == 0)
        return radii;
    std::sort(directions.begin(), directions.begin() + count,
              [](const auto &left, const auto &right) { return left.angle < right.angle; });
    if (count == 1)
    {
        radii.fill(MixerPolarCurve::radiusForPower(directions[0].power));
        return radii;
    }
    for (int degree = 0; degree < 360; ++degree)
    {
        double angle = static_cast<double>(degree);
        std::size_t upper = 0;
        while (upper < count && directions[upper].angle <= angle)
            ++upper;
        const auto next = upper == count ? directions[0] : directions[upper];
        const auto previous = upper == 0 ? directions[count - 1] : directions[upper - 1];
        const double start = upper == 0 ? previous.angle - 360.0 : previous.angle;
        const double end = upper == count ? next.angle + 360.0 : next.angle;
        const double fraction = (angle - start) / (end - start);
        radii[degree] = MixerPolarCurve::radiusForPower(previous.power +
                                                        (next.power - previous.power) * fraction);
    }
    return radii;
}
}

float MixerPolarCurve::radiusForPower(double power) noexcept
{
    if (!std::isfinite(power) || power <= 0.0)
        return 0.0F;
    const double db = 10.0 * std::log10(power);
    return static_cast<float>(std::clamp((db + 72.0) / 84.0, 0.0, 1.0));
}

bool MixerPolarCurve::accept(const MixerPolarSnapshot &snapshot) noexcept
{
    if (!snapshot.valid || snapshot.sampleCount == 0 || snapshot.sampleRate == 0 ||
        snapshot.endSample <= snapshot.firstSample)
    {
        clear();
        return false;
    }
    const bool switched =
        !active_ || snapshot.sourceId != last_.sourceId || snapshot.targetId != last_.targetId ||
        snapshot.topologyVersion != last_.topologyVersion ||
        snapshot.sourceLayout != last_.sourceLayout ||
        snapshot.targetLayout != last_.targetLayout || snapshot.sampleRate != last_.sampleRate ||
        snapshot.firstSample < last_.endSample;
    if (switched)
    {
        inputPower_.fill(0.0);
        outputPower_.fill(0.0);
    }
    const double elapsed =
        switched ? static_cast<double>(snapshot.sampleCount) / snapshot.sampleRate
                 : static_cast<double>(snapshot.endSample - last_.endSample) / snapshot.sampleRate;
    const double seconds = std::clamp(elapsed, 0.0, 1.0);
    for (std::size_t channel = 0; channel < 8; ++channel)
    {
        const double input = snapshot.inputSumSquares[channel] / snapshot.sampleCount;
        const double output = snapshot.outputSumSquares[channel] / snapshot.sampleCount;
        if (!std::isfinite(input) || !std::isfinite(output) || input < 0.0 || output < 0.0)
        {
            clear();
            return false;
        }
        const auto smooth = [seconds](double previous, double target) {
            const double tau = target > previous ? .03 : .25;
            const double a = std::exp(-seconds / tau);
            const double result = a * previous + (1.0 - a) * target;
            return result < std::pow(10.0, -72.0 / 10.0) ? 0.0 : result;
        };
        inputPower_[channel] = smooth(inputPower_[channel], input);
        outputPower_[channel] = smooth(outputPower_[channel], output);
    }
    last_ = snapshot;
    active_ = true;
    return true;
}

MixerPolarContour MixerPolarCurve::contour() const noexcept
{
    MixerPolarContour result;
    if (!active_)
        return result;
    result.inputRadius = interpolate(last_.sourceLayout, inputPower_);
    result.outputRadius = interpolate(last_.targetLayout, outputPower_);
    const double overPower = std::pow(10.0, 12.0 / 10.0);
    for (std::size_t channel = 0; channel < 8; ++channel)
    {
        result.inputOverrange |=
            inputPower_[channel] > overPower || last_.inputPeak[channel] > std::sqrt(overPower);
        result.outputOverrange |=
            outputPower_[channel] > overPower || last_.outputPeak[channel] > std::sqrt(overPower);
    }
    result.valid = true;
    return result;
}

void MixerPolarCurve::clear() noexcept
{
    active_ = false;
    inputPower_.fill(0.0);
    outputPower_.fill(0.0);
}

}
