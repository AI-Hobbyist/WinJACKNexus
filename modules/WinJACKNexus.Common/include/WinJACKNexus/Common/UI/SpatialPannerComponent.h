#pragma once

#include "ThemeContext.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace wjn::common
{

class SpatialPannerComponent final : public juce::Component
{
public:
    explicit SpatialPannerComponent(bool sevenOne = false);
    void setPosition(float x, float y);
    juce::Point<float> getPosition() const noexcept { return position; }
    void setIntensityGraphVisible(bool visible);
    void setCompactPreview(bool compact);
    void setPositionChangedCallback(std::function<void(juce::Point<float>)> callback);
    void setDoubleClickCallback(std::function<void()> callback);
    void setTheme(const ThemeContext& theme);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    bool sevenOne;
    bool showIntensity = true;
    bool compactPreview = false;
    juce::Point<float> position { 0.62f, 0.34f };
    ThemeContext theme;
    juce::Rectangle<int> padBounds;
    std::function<void(juce::Point<float>)> positionChangedCallback;
    std::function<void()> doubleClickCallback;
};

} // namespace wjn::common
