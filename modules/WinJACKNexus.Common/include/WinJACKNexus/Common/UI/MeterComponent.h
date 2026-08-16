#pragma once

#include "ThemeContext.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::common
{

class MeterComponent final : public juce::Component
{
public:
    enum class MeterType { decibels, truePeak, loudness, range };

    MeterComponent(juce::String label, MeterType type, float minimum = -60.0f,
                   float maximum = 12.0f, float barThickness = 0.0f);
    void setValue(float value);
    void resetValue(float value);
    float getValue() const noexcept;
    void setPeakHoldDuration(float seconds);
    void setPreset(float targetLufs, float toleranceLu, float truePeakMaxDbtp);
    void setBarThickness(float pixels) noexcept;
    void setTheme(const ThemeContext& theme);
    void paint(juce::Graphics&) override;

private:
    float minimum() const noexcept;
    float maximum() const noexcept;
    float clampValue(float value) const noexcept;
    juce::String label;
    MeterType type;
    float value;
    float heldValue;
    float minValue;
    float maxValue;
    float peakHoldSeconds = 2.0f;
    double peakHoldExpiryMs = 0.0;
    float targetLufs = -23.0f;
    float toleranceLu = 1.0f;
    float truePeakMaxDbtp = -1.0f;
    float barThickness = 0.0f;
    ThemeContext theme;
};

} // namespace wjn::common
