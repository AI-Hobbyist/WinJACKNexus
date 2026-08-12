#include "RotaryControl.h"

#include <algorithm>
#include <cmath>

namespace wjn::common
{

RotaryControl::RotaryControl(juce::String initialLabel, juce::String initialSuffix)
    : label(std::move(initialLabel)), suffix(std::move(initialSuffix))
{
    setWantsKeyboardFocus(true);
}

void RotaryControl::setRange(double newMinimum, double newMaximum)
{
    minimum = std::min(newMinimum, newMaximum);
    maximum = std::max(newMinimum, newMaximum);
    setValue(value, juce::dontSendNotification);
}

void RotaryControl::setValue(double newValue, juce::NotificationType notification)
{
    const auto clamped = juce::jlimit(minimum, maximum, newValue);
    if (juce::approximatelyEqual(clamped, value))
        return;

    value = clamped;
    repaint();
    notifyValueChanged(notification);
}

void RotaryControl::setLabel(juce::String newLabel) { label = std::move(newLabel); repaint(); }
void RotaryControl::setSuffix(juce::String newSuffix) { suffix = std::move(newSuffix); repaint(); }
void RotaryControl::setAccent(juce::Colour newAccent) { accent = newAccent; repaint(); }
void RotaryControl::setTheme(const ThemeContext& newTheme) { theme = newTheme; repaint(); }
void RotaryControl::setValueChangeCallback(std::function<void(double)> callback) { valueChangeCallback = std::move(callback); }

void RotaryControl::paint(juce::Graphics& g)
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    const auto dialBounds = juce::Rectangle<float>(getLocalBounds().withSizeKeepingCentre(44, 44).withY(2).toFloat());
    const auto centre = dialBounds.getCentre();
    const auto normalised = maximum > minimum ? static_cast<float>((value - minimum) / (maximum - minimum)) : 0.0f;
    const auto angle = juce::jmap(juce::jlimit(0.0f, 1.0f, normalised), -2.35f, 2.35f);

    g.setColour(theme.colour("darkCanvas"));
    g.fillEllipse(dialBounds);
    g.setColour(theme.colour("secondaryText"));
    g.drawEllipse(dialBounds, 1.2f);
    g.setColour(accent);
    g.drawLine(centre.x, centre.y, centre.x + std::sin(angle) * 15.0f,
               centre.y - std::cos(angle) * 15.0f, 2.0f);

    g.setColour(theme.colour("secondaryText"));
    g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
    g.drawText(label, getLocalBounds().withY(48).withHeight(12), juce::Justification::centred);
    g.setColour(accent);
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    g.drawText(juce::String(value, maximum - minimum < 20.0 ? 2 : 1) + suffix,
               getLocalBounds().withY(60).withHeight(16), juce::Justification::centred);
}

void RotaryControl::mouseDown(const juce::MouseEvent& event)
{
    valueOnMouseDown = value;
    dragStartY = event.position.y;
}

void RotaryControl::mouseDrag(const juce::MouseEvent& event)
{
    updateFromDrag(dragStartY - event.position.y);
}

void RotaryControl::mouseDoubleClick(const juce::MouseEvent&)
{
    setValue(minimum, juce::sendNotificationSync);
}

void RotaryControl::updateFromDrag(float distance)
{
    const auto delta = static_cast<double>(distance) / 150.0 * (maximum - minimum);
    setValue(valueOnMouseDown + delta, juce::sendNotificationSync);
}

void RotaryControl::notifyValueChanged(juce::NotificationType notification)
{
    if (notification != juce::dontSendNotification && valueChangeCallback != nullptr)
        valueChangeCallback(value);
}

} // namespace wjn::common