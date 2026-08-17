#pragma once

#include <juce_dsp/juce_dsp.h>

namespace mixerpro
{

class LowCutFilter
{
public:
    void prepare(double sampleRate, int maximumBlockSize, int channelCount)
    {
        currentSampleRate = sampleRate;
        filter.prepare({ sampleRate,
                         static_cast<juce::uint32>(maximumBlockSize),
                         static_cast<juce::uint32>(channelCount) });
        setCutoffFrequency(80.0f);
    }

    void setEnabled(bool shouldBeEnabled) noexcept
    {
        enabled = shouldBeEnabled;
    }

    void setCutoffFrequency(float frequencyHz)
    {
        cutoffFrequencyHz = frequencyHz;
        filter.state = juce::dsp::IIR::Coefficients<float>::makeHighPass(currentSampleRate,
                                                                          cutoffFrequencyHz);
    }

    void reset() noexcept
    {
        filter.reset();
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept
    {
        if (!enabled)
            return;

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        filter.process(context);
    }

private:
    using Filter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                  juce::dsp::IIR::Coefficients<float>>;

    Filter filter;
    double currentSampleRate = 48000.0;
    float cutoffFrequencyHz = 80.0f;
    bool enabled = false;
};

} // namespace mixerpro
