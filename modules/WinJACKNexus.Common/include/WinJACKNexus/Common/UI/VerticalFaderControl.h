#pragma once

#include <WinJACKNexus/Common/UI/ThemeContext.h>

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::common
{

class VerticalFaderControl final : public juce::Component
{
public:
    VerticalFaderControl();

    void setRange(float minimum, float maximum);
    void setValue(float newValue, juce::NotificationType notification = juce::sendNotificationAsync);
    float getValue() const noexcept { return value; }
    void setAccent(juce::Colour newAccent);
    void setTheme(const ThemeContext& newTheme);
    void setCompactStyle(bool shouldUseCompactStyle);
    void setLabel(juce::String newLabel);
    void setValueLabelVisible(bool shouldShowValueLabel);
    void setValueChangeCallback(std::function<void(float)> callback);
    void setContextMenuCallback(std::function<void()> callback);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    void updateFromPosition(float y);

    ThemeContext theme;
    juce::Colour accent { 0xffd6dde6 };
    juce::String label { "FADER" };
    bool compactStyle = false;
    bool valueLabelVisible = true;
    bool valueIsBeingAdjusted = false;
    float minimum = -60.0f;
    float maximum = 12.0f;
    float value = 0.0f;
    std::function<void(float)> valueChangeCallback;
    std::function<void()> contextMenuCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VerticalFaderControl)
};

} // namespace wjn::common