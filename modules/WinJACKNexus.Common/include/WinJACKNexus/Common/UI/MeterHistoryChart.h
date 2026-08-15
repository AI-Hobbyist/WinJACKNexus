#pragma once

#include <WinJACKNexus/Common/UI/ThemeContext.h>

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::common
{

class MeterHistoryChart final : public juce::Component
{
public:
    struct Series
    {
        juce::String name;
        juce::Colour colour;
        std::vector<float> values;
        bool visible = true;
        float minimum = -60.0f;
        float maximum = 12.0f;
    };

    struct ReferenceLine
    {
        juce::String label;
        juce::Colour colour;
        float value = 0.0f;
        float minimum = -60.0f;
        float maximum = 12.0f;
    };

    MeterHistoryChart();

    void setTitle(juce::String newTitle);
    void setSubtitle(juce::String newSubtitle);
    void setSeries(std::vector<Series> newSeries);
    void setReferenceLines(std::vector<ReferenceLine> newLines);
    void setTimeLabels(juce::StringArray newLabels);
    void setValueRange(juce::String newTopLabel, juce::String newBottomLabel);
    void setSecondaryValueRange(juce::String newTopLabel, juce::String newBottomLabel);
    void setValueScale(float newMinimum, float newMaximum, juce::String newUnit);
    void setSecondaryValueScale(float newMinimum, float newMaximum, juce::String newUnit);
    void setScanPosition(float normalisedPosition);
    void setScanCallback(std::function<void(float)> callback);
    void setHistoryAvailable(bool shouldShowHistory);
    void setTheme(const ThemeContext& newTheme);

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> getGraphBounds() const noexcept;
    void updateScanPosition(float x);

    juce::String title { "Meter History" };
    juce::String subtitle;
    std::vector<Series> series;
    juce::StringArray timeLabels;
    juce::String topValueLabel { "+12 dB" };
    juce::String bottomValueLabel { "-60 dB" };
    juce::String secondaryTopValueLabel { "72" };
    juce::String secondaryBottomValueLabel { "0" };
    float valueMinimum = -60.0f;
    float valueMaximum = 12.0f;
    float secondaryValueMinimum = 0.0f;
    float secondaryValueMaximum = 72.0f;
    juce::String valueUnit { "dB" };
    juce::String secondaryValueUnit { "LU" };
    ThemeContext theme;
    float scanPosition = 0.64f;
    bool scanActive = false;
    bool historyAvailable = true;
    std::vector<ReferenceLine> referenceLines;
    std::function<void(float)> scanCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MeterHistoryChart)
};

} // namespace wjn::common