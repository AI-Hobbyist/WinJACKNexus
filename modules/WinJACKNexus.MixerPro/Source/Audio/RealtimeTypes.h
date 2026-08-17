#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace mixerpro
{

struct AudioProcessContext
{
    const juce::AudioBuffer<float>& input;
    juce::AudioBuffer<float>& output;
    int numSamples = 0;
};

class AudioProcessCallback
{
public:
    virtual ~AudioProcessCallback() = default;
    virtual void process(AudioProcessContext& context) noexcept = 0;
};

} // namespace mixerpro
