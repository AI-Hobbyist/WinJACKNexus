#pragma once

#include "ThemeContext.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace wjn::common
{

class HorizontalSliderControl final : public juce::Component
{
public:
    HorizontalSliderControl();

    void setRange(float minimum, float maximum);
    void setValue(float newValue, juce::NotificationType notification = juce::sendNotificationAsync);
    float getValue() const noexcept { return value; }
    void setAccent(juce::Colour newAccent);
    void setTheme(const ThemeContext& newTheme);
    void setValueChangeCallback(std::function<void(float)> callback);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    void updateFromPosition(float x);

    ThemeContext theme;
    juce::Colour accent { 0xff8de3ff };
    float minimum = 0.0f;
    float maximum = 1.0f;
    float value = 0.5f;
    std::function<void(float)> valueChangeCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HorizontalSliderControl)
};

} // namespace wjn::common