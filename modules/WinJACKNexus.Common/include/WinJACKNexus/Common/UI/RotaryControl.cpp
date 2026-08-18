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
void RotaryControl::setCompactStyle(bool shouldUseCompactStyle) { compactStyle = shouldUseCompactStyle; repaint(); }
void RotaryControl::setValueTextFormatter(std::function<juce::String(double)> formatter)
{
    valueTextFormatter = std::move(formatter);
    repaint();
}
void RotaryControl::setValueChangeCallback(std::function<void(double)> callback) { valueChangeCallback = std::move(callback); }
void RotaryControl::setContextMenuCallback(std::function<void()> callback) { contextMenuCallback = std::move(callback); }

juce::String RotaryControl::valueText() const
{
    if (valueTextFormatter != nullptr)
        return valueTextFormatter(value);

    return juce::String(value, maximum - minimum < 20.0 ? 2 : 1) + suffix;
}

void RotaryControl::paint(juce::Graphics& g)
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    if (compactStyle)
    {
        const auto dialBounds = juce::Rectangle<float>(getLocalBounds().withSizeKeepingCentre(34, 34).withY(2).toFloat());
        const auto centre = dialBounds.getCentre();
        const auto normalised = maximum > minimum ? static_cast<float>((value - minimum) / (maximum - minimum)) : 0.0f;
        const auto angle = juce::jmap(juce::jlimit(0.0f, 1.0f, normalised), -2.35f, 2.35f);

        g.setColour(juce::Colour(0xff111418));
        g.fillEllipse(dialBounds);
        g.setColour(juce::Colour(0xff4c5664));
        g.drawEllipse(dialBounds, 1.2f);
        g.setColour(accent);
        g.drawLine(centre.x, centre.y,
                   centre.x + std::sin(angle) * 12.0f,
                   centre.y - std::cos(angle) * 12.0f,
                   2.0f);

        g.setColour(juce::Colour(0xffd6dde6));
        g.setFont(systemUiFont(8.0f, juce::Font::bold));
        const auto labelY = juce::jmin(getHeight() - 12, juce::roundToInt(dialBounds.getBottom()) + 3);
        g.drawText(valueIsBeingAdjusted ? valueText() : label,
               getLocalBounds().withY(labelY).withHeight(juce::jmax(0, getHeight() - labelY)),
               juce::Justification::centred);
        return;
    }

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
    g.setFont(systemUiFont(8.5f, juce::Font::bold));
    g.drawText(valueIsBeingAdjusted ? valueText() : label,
               getLocalBounds().withY(48).withHeight(12), juce::Justification::centred);
}

void RotaryControl::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
    {
        if (contextMenuCallback != nullptr)
            contextMenuCallback();
        return;
    }

    valueIsBeingAdjusted = true;
    repaint();
    valueOnMouseDown = value;
    dragStartY = event.position.y;
}

void RotaryControl::mouseDrag(const juce::MouseEvent& event)
{
    updateFromDrag(dragStartY - event.position.y);
}

void RotaryControl::mouseUp(const juce::MouseEvent&)
{
    valueIsBeingAdjusted = false;
    repaint();
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