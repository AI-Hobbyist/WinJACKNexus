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
void MeterHistoryChart::setReferenceLines(std::vector<ReferenceLine> newLines)
{
    referenceLines = std::move(newLines);
    repaint();
}
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
void MeterHistoryChart::setValueScale(float newMinimum, float newMaximum, juce::String newUnit)
{
    valueMinimum = juce::jmin(newMinimum, newMaximum);
    valueMaximum = juce::jmax(newMinimum, newMaximum);
    valueUnit = std::move(newUnit);
    repaint();
}
void MeterHistoryChart::setSecondaryValueScale(float newMinimum, float newMaximum, juce::String newUnit)
{
    secondaryValueMinimum = juce::jmin(newMinimum, newMaximum);
    secondaryValueMaximum = juce::jmax(newMinimum, newMaximum);
    secondaryValueUnit = std::move(newUnit);
    repaint();
}
void MeterHistoryChart::setScanPosition(float normalisedPosition) { scanPosition = juce::jlimit(0.0f, 1.0f, normalisedPosition); repaint(); }
void MeterHistoryChart::setScanCallback(std::function<void(float)> callback)
{
    scanCallback = std::move(callback);
}
void MeterHistoryChart::setHistoryAvailable(bool shouldShowHistory) { historyAvailable = shouldShowHistory; repaint(); }
void MeterHistoryChart::setTheme(const ThemeContext& newTheme) { theme = newTheme; repaint(); }

juce::Rectangle<int> MeterHistoryChart::getGraphBounds() const noexcept
{
    return getLocalBounds().reduced(8).reduced(42, 76);
}

void MeterHistoryChart::updateScanPosition(float x)
{
    const auto graph = getGraphBounds();
    if (! graph.contains(juce::roundToInt(x), graph.getCentreY()))
        return;

    scanActive = true;
    setScanPosition(static_cast<float>(x - graph.getX())
                    / static_cast<float>(juce::jmax(1, graph.getWidth())));
    if (scanCallback != nullptr)
        scanCallback(scanPosition);
}

void MeterHistoryChart::mouseMove(const juce::MouseEvent& event)
{
    updateScanPosition(event.position.x);
}

void MeterHistoryChart::mouseDrag(const juce::MouseEvent& event)
{
    updateScanPosition(event.position.x);
}

void MeterHistoryChart::mouseExit(const juce::MouseEvent&)
{
    if (! scanActive)
        return;

    scanActive = false;
    repaint();
    if (scanCallback != nullptr)
        scanCallback(-1.0f);
}

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
    const auto graph = getGraphBounds();
    if (graph.getWidth() <= 0 || graph.getHeight() <= 0)
        return;
    g.setColour(theme.colour("border").withAlpha(0.72f));
    for (int index = 0; index <= 6; ++index)
    {
        const auto x = graph.getX() + graph.getWidth() * index / 6;
        const auto y = graph.getY() + graph.getHeight() * index / 6;
        g.drawVerticalLine (x, static_cast<float> (graph.getY()), static_cast<float> (graph.getBottom()));
        g.drawHorizontalLine (y, static_cast<float> (graph.getX()), static_cast<float> (graph.getRight()));
    }

    g.setColour(theme.colour("secondaryText"));
    g.setFont(systemUiFont(8.0f));
    const auto formatScaleValue = [] (float value, const juce::String& unit)
    {
        auto text = juce::String(value, std::abs(value - std::round(value)) > 0.01f ? 1 : 0);
        if (value > 0.0f)
            text = "+" + text;
        return text + " " + unit;
    };
    for (int index = 0; index <= 6; ++index)
    {
        const auto ratio = static_cast<float>(index) / 6.0f;
        const auto value = valueMaximum - ratio * (valueMaximum - valueMinimum);
        const auto secondaryValue = secondaryValueMaximum
                                   - ratio * (secondaryValueMaximum - secondaryValueMinimum);
        const auto y = graph.getY() + juce::roundToInt(ratio * graph.getHeight()) - 6;
        g.drawText(formatScaleValue(value, valueUnit),
                   juce::Rectangle<int>(bounds.getX(), y, 40, 12), juce::Justification::centredRight);
        g.drawText(formatScaleValue(secondaryValue, secondaryValueUnit),
                   juce::Rectangle<int>(graph.getRight() + 4, y, 42, 12), juce::Justification::centredLeft);
    }

    auto legendX = graph.getX();
    for (const auto& item : series)
    {
        if (! item.visible)
            continue;
        g.setColour(item.colour);
        g.fillEllipse(static_cast<float>(legendX), static_cast<float>(bounds.getY() + 14), 6.0f, 6.0f);
        g.setFont(systemUiFont(8.0f, juce::Font::bold));
        const auto labelWidth = juce::jmax(42, item.name.length() * 5);
        g.drawText(item.name, juce::Rectangle<int>(legendX + 10, bounds.getY() + 10,
                                                    labelWidth, 14), juce::Justification::centredLeft);
        legendX += labelWidth + 20;
    }
    for (const auto& line : referenceLines)
    {
        const auto ratio = line.maximum > line.minimum
            ? juce::jlimit(0.0f, 1.0f, (line.value - line.minimum) / (line.maximum - line.minimum))
            : 0.0f;
        const auto y = graph.getBottom() - juce::roundToInt(ratio * graph.getHeight());
        juce::Graphics::ScopedSaveState state (g);
        g.setColour(line.colour);
        g.setOpacity(0.8f);
        for (int x = graph.getX(); x < graph.getRight(); x += 8)
            g.fillRect(x, y, juce::jmin(5, graph.getRight() - x), 1);
        g.setFont(systemUiFont(8.0f));
        g.drawText(line.label, graph.getRight() - 100, y - 12, 96, 12, juce::Justification::centredRight);
    }
    for (const auto& item : series)
    {
        if (! item.visible || item.values.size() < 2 || item.maximum <= item.minimum)
            continue;

        g.setColour(item.colour);
        const auto valueToY = [&item, &graph] (float value)
        {
            const auto ratio = juce::jlimit(0.0f, 1.0f,
                (value - item.minimum) / (item.maximum - item.minimum));
            return graph.getBottom() - juce::roundToInt(ratio * graph.getHeight());
        };
        for (size_t pointIndex = 1; pointIndex < item.values.size(); ++pointIndex)
        {
            const auto previousX = graph.getX() + juce::roundToInt(
                static_cast<float>(pointIndex - 1) / static_cast<float>(item.values.size() - 1) * graph.getWidth());
            const auto currentX = graph.getX() + juce::roundToInt(
                static_cast<float>(pointIndex) / static_cast<float>(item.values.size() - 1) * graph.getWidth());
            g.drawLine(static_cast<float>(previousX), static_cast<float>(valueToY(item.values[pointIndex - 1])),
                       static_cast<float>(currentX), static_cast<float>(valueToY(item.values[pointIndex])), 1.5f);
        }
    }

    if (scanActive && historyAvailable)
    {
        const auto x = graph.getX() + juce::roundToInt(scanPosition * graph.getWidth());
        g.setColour(theme.colour("accent"));
        g.drawVerticalLine (x, static_cast<float> (graph.getY()), static_cast<float> (graph.getBottom()));
    }
}

} // namespace wjn::common