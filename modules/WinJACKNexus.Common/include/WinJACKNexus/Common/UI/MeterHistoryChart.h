#pragma once

#include <WinJACKNexus/Common/UI/ThemeContext.h>

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
    };

    MeterHistoryChart();

    void setTitle(juce::String newTitle);
    void setSubtitle(juce::String newSubtitle);
    void setSeries(std::vector<Series> newSeries);
    void setTimeLabels(juce::StringArray newLabels);
    void setValueRange(juce::String newTopLabel, juce::String newBottomLabel);
    void setSecondaryValueRange(juce::String newTopLabel, juce::String newBottomLabel);
    void setScanMode(bool shouldScan);
    void setScanPosition(float normalisedPosition);
    void setHistoryAvailable(bool shouldShowHistory);
    void setTheme(const ThemeContext& newTheme);

    void paint(juce::Graphics&) override;

private:
    juce::String title { "Meter History" };
    juce::String subtitle;
    std::vector<Series> series;
    juce::StringArray timeLabels;
    juce::String topValueLabel { "+12 dB" };
    juce::String bottomValueLabel { "-60 dB" };
    juce::String secondaryTopValueLabel { "72" };
    juce::String secondaryBottomValueLabel { "0" };
    ThemeContext theme;
    bool scanMode = false;
    float scanPosition = 0.64f;
    bool historyAvailable = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MeterHistoryChart)
};

} // namespace wjn::common