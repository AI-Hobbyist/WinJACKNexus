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
    void setCompactStyle(bool shouldUseCompactStyle);
    void setValueTextFormatter(std::function<juce::String(double)> formatter);
    void setValueChangeCallback(std::function<void(double)> callback);
    void setContextMenuCallback(std::function<void()> callback);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    void updateFromDrag(float distance);
    void notifyValueChanged(juce::NotificationType notification);
    juce::String valueText() const;

    juce::String label;
    juce::String suffix;
    ThemeContext theme;
    juce::Colour accent { 0xff8de3ff };
    bool compactStyle = false;
    double minimum = 0.0;
    double maximum = 1.0;
    double value = 0.5;
    double valueOnMouseDown = 0.5;
    float dragStartY = 0.0f;
    bool valueIsBeingAdjusted = false;
    std::function<juce::String(double)> valueTextFormatter;
    std::function<void(double)> valueChangeCallback;
    std::function<void()> contextMenuCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RotaryControl)
};

} // namespace wjn::common