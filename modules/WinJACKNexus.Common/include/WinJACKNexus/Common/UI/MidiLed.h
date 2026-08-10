#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::common
{

class MidiLed final : public juce::Component, private juce::Timer
{
public:
    MidiLed();

    void trigger();
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    float level = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiLed)
};

} // namespace wjn::common
