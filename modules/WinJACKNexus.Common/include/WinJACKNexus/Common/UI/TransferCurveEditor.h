#pragma once

#include <WinJACKNexus/Common/UI/ThemeContext.h>

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::common
{

class TransferCurveEditor final : public juce::Component
{
public:
    TransferCurveEditor();

    void setThreshold(float newValue);
    void setRange(float newValue);
    float getThreshold() const noexcept { return threshold; }
    float getRange() const noexcept { return range; }
    void setTheme(const ThemeContext& newTheme);
    void setValueChangeCallback(std::function<void(float, float)> callback);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    void updateFromPosition(juce::Point<float> position);

    ThemeContext theme;
    float threshold = 0.46f;
    float range = 0.68f;
    bool draggingThreshold = false;
    bool draggingRange = false;
    std::function<void(float, float)> valueChangeCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransferCurveEditor)
};

} // namespace wjn::common