#pragma once

#include <WinJACKNexus/Common/UI/ThemeContext.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace wjn::common
{

class RotaryControl final : public juce::Component
{
public:
    RotaryControl(juce::String label = {}, juce::String suffix = {});

    void setRange(double minimum, double maximum);
    void setValue(double newValue, juce::NotificationType notification = juce::sendNotificationAsync);
    double getValue() const noexcept { return value; }
    void setLabel(juce::String newLabel);
    void setSuffix(juce::String newSuffix);
    void setAccent(juce::Colour newAccent);
    void setTheme(const ThemeContext& newTheme);
    void setValueChangeCallback(std::function<void(double)> callback);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    void updateFromDrag(float distance);
    void notifyValueChanged(juce::NotificationType notification);

    juce::String label;
    juce::String suffix;
    ThemeContext theme;
    juce::Colour accent { 0xff8de3ff };
    double minimum = 0.0;
    double maximum = 1.0;
    double value = 0.5;
    double valueOnMouseDown = 0.5;
    float dragStartY = 0.0f;
    std::function<void(double)> valueChangeCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RotaryControl)
};

} // namespace wjn::common