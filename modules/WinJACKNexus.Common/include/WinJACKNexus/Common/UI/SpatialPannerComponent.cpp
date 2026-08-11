#include "SpatialPannerComponent.h"

namespace wjn::common
{

SpatialPannerComponent::SpatialPannerComponent(bool isSevenOne) : sevenOne(isSevenOne) {}
void SpatialPannerComponent::setPosition(float x, float y)
{
    position = { juce::jlimit(0.0f, 1.0f, x), juce::jlimit(0.0f, 1.0f, y) };
    repaint();
}
void SpatialPannerComponent::setIntensityGraphVisible(bool visible) { showIntensity = visible; repaint(); }
void SpatialPannerComponent::setTheme(const ThemeContext& newTheme) { theme = newTheme; repaint(); }

void SpatialPannerComponent::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().reduced(8);
    padBounds = area.withSizeKeepingCentre(juce::jmin(area.getWidth(), area.getHeight()),
                                            juce::jmin(area.getWidth(), area.getHeight()));
    g.setColour(theme.colour("darkCanvas"));
    g.fillRoundedRectangle(padBounds.toFloat(), 6.0f);
    g.setColour(theme.colour("border"));
    g.drawRoundedRectangle(padBounds.toFloat(), 6.0f, 1.0f);
    auto field = padBounds.reduced(24);
    g.setColour(theme.colour("border"));
    g.drawEllipse(field.toFloat(), 1.0f);
    g.drawLine(static_cast<float>(field.getCentreX()), static_cast<float>(field.getY()),
               static_cast<float>(field.getCentreX()), static_cast<float>(field.getBottom()), 1.0f);
    g.drawLine(static_cast<float>(field.getX()), static_cast<float>(field.getCentreY()),
               static_cast<float>(field.getRight()), static_cast<float>(field.getCentreY()), 1.0f);
    if (showIntensity)
    {
        g.setColour(theme.colour("accent").withAlpha(0.18f));
        g.fillEllipse(field.toFloat().reduced(12));
    }
    const auto point = juce::Point<float>(static_cast<float>(field.getX()) + position.x * field.getWidth(),
                                          static_cast<float>(field.getY()) + position.y * field.getHeight());
    g.setColour(theme.colour("accent"));
    g.fillEllipse(point.x - 8.0f, point.y - 8.0f, 16.0f, 16.0f);
    g.setColour(theme.colour("primaryText"));
    g.drawEllipse(point.x - 11.0f, point.y - 11.0f, 22.0f, 22.0f, 1.0f);
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText(sevenOne ? "7.1" : "5.1", padBounds.removeFromTop(20), juce::Justification::centred);
}

void SpatialPannerComponent::mouseDown(const juce::MouseEvent& event) { mouseDrag(event); }
void SpatialPannerComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (!padBounds.contains(event.getPosition()))
        return;
    auto field = padBounds.reduced(24);
    setPosition(static_cast<float>(event.x - field.getX()) / static_cast<float>(juce::jmax(1, field.getWidth())),
                static_cast<float>(event.y - field.getY()) / static_cast<float>(juce::jmax(1, field.getHeight())));
}

} // namespace wjn::common
