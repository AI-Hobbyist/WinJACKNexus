#include "MeterEngine.h"

#include <algorithm>
#include <cmath>

namespace wjn::common
{
namespace
{
constexpr float minimumDb = -100.0f;
constexpr double absoluteGateLufs = -70.0;
constexpr double lufsOffset = -0.691;
constexpr int truePeakOversampleFactor = 4;
constexpr int truePeakTaps = 32;
constexpr double pi = 3.14159265358979323846;
}

float MeterEngine::Biquad::process(float sample)
{
    const auto output = b0 * sample + z1;
    z1 = b1 * sample - a1 * output + z2;
    z2 = b2 * sample - a2 * output;
    return output;
}

void MeterEngine::Biquad::clear()
{
    z1 = 0.0f;
    z2 = 0.0f;
}

void MeterEngine::setSampleRate(double newSampleRate)
{
    if (newSampleRate <= 0.0 || std::abs(sampleRate - newSampleRate) < 0.5)
        return;

    sampleRate = newSampleRate;
    rmsWindowSamples = std::max(1, static_cast<int>(std::round(sampleRate * 0.010)));
    momentaryWindowSamples = std::max(1, static_cast<int>(std::round(sampleRate * 0.400)));
    shortTermWindowSamples = std::max(1, static_cast<int>(std::round(sampleRate * 3.000)));
    integratedHopSamples = std::max(1, static_cast<int>(std::round(sampleRate * 0.100)));
    shortTermHopSamples = std::max(1, static_cast<int>(std::round(sampleRate)));
    preFilter = makeHighShelf(sampleRate);
    highPass = makeHighPass(sampleRate);
    reset();
}

void MeterEngine::process(const float* samples, int frameCount)
{
    if (samples == nullptr || frameCount <= 0)
        return;

    auto peak = 0.0f;
    auto truePeak = 0.0f;
    for (int index = 0; index < frameCount; ++index)
    {
        const auto sample = samples[index];
        peak = std::max(peak, std::abs(sample));
        truePeak = std::max(truePeak, calculateTruePeak(sample));
        appendEnergy(sample, highPass.process(preFilter.process(sample)));
    }

    values.peakDbfs = toDecibels(peak);
    values.truePeakDbtp = toDecibels(truePeak);
    values.rmsDbfs = toDecibels(std::sqrt(rmsEnergySum / std::max<size_t>(1, rmsEnergies.size())));
    values.momentaryLufs = toLufs(momentaryEnergySum / std::max<size_t>(1, momentaryEnergies.size()));
    values.shortTermLufs = toLufs(shortTermEnergySum / std::max<size_t>(1, shortTermEnergies.size()));
}

void MeterEngine::reset()
{
    rmsEnergySum = momentaryEnergySum = shortTermEnergySum = 0.0;
    samplesSinceIntegratedBlock = samplesSinceShortTermBlock = 0;
    rmsEnergies.clear();
    momentaryEnergies.clear();
    shortTermEnergies.clear();
    integratedBlockEnergies.clear();
    shortTermLoudnesses.clear();
    truePeakHistory.fill(0.0f);
    truePeakWriteIndex = 0;
    truePeakHistorySize = 0;
    preFilter.clear();
    highPass.clear();
    values = {};
    values.peakDbfs = values.rmsDbfs = values.truePeakDbtp = minimumDb;
    values.momentaryLufs = values.shortTermLufs = values.integratedLufs = minimumDb;
}

float MeterEngine::toDecibels(double linear)
{
    return static_cast<float>(linear > 1.0e-10 ? 20.0 * std::log10(linear) : minimumDb);
}

float MeterEngine::toLufs(double meanSquare)
{
    return static_cast<float>(meanSquare > 1.0e-10 ? lufsOffset + 10.0 * std::log10(meanSquare) : minimumDb);
}

float MeterEngine::percentile(std::vector<float> valuesToSort, float fraction)
{
    if (valuesToSort.empty())
        return 0.0f;
    std::sort(valuesToSort.begin(), valuesToSort.end());
    const auto index = static_cast<size_t>(std::round((valuesToSort.size() - 1) * fraction));
    return valuesToSort[index];
}

MeterEngine::Biquad MeterEngine::makeHighShelf(double newSampleRate)
{
    const auto frequency = 1681.974450955533;
    const auto gain = std::pow(10.0, 4.0 / 40.0);
    const auto omega = 2.0 * pi * frequency / newSampleRate;
    const auto alpha = std::sin(omega) / 2.0 * std::sqrt((gain + 1.0 / gain) * (1.0 / 0.7071752369554196 - 1.0) + 2.0);
    const auto cosine = std::cos(omega);
    const auto rootGain = std::sqrt(gain);
    const auto a0 = (gain + 1.0) - (gain - 1.0) * cosine + 2.0 * rootGain * alpha;
    return { static_cast<float>(gain * ((gain + 1.0) + (gain - 1.0) * cosine + 2.0 * rootGain * alpha) / a0),
             static_cast<float>(-2.0 * gain * ((gain - 1.0) + (gain + 1.0) * cosine) / a0),
             static_cast<float>(gain * ((gain + 1.0) + (gain - 1.0) * cosine - 2.0 * rootGain * alpha) / a0),
             static_cast<float>(2.0 * ((gain - 1.0) - (gain + 1.0) * cosine) / a0),
             static_cast<float>(((gain + 1.0) - (gain - 1.0) * cosine - 2.0 * rootGain * alpha) / a0) };
}

MeterEngine::Biquad MeterEngine::makeHighPass(double newSampleRate)
{
    const auto omega = 2.0 * pi * 38.13547087602444 / newSampleRate;
    const auto alpha = std::sin(omega) / (2.0 * 0.5003270373238773);
    const auto cosine = std::cos(omega);
    const auto a0 = 1.0 + alpha;
    return { static_cast<float>((1.0 + cosine) / 2.0 / a0),
             static_cast<float>(-(1.0 + cosine) / a0),
             static_cast<float>((1.0 + cosine) / 2.0 / a0),
             static_cast<float>(-2.0 * cosine / a0),
             static_cast<float>((1.0 - alpha) / a0) };
}

void MeterEngine::appendEnergy(float sample, float weightedSample)
{
    const auto rawEnergy = sample * sample;
    const auto weightedEnergy = weightedSample * weightedSample;
    const auto appendToWindow = [](std::deque<float>& window, double& sum, int limit, float value)
    {
        window.push_back(value);
        sum += value;
        if (static_cast<int>(window.size()) > limit)
        {
            sum -= window.front();
            window.pop_front();
        }
    };
    appendToWindow(rmsEnergies, rmsEnergySum, rmsWindowSamples, rawEnergy);
    appendToWindow(momentaryEnergies, momentaryEnergySum, momentaryWindowSamples, weightedEnergy);
    appendToWindow(shortTermEnergies, shortTermEnergySum, shortTermWindowSamples, weightedEnergy);
    if (++samplesSinceIntegratedBlock >= integratedHopSamples)
    {
        samplesSinceIntegratedBlock = 0;
        appendIntegratedBlock();
    }
    if (++samplesSinceShortTermBlock >= shortTermHopSamples)
    {
        samplesSinceShortTermBlock = 0;
        appendShortTermBlock();
    }
}

void MeterEngine::appendIntegratedBlock()
{
    if (static_cast<int>(momentaryEnergies.size()) == momentaryWindowSamples)
    {
        integratedBlockEnergies.push_back(momentaryEnergySum / momentaryEnergies.size());
        updateIntegratedLoudness();
    }
}

void MeterEngine::appendShortTermBlock()
{
    if (static_cast<int>(shortTermEnergies.size()) == shortTermWindowSamples)
    {
        shortTermLoudnesses.push_back(toLufs(shortTermEnergySum / shortTermEnergies.size()));
        updateLra();
    }
}

void MeterEngine::updateIntegratedLoudness()
{
    std::vector<double> absoluteGated;
    for (const auto energy : integratedBlockEnergies)
        if (toLufs(energy) >= absoluteGateLufs)
            absoluteGated.push_back(energy);
    if (absoluteGated.empty())
        return;
    double meanEnergy = 0.0;
    for (const auto energy : absoluteGated)
        meanEnergy += energy;
    meanEnergy /= absoluteGated.size();
    const auto relativeGate = toLufs(meanEnergy) - 10.0f;
    double gatedEnergy = 0.0;
    size_t gatedCount = 0;
    for (const auto energy : absoluteGated)
        if (toLufs(energy) >= relativeGate)
        {
            gatedEnergy += energy;
            ++gatedCount;
        }
    if (gatedCount > 0)
        values.integratedLufs = toLufs(gatedEnergy / gatedCount);
}

void MeterEngine::updateLra()
{
    std::vector<float> lraValues;
    const auto lraGate = values.integratedLufs - 20.0f;
    for (const auto loudness : shortTermLoudnesses)
        if (loudness >= absoluteGateLufs && loudness >= lraGate)
            lraValues.push_back(loudness);
    if (lraValues.size() >= 2)
        values.lraLu = percentile(lraValues, 0.95f) - percentile(lraValues, 0.05f);
}

float MeterEngine::calculateTruePeak(float sample)
{
    static const auto interpolationCoefficients = []
    {
        std::array<std::array<float, truePeakTaps>, truePeakOversampleFactor - 1> coefficients {};
        for (int phase = 1; phase < truePeakOversampleFactor; ++phase)
        {
            double sum = 0.0;
            const auto fractionalDelay = static_cast<double>(phase) / truePeakOversampleFactor;
            for (int tap = 0; tap < truePeakTaps; ++tap)
            {
                const auto distance = static_cast<double>(tap) - fractionalDelay;
                const auto sincValue = std::abs(distance) < 1.0e-12 ? 1.0 : std::sin(pi * distance) / (pi * distance);
                const auto window = 0.5 + 0.5 * std::cos(pi * distance / truePeakTaps);
                coefficients[static_cast<size_t>(phase - 1)][static_cast<size_t>(tap)] = static_cast<float>(sincValue * window);
                sum += sincValue * window;
            }
            for (auto& coefficient : coefficients[static_cast<size_t>(phase - 1)])
                coefficient = static_cast<float>(coefficient / sum);
        }
        return coefficients;
    }();
    truePeakHistory[static_cast<size_t>(truePeakWriteIndex)] = sample;
    truePeakWriteIndex = (truePeakWriteIndex + 1) % truePeakTaps;
    truePeakHistorySize = std::min(truePeakHistorySize + 1, truePeakTaps);
    auto peak = std::abs(sample);
    if (truePeakHistorySize < truePeakTaps)
        return peak;
    for (int phase = 1; phase < truePeakOversampleFactor; ++phase)
    {
        double value = 0.0;
        const auto& coefficients = interpolationCoefficients[static_cast<size_t>(phase - 1)];
        for (int tap = 0; tap < truePeakTaps; ++tap)
        {
            const auto historyIndex = (truePeakWriteIndex + truePeakTaps - 1 - tap) % truePeakTaps;
            value += truePeakHistory[static_cast<size_t>(historyIndex)] * coefficients[static_cast<size_t>(tap)];
        }
        peak = std::max(peak, static_cast<float>(std::abs(value)));
    }
    return peak;
}

} // namespace wjn::common
