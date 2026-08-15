#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::common
{

class AudioLed final : public juce::Component
{
public:
    AudioLed();

    void setLevel (float newLevel, bool clipping);
    void update();
    void paint (juce::Graphics&) override;

private:
    float level = 0.0f;
    float peak = 0.0f;
    int peakHoldMs = 0;
    bool clipping = false;
    bool repaintPending = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioLed)
};

} // namespace wjn::common
