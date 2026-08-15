#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ThemeContext.h"

namespace wjn::common
{

class SettingsSlider final : public juce::Component
{
public:
    SettingsSlider();

    void setTheme(const ThemeContext& newTheme);
    void setRange(double minimum, double maximum, double interval = 0.0);
    void setValue(double newValue, juce::NotificationType notification);
    double getValue() const noexcept;
    void setTextValueSuffix(juce::String suffix);
    void setValueChangeCallback(std::function<void(double)> callback);

    void paint(juce::Graphics&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> getTrackBounds() const noexcept;
    void setValueFromPosition(float x, juce::NotificationType notification);
    void notifyValueChanged(juce::NotificationType notification);

    ThemeContext theme;
    double minimum = 0.0;
    double maximum = 1.0;
    double interval = 0.0;
    double value = 0.0;
    juce::String textValueSuffix;
    float dragStartX = 0.0f;
    double dragStartValue = 0.0;
    std::function<void(double)> valueChangeCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsSlider)
};

} // namespace wjn::common