#include "DSP/Panner.h"

#include <algorithm>
#include <cmath>

namespace mixerpro
{

void StandardPanner::setPan(float newPan) noexcept
{
    pan = std::clamp(newPan, -1.0f, 1.0f);
}

float StandardPanner::getPan() const noexcept
{
    return pan;
}

void StandardPanner::processMonoToStereo(const juce::AudioBuffer<float>& monoInput,
                                         juce::AudioBuffer<float>& stereoOutput) const noexcept
{
    stereoOutput.clear();

    if (monoInput.getNumChannels() < 1 || stereoOutput.getNumChannels() < 2)
        return;

    const auto samples = std::min(monoInput.getNumSamples(), stereoOutput.getNumSamples());
    const auto angle = (pan + 1.0f) * (juce::MathConstants<float>::pi / 4.0f);
    const auto leftGain = std::cos(angle);
    const auto rightGain = std::sin(angle);

    stereoOutput.addFrom(0, 0, monoInput, 0, 0, samples, leftGain);
    stereoOutput.addFrom(1, 0, monoInput, 0, 0, samples, rightGain);
}

} // namespace mixerpro
