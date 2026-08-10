#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::common
{

class AudioLed final : public juce::Component, private juce::Timer
{
public:
    AudioLed();

    void setLevel (float newLevel, bool clipping);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    float level = 0.0f;
    float peak = 0.0f;
    int peakHoldMs = 0;
    bool clipping = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioLed)
};

} // namespace wjn::common
