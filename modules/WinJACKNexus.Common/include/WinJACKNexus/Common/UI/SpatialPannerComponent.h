#pragma once

#include "ThemeContext.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::common
{

class SpatialPannerComponent final : public juce::Component
{
public:
    explicit SpatialPannerComponent(bool sevenOne = false);
    void setPosition(float x, float y);
    juce::Point<float> getPosition() const noexcept { return position; }
    void setIntensityGraphVisible(bool visible);
    void setTheme(const ThemeContext& theme);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    bool sevenOne;
    bool showIntensity = true;
    juce::Point<float> position { 0.62f, 0.34f };
    ThemeContext theme;
    juce::Rectangle<int> padBounds;
};

} // namespace wjn::common
