#pragma once

#include "ThemeContext.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::common
{

class MixerChannelStripComponent final : public juce::Component
{
public:
    explicit MixerChannelStripComponent(juce::String title = "通道");
    void setTitle(const juce::String& title);
    void setGain(float normalised);
    void setPan(float normalised);
    void setMeter(float peakNormalised, float rmsNormalised, bool overload);
    void setTheme(const ThemeContext& theme);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    void updateFromPoint(juce::Point<int> point);
    juce::String title;
    float gain = 0.5f;
    float pan = 0.5f;
    float peak = 0.0f;
    float rms = 0.0f;
    bool overload = false;
    ThemeContext theme;
    juce::Rectangle<int> faderBounds;
};

} // namespace wjn::common
