#include "MeterHistoryChart.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace wjn::common
{

MeterHistoryChart::MeterHistoryChart() = default;

void MeterHistoryChart::setTitle(juce::String newTitle) { title = std::move(newTitle); repaint(); }
void MeterHistoryChart::setSubtitle(juce::String newSubtitle) { subtitle = std::move(newSubtitle); repaint(); }
void MeterHistoryChart::setSeries(std::vector<Series> newSeries) { series = std::move(newSeries); repaint(); }
void MeterHistoryChart::setTimeLabels(juce::StringArray newLabels) { timeLabels = std::move(newLabels); repaint(); }
void MeterHistoryChart::setValueRange(juce::String newTopLabel, juce::String newBottomLabel)
{
    topValueLabel = std::move(newTopLabel);
    bottomValueLabel = std::move(newBottomLabel);
    repaint();
}
void MeterHistoryChart::setSecondaryValueRange(juce::String newTopLabel, juce::String newBottomLabel)
{
    secondaryTopValueLabel = std::move(newTopLabel);
    secondaryBottomValueLabel = std::move(newBottomLabel);
    repaint();
}
void MeterHistoryChart::setScanMode(bool shouldScan) { scanMode = shouldScan; repaint(); }
void MeterHistoryChart::setScanPosition(float normalisedPosition) { scanPosition = juce::jlimit(0.0f, 1.0f, normalisedPosition); repaint(); }
void MeterHistoryChart::setHistoryAvailable(bool shouldShowHistory) { historyAvailable = shouldShowHistory; repaint(); }
void MeterHistoryChart::setTheme(const ThemeContext& newTheme) { theme = newTheme; repaint(); }

void MeterHistoryChart::paint(juce::Graphics& g)
{
    g.fillAll(theme.colour("darkCanvas"));
    if (getWidth() < 120 || getHeight() < 100)
        return;

    const auto bounds = getLocalBounds().reduced(8);
    g.setColour(theme.colour("darkCanvas"));
    g.fillRect(bounds);
    g.setColour(theme.colour("border"));
    g.drawRect(bounds, 1);
    const auto graph = bounds.reduced(42, 76);
    if (graph.getWidth() <= 0 || graph.getHeight() <= 0)
        return;
    g.setColour(theme.colour("border").withAlpha(0.72f));
    for (int index = 0; index <= 6; ++index)
    {
        const auto x = graph.getX() + graph.getWidth() * index / 6;
        const auto y = graph.getY() + graph.getHeight() * index / 6;
        g.drawVerticalLine(x, graph.getY(), graph.getBottom());
        g.drawHorizontalLine(y, graph.getX(), graph.getRight());
    }

    g.setColour(theme.colour("secondaryText"));
    g.setFont(juce::FontOptions(8.0f));
    g.drawText(topValueLabel, juce::Rectangle<int>(bounds.getX(), graph.getY() - 6, 38, 12), juce::Justification::centredRight);
    g.drawText(bottomValueLabel, juce::Rectangle<int>(bounds.getX(), graph.getBottom() - 6, 38, 12), juce::Justification::centredRight);
    g.drawText(secondaryTopValueLabel, juce::Rectangle<int>(graph.getRight() + 4, graph.getY() - 6, 30, 12), juce::Justification::centredLeft);
    g.drawText(secondaryBottomValueLabel, juce::Rectangle<int>(graph.getRight() + 4, graph.getBottom() - 6, 30, 12), juce::Justification::centredLeft);

    const std::array<std::array<float, 8>, 3> demoTraces { {
        { 0.52f, 0.69f, 0.43f, 0.58f, 0.37f, 0.72f, 0.48f, 0.61f },
        { 0.31f, 0.26f, 0.34f, 0.29f, 0.36f, 0.25f, 0.32f, 0.28f },
        { 0.14f, 0.16f, 0.13f, 0.17f, 0.15f, 0.18f, 0.14f, 0.16f }
    } };
    const std::array<juce::Colour, 3> demoColours { juce::Colour(0xff8de3ff), juce::Colour(0xffe0bf35), juce::Colour(0xff42d96f) };
    const std::array<const char*, 3> demoNames { "PEAK", "COMP", "GATE" };
    auto legendX = graph.getX();
    for (size_t legendIndex = 0; legendIndex < demoNames.size(); ++legendIndex)
    {
        g.setColour(demoColours[legendIndex]);
        g.fillEllipse(static_cast<float>(legendX), static_cast<float>(bounds.getY() + 14), 6.0f, 6.0f);
        g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
        g.drawText(demoNames[legendIndex], juce::Rectangle<int>(legendX + 10, bounds.getY() + 10, 44, 14), juce::Justification::centredLeft);
        legendX += 62;
    }
    for (size_t traceIndex = 0; traceIndex < demoTraces.size(); ++traceIndex)
    {
        g.setColour(demoColours[traceIndex]);
        const auto& trace = demoTraces[traceIndex];
        for (size_t pointIndex = 1; pointIndex < trace.size(); ++pointIndex)
        {
            const auto previousX = graph.getX() + juce::roundToInt(static_cast<float>(pointIndex - 1) / static_cast<float>(trace.size() - 1) * graph.getWidth());
            const auto currentX = graph.getX() + juce::roundToInt(static_cast<float>(pointIndex) / static_cast<float>(trace.size() - 1) * graph.getWidth());
            const auto previousY = graph.getBottom() - juce::roundToInt(trace[pointIndex - 1] * graph.getHeight());
            const auto currentY = graph.getBottom() - juce::roundToInt(trace[pointIndex] * graph.getHeight());
            g.drawLine(static_cast<float>(previousX), static_cast<float>(previousY), static_cast<float>(currentX), static_cast<float>(currentY), 1.5f);
        }
    }
}

} // namespace wjn::common