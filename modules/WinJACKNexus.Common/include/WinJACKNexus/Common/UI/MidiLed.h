#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::common
{

class MidiLed final : public juce::Component
{
public:
    MidiLed();

    void trigger();
    void update();
    void paint (juce::Graphics&) override;

private:
    float level = 0.0f;
    bool repaintPending = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiLed)
};

} // namespace wjn::common
