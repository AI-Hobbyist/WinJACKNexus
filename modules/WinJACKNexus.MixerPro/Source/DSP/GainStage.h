#pragma once

#include <juce_dsp/juce_dsp.h>

namespace mixerpro
{

class GainStage
{
public:
    void prepare(double sampleRate, int maximumBlockSize, int channelCount);
    void reset() noexcept;
    void setGainDecibels(float gainDb);
    void process(juce::AudioBuffer<float>& buffer) noexcept;

private:
    juce::dsp::Gain<float> gain;
};

} // namespace mixerpro
