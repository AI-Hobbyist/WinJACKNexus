#include "EqCurveEditor.h"

#include <cmath>

namespace wjn::common
{

EqCurveEditor::EqCurveEditor()
{
    bands = { Band { 80.0f, 2.5f, 0.7f, juce::Colour(0xff4bb7ff) },
              Band { 620.0f, -3.0f, 1.4f, juce::Colour(0xfff0c84b) },
              Band { 2400.0f, 1.5f, 1.1f, juce::Colour(0xff9bdf73) },
              Band { 10000.0f, 3.0f, 0.8f, juce::Colour(0xffff7b9a) } };
}

void EqCurveEditor::setBands(std::array<Band, 4> newBands) { bands = newBands; repaint(); }
void EqCurveEditor::setTheme(const ThemeContext& newTheme) { theme = newTheme; repaint(); }
void EqCurveEditor::setBandChangeCallback(std::function<void(int, const Band&)> callback) { bandChangeCallback = std::move(callback); }

void EqCurveEditor::paint(juce::Graphics& g)
{
    if (getWidth() < 80 || getHeight() < 80)
        return;

    const auto outer = getLocalBounds().reduced(8).toFloat();
    const auto graph = graphBounds();
    g.setColour(theme.colour("darkCanvas"));
    g.fillRoundedRectangle(outer, 5.0f);
    g.setColour(theme.colour("border"));
    g.drawRoundedRectangle(outer, 5.0f, 1.0f);

    g.setColour(theme.colour("border").withAlpha(0.75f));
    for (int i = 0; i <= 8; ++i)
        g.drawVerticalLine(juce::roundToInt(graph.getX() + graph.getWidth() * i / 8.0f), graph.getY(), graph.getBottom());
    for (int i = 0; i <= 6; ++i)
        g.drawHorizontalLine(juce::roundToInt(graph.getY() + graph.getHeight() * i / 6.0f), graph.getX(), graph.getRight());

    for (const auto& band : bands)
    {
        const auto centre = juce::Point<float>(frequencyToX(band.frequency), gainToY(band.gain));
        const auto safeQ = juce::jmax(0.01f, band.q);
        const auto halfWidthInOctaves = juce::jmax(0.20f, 1.8f / safeQ);
        const auto leftFrequency = juce::jmax(20.0f, band.frequency * std::pow(2.0f, -halfWidthInOctaves));
        const auto rightFrequency = juce::jmin(20000.0f, band.frequency * std::pow(2.0f, halfWidthInOctaves));
        const auto leftX = frequencyToX(leftFrequency);
        const auto rightX = frequencyToX(rightFrequency);
        g.setColour(band.colour.withAlpha(0.58f));
        const float dashLengths[] { 3.0f, 3.0f };
        g.drawDashedLine(juce::Line<float>(leftX, centre.y, rightX, centre.y),
                 dashLengths, 2, 1.0f, 1.5f);
    }

    juce::Path response;
    for (int pixel = 0; pixel <= juce::roundToInt(graph.getWidth()); ++pixel)
    {
        const auto frequency = std::pow(10.0f, std::log10(20.0f)
            + static_cast<float>(pixel) / graph.getWidth() * (std::log10(20000.0f) - std::log10(20.0f)));
        float totalGain = 0.0f;
        for (const auto& band : bands)
        {
            const auto distance = std::log2(frequency / band.frequency);
            const auto width = juce::jmax(0.20f, 1.8f / juce::jmax(0.01f, band.q));
            totalGain += band.gain * std::exp(-0.5f * (distance / width) * (distance / width));
        }
        const auto point = juce::Point<float>(graph.getX() + static_cast<float>(pixel),
                                               graph.getCentreY() - totalGain / 12.0f * graph.getHeight() / 2.0f);
        pixel == 0 ? response.startNewSubPath(point) : response.lineTo(point);
    }
    g.setColour(juce::Colour(0xff8de3ff));
    g.strokePath(response, juce::PathStrokeType(2.2f));

    for (const auto& band : bands)
    {
        const auto centre = juce::Point<float>(frequencyToX(band.frequency), gainToY(band.gain));
        g.setColour(theme.colour("darkCanvas"));
        g.fillEllipse(centre.x - 7.0f, centre.y - 7.0f, 14.0f, 14.0f);
        g.setColour(band.colour);
        g.drawEllipse(centre.x - 6.0f, centre.y - 6.0f, 12.0f, 12.0f, 2.0f);
    }

    g.setColour(theme.colour("secondaryText"));
    g.setFont(systemUiFont(8.0f, juce::Font::bold));
    g.drawText("20 Hz", juce::Rectangle<int>(juce::roundToInt(graph.getX()), juce::roundToInt(graph.getBottom() - 15.0f), 52, 12), juce::Justification::left);
    g.drawText("20 kHz", juce::Rectangle<int>(juce::roundToInt(graph.getRight() - 52.0f), juce::roundToInt(graph.getBottom() - 15.0f), 52, 12), juce::Justification::right);
    g.drawText("+12 dB", juce::Rectangle<int>(juce::roundToInt(outer.getRight() - 54.0f), juce::roundToInt(outer.getY() + 4.0f), 50, 12), juce::Justification::right);
    g.drawText("-12 dB", juce::Rectangle<int>(juce::roundToInt(outer.getRight() - 54.0f), juce::roundToInt(outer.getBottom() - 16.0f), 50, 12), juce::Justification::right);
}

void EqCurveEditor::mouseDown(const juce::MouseEvent& event)
{
    const auto position = event.position;
    for (int index = 0; index < static_cast<int>(bands.size()); ++index)
        if (position.getDistanceFrom({ frequencyToX(bands[static_cast<size_t>(index)].frequency), gainToY(bands[static_cast<size_t>(index)].gain) }) < 14.0f)
        {
            draggedBand = index;
            return;
        }
}

void EqCurveEditor::mouseDrag(const juce::MouseEvent& event) { updateDraggedBand(event.position); }
void EqCurveEditor::mouseUp(const juce::MouseEvent&) { draggedBand = -1; }

juce::Rectangle<float> EqCurveEditor::graphBounds() const
{
    return getLocalBounds().reduced(28, 24).toFloat();
}

float EqCurveEditor::frequencyToX(float frequency) const
{
    const auto graph = graphBounds();
    const auto normalised = (std::log10(juce::jlimit(20.0f, 20000.0f, frequency)) - std::log10(20.0f))
        / (std::log10(20000.0f) - std::log10(20.0f));
    return graph.getX() + normalised * graph.getWidth();
}

float EqCurveEditor::gainToY(float gain) const
{
    const auto graph = graphBounds();
    return graph.getCentreY() - juce::jlimit(-12.0f, 12.0f, gain) / 12.0f * graph.getHeight() / 2.0f;
}

void EqCurveEditor::updateDraggedBand(juce::Point<float> position)
{
    if (draggedBand < 0)
        return;
    auto graph = graphBounds();
    const auto x = juce::jlimit(graph.getX(), graph.getRight(), position.x);
    const auto y = juce::jlimit(graph.getY(), graph.getBottom(), position.y);
    auto& band = bands[static_cast<size_t>(draggedBand)];
    band.frequency = std::pow(10.0f, std::log10(20.0f) + (x - graph.getX()) / graph.getWidth() * (std::log10(20000.0f) - std::log10(20.0f)));
    band.gain = juce::jlimit(-12.0f, 12.0f, (graph.getCentreY() - y) / (graph.getHeight() / 2.0f) * 12.0f);
    repaint();
    if (bandChangeCallback != nullptr)
        bandChangeCallback(draggedBand, band);
}

} // namespace wjn::common