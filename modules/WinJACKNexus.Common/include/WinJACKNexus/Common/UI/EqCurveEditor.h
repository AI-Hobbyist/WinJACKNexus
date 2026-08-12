#pragma once

#include <WinJACKNexus/Common/UI/ThemeContext.h>

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::common
{

class EqCurveEditor final : public juce::Component
{
public:
    struct Band
    {
        float frequency = 1000.0f;
        float gain = 0.0f;
        float q = 1.0f;
        juce::Colour colour { 0xff8de3ff };
    };

    EqCurveEditor();

    void setBands(std::array<Band, 4> newBands);
    const std::array<Band, 4>& getBands() const noexcept { return bands; }
    void setTheme(const ThemeContext& newTheme);
    void setBandChangeCallback(std::function<void(int, const Band&)> callback);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> graphBounds() const;
    float frequencyToX(float frequency) const;
    float gainToY(float gain) const;
    void updateDraggedBand(juce::Point<float> position);

    ThemeContext theme;
    std::array<Band, 4> bands;
    std::function<void(int, const Band&)> bandChangeCallback;
    int draggedBand = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqCurveEditor)
};

} // namespace wjn::common