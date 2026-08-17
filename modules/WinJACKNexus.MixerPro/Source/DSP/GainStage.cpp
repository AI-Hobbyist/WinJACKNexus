#include "DSP/GainStage.h"

namespace mixerpro
{

void GainStage::prepare(double sampleRate, int maximumBlockSize, int channelCount)
{
    gain.prepare({ sampleRate,
                   static_cast<juce::uint32>(maximumBlockSize),
                   static_cast<juce::uint32>(channelCount) });
    gain.setRampDurationSeconds(0.005);
}

void GainStage::reset() noexcept
{
    gain.reset();
}

void GainStage::setGainDecibels(float gainDb)
{
    gain.setGainDecibels(gainDb);
}

void GainStage::process(juce::AudioBuffer<float>& buffer) noexcept
{
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    gain.process(context);
}

} // namespace mixerpro
