#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace mixerpro
{

class StandardPanner
{
public:
    void setPan(float newPan) noexcept;
    float getPan() const noexcept;
    void processMonoToStereo(const juce::AudioBuffer<float>& monoInput,
                             juce::AudioBuffer<float>& stereoOutput) const noexcept;

private:
    float pan = 0.0f;
};

struct SpatialPanState
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float divergence = 0.0f;
};

} // namespace mixerpro
