#pragma once

#include <WinJACKNexus/Common/UI/ThemeContext.h>

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::common
{

class SegmentedMeterControl final : public juce::Component
{
public:
    SegmentedMeterControl(juce::String label = {}, juce::Colour accent = juce::Colour(0xff8de3ff));

    void setLevel(float newLevel);
    float getLevel() const noexcept { return level; }
    void setHold(float newHold);
    void setTheme(const ThemeContext& newTheme);
    void paint(juce::Graphics&) override;

private:
    ThemeContext theme;
    juce::String label;
    juce::Colour accent;
    float level = 0.5f;
    float hold = 0.65f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SegmentedMeterControl)
};

} // namespace wjn::common