#include "HorizontalSliderControl.h"

namespace wjn::common
{

HorizontalSliderControl::HorizontalSliderControl() { setWantsKeyboardFocus(true); }

void HorizontalSliderControl::setRange(float newMinimum, float newMaximum)
{
    minimum = juce::jmin(newMinimum, newMaximum);
    maximum = juce::jmax(newMinimum, newMaximum);
    setValue(value, juce::dontSendNotification);
}

void HorizontalSliderControl::setValue(float newValue, juce::NotificationType notification)
{
    const auto clamped = juce::jlimit(minimum, maximum, newValue);
    if (juce::approximatelyEqual(clamped, value))
        return;

    value = clamped;
    repaint();
    if (notification != juce::dontSendNotification && valueChangeCallback != nullptr)
        valueChangeCallback(value);
}

void HorizontalSliderControl::setAccent(juce::Colour newAccent)
{
    accent = newAccent;
    repaint();
}

void HorizontalSliderControl::setTheme(const ThemeContext& newTheme)
{
    theme = newTheme;
    repaint();
}

void HorizontalSliderControl::setValueChangeCallback(std::function<void(float)> callback)
{
    valueChangeCallback = std::move(callback);
}

void HorizontalSliderControl::paint(juce::Graphics& g)
{
    auto rail = getLocalBounds().reduced(0, 6);
    if (rail.isEmpty())
        return;

    g.setColour(theme.colour("darkCanvas").isTransparent() ? juce::Colour(0xff101318)
                                                              : theme.colour("darkCanvas"));
    g.fillRect(rail);

    const auto normalised = maximum > minimum ? (value - minimum) / (maximum - minimum) : 0.0f;
    const auto thumbCentreX = rail.getX() + juce::roundToInt(juce::jlimit(0.0f, 1.0f, normalised)
                                                             * static_cast<float>(rail.getWidth()));
    auto thumb = juce::Rectangle<int>(thumbCentreX - 4, rail.getY() - 4, 8, rail.getHeight() + 8);
    g.setColour(accent);
    g.fillRoundedRectangle(thumb.toFloat(), 2.0f);
    g.setColour(theme.colour("border").isTransparent() ? juce::Colour(0xffd6dde6)
                                                         : theme.colour("border"));
    g.drawRoundedRectangle(thumb.toFloat(), 2.0f, 1.0f);
}

void HorizontalSliderControl::mouseDown(const juce::MouseEvent& event)
{
    updateFromPosition(event.position.x);
}

void HorizontalSliderControl::mouseDrag(const juce::MouseEvent& event)
{
    updateFromPosition(event.position.x);
}

void HorizontalSliderControl::updateFromPosition(float x)
{
    const auto rail = getLocalBounds().reduced(0, 6).toFloat();
    if (rail.getWidth() <= 8.0f)
        return;

    const auto normalised = juce::jlimit(0.0f, 1.0f, (x - rail.getX()) / rail.getWidth());
    setValue(minimum + normalised * (maximum - minimum), juce::sendNotificationSync);
}

} // namespace wjn::common