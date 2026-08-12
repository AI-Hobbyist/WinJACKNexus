#include "TransferCurveEditor.h"

namespace wjn::common
{

TransferCurveEditor::TransferCurveEditor() = default;

void TransferCurveEditor::setThreshold(float newValue) { threshold = juce::jlimit(0.0f, 1.0f, newValue); repaint(); }
void TransferCurveEditor::setRange(float newValue) { range = juce::jlimit(0.0f, 1.0f, newValue); repaint(); }
void TransferCurveEditor::setTheme(const ThemeContext& newTheme) { theme = newTheme; repaint(); }
void TransferCurveEditor::setValueChangeCallback(std::function<void(float, float)> callback) { valueChangeCallback = std::move(callback); }

void TransferCurveEditor::paint(juce::Graphics& g)
{
    if (getWidth() < 100 || getHeight() < 90)
        return;

    const auto outer = getLocalBounds().reduced(8);
    auto plot = outer.reduced(22, 18);
    plot.removeFromBottom(34);
    g.setColour(theme.colour("darkCanvas"));
    g.fillRoundedRectangle(outer.toFloat(), 5.0f);
    g.setColour(theme.colour("border"));
    g.drawRoundedRectangle(outer.toFloat(), 5.0f, 1.0f);
    g.setColour(theme.colour("border").withAlpha(0.75f));
    for (int i = 0; i <= 6; ++i)
    {
        g.drawVerticalLine(plot.getX() + i * plot.getWidth() / 6, static_cast<float>(plot.getY()), static_cast<float>(plot.getBottom()));
        g.drawHorizontalLine(plot.getY() + i * plot.getHeight() / 6, static_cast<float>(plot.getX()), static_cast<float>(plot.getRight()));
    }
    g.setColour(juce::Colour(0xff49718a));
    g.drawLine(static_cast<float>(plot.getX()), static_cast<float>(plot.getBottom()), static_cast<float>(plot.getRight()), static_cast<float>(plot.getY()), 1.0f);

    const auto thresholdX = plot.getX() + juce::roundToInt(threshold * plot.getWidth());
    const auto thresholdY = plot.getBottom() - juce::roundToInt(threshold * plot.getHeight());
    juce::Path compression;
    compression.startNewSubPath(static_cast<float>(plot.getX()), static_cast<float>(plot.getBottom()));
    compression.lineTo(static_cast<float>(thresholdX - 16), static_cast<float>(thresholdY + 16));
    compression.cubicTo(static_cast<float>(thresholdX - 2), static_cast<float>(thresholdY + 2), static_cast<float>(thresholdX + 14), static_cast<float>(thresholdY - 4), static_cast<float>(plot.getRight()), static_cast<float>(plot.getY() + plot.getHeight() * 0.28f));
    g.setColour(juce::Colour(0xff8de3ff));
    g.strokePath(compression, juce::PathStrokeType(2.2f));

    juce::Path gate;
    gate.startNewSubPath(static_cast<float>(plot.getX()), static_cast<float>(plot.getBottom()));
    gate.lineTo(static_cast<float>(plot.getX() + plot.getWidth() * 0.24f), static_cast<float>(plot.getBottom()));
    gate.cubicTo(static_cast<float>(plot.getX() + plot.getWidth() * 0.31f), static_cast<float>(plot.getBottom() - 6), static_cast<float>(plot.getX() + plot.getWidth() * 0.38f), static_cast<float>(plot.getBottom() - plot.getHeight() * 0.31f), static_cast<float>(thresholdX), static_cast<float>(thresholdY + 10));
    g.setColour(juce::Colour(0xffd8df39));
    g.strokePath(gate, juce::PathStrokeType(1.7f));
    g.setColour(theme.colour("primaryText"));
    g.fillRect(juce::Rectangle<int>(thresholdX - 3, thresholdY - 3, 6, 6));

    g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xff8de3ff));
    g.drawText("THRESH", juce::Rectangle<int>(outer.getX() + 8, outer.getBottom() - 28, 48, 14), juce::Justification::left);
    g.setColour(juce::Colour(0xffd8df39));
    g.drawText("RANGE", juce::Rectangle<int>(outer.getCentreX(), outer.getBottom() - 28, 44, 14), juce::Justification::left);
}

void TransferCurveEditor::mouseDown(const juce::MouseEvent& event)
{
    const auto plot = getLocalBounds().reduced(30, 26);
    const auto thresholdPoint = juce::Point<float>(plot.getX() + threshold * plot.getWidth(), plot.getBottom() - threshold * plot.getHeight());
    draggingThreshold = event.position.getDistanceFrom(thresholdPoint) < 16.0f;
    draggingRange = ! draggingThreshold && event.position.x > plot.getCentreX();
    updateFromPosition(event.position);
}

void TransferCurveEditor::mouseDrag(const juce::MouseEvent& event) { updateFromPosition(event.position); }

void TransferCurveEditor::updateFromPosition(juce::Point<float> position)
{
    if (! draggingThreshold && ! draggingRange)
        return;
    const auto plot = getLocalBounds().reduced(30, 26).toFloat();
    const auto normalisedX = juce::jlimit(0.0f, 1.0f, (position.x - plot.getX()) / plot.getWidth());
    const auto normalisedY = juce::jlimit(0.0f, 1.0f, (plot.getBottom() - position.y) / plot.getHeight());
    if (draggingThreshold)
        threshold = normalisedX;
    else
        range = normalisedY;
    repaint();
    if (valueChangeCallback != nullptr)
        valueChangeCallback(threshold, range);
}

} // namespace wjn::common