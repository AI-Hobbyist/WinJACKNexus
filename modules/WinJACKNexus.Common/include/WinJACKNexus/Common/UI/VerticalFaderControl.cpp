#include "VerticalFaderControl.h"

namespace wjn::common
{

VerticalFaderControl::VerticalFaderControl() { setWantsKeyboardFocus(true); }

void VerticalFaderControl::setRange(float newMinimum, float newMaximum)
{
    minimum = juce::jmin(newMinimum, newMaximum);
    maximum = juce::jmax(newMinimum, newMaximum);
    setValue(value, juce::dontSendNotification);
}

void VerticalFaderControl::setValue(float newValue, juce::NotificationType notification)
{
    const auto clamped = juce::jlimit(minimum, maximum, newValue);
    if (juce::approximatelyEqual(clamped, value))
        return;
    value = clamped;
    repaint();
    if (notification != juce::dontSendNotification && valueChangeCallback != nullptr)
        valueChangeCallback(value);
}

void VerticalFaderControl::setAccent(juce::Colour newAccent) { accent = newAccent; repaint(); }
void VerticalFaderControl::setTheme(const ThemeContext& newTheme) { theme = newTheme; repaint(); }
void VerticalFaderControl::setCompactStyle(bool shouldUseCompactStyle) { compactStyle = shouldUseCompactStyle; repaint(); }
void VerticalFaderControl::setLabel(juce::String newLabel) { label = std::move(newLabel); repaint(); }
void VerticalFaderControl::setValueLabelVisible(bool shouldShowValueLabel)
{
    valueLabelVisible = shouldShowValueLabel;
    repaint();
}
void VerticalFaderControl::setValueChangeCallback(std::function<void(float)> callback) { valueChangeCallback = std::move(callback); }
void VerticalFaderControl::setContextMenuCallback(std::function<void()> callback) { contextMenuCallback = std::move(callback); }

void VerticalFaderControl::paint(juce::Graphics& g)
{
    if (getWidth() <= 0 || getHeight() <= 28)
        return;

    if (compactStyle)
    {
        auto slot = getLocalBounds().withWidth(8).withCentre({ getLocalBounds().getCentreX(), getLocalBounds().getCentreY() }).reduced(0, 14);
        g.setColour(juce::Colour(0xff101318));
        g.fillRoundedRectangle(slot.toFloat(), 4.0f);
        g.setColour(juce::Colour(0xff48515e));
        g.drawRoundedRectangle(slot.toFloat(), 4.0f, 1.0f);

        const auto normalised = maximum > minimum ? (value - minimum) / (maximum - minimum) : 0.0f;
        const auto capY = slot.getBottom() - juce::roundToInt(juce::jlimit(0.0f, 1.0f, normalised) * slot.getHeight());
        auto cap = juce::Rectangle<int>(getLocalBounds().getX(), capY - 8, getWidth(), 18);
        g.setColour(juce::Colour(0xffd7dde6));
        g.fillRoundedRectangle(cap.toFloat(), 4.0f);
        g.setColour(juce::Colour(0xff111418));
        g.drawRoundedRectangle(cap.toFloat(), 4.0f, 1.0f);

        if (valueLabelVisible)
        {
            g.setColour(juce::Colour(0xffc9d1da));
            g.setFont(systemUiFont(11.0f, juce::Font::bold));
            g.drawText(valueIsBeingAdjusted ? juce::String(value, 1) : label,
                       getLocalBounds().removeFromBottom(20), juce::Justification::centred);
        }
        return;
    }

    auto rail = getLocalBounds().reduced(10, 8).withSizeKeepingCentre(8, getHeight() - 28);
    g.setColour(theme.colour("darkCanvas"));
    g.fillRoundedRectangle(rail.toFloat(), 3.0f);
    g.setColour(theme.colour("secondaryText"));
    g.drawRoundedRectangle(rail.toFloat(), 3.0f, 1.0f);

    const auto normalised = maximum > minimum ? (value - minimum) / (maximum - minimum) : 0.0f;
    const auto thumbY = rail.getBottom() - juce::roundToInt(juce::jlimit(0.0f, 1.0f, normalised) * rail.getHeight());
    auto thumb = juce::Rectangle<int>(getWidth() / 2 - 18, thumbY - 9, 36, 18);
    g.setColour(accent);
    g.fillRoundedRectangle(thumb.toFloat(), 4.0f);
    g.setColour(theme.colour("darkCanvas"));
    g.drawRoundedRectangle(thumb.toFloat(), 4.0f, 1.0f);
    if (valueLabelVisible)
    {
        g.setColour(theme.colour("primaryText"));
        g.setFont(systemUiFont(9.0f, juce::Font::bold));
        g.drawText(valueIsBeingAdjusted ? juce::String(value, 1) : label,
                   getLocalBounds().removeFromBottom(16), juce::Justification::centred);
    }
}

void VerticalFaderControl::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
    {
        if (contextMenuCallback != nullptr)
            contextMenuCallback();
        return;
    }
    valueIsBeingAdjusted = true;
    repaint();
    updateFromPosition(event.position.y);
}
void VerticalFaderControl::mouseDrag(const juce::MouseEvent& event) { updateFromPosition(event.position.y); }

void VerticalFaderControl::mouseUp(const juce::MouseEvent&)
{
    valueIsBeingAdjusted = false;
    repaint();
}

void VerticalFaderControl::updateFromPosition(float y)
{
    const auto rail = getLocalBounds().reduced(10, 8).withSizeKeepingCentre(8, getHeight() - 28).toFloat();
    if (rail.getHeight() <= 0.0f)
        return;

    const auto normalised = juce::jlimit(0.0f, 1.0f, (rail.getBottom() - y) / rail.getHeight());
    setValue(minimum + normalised * (maximum - minimum), juce::sendNotificationSync);
}

} // namespace wjn::common