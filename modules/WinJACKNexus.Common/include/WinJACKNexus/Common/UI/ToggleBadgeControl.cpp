#include "ToggleBadgeControl.h"

namespace wjn::common
{

ToggleBadgeControl::ToggleBadgeControl(juce::String initialText, bool initialActive)
    : text(std::move(initialText)), active(initialActive)
{
    setWantsKeyboardFocus(true);
}

void ToggleBadgeControl::setText(juce::String newText) { text = std::move(newText); repaint(); }

void ToggleBadgeControl::setToggleState(bool shouldBeActive, juce::NotificationType notification)
{
    if (active == shouldBeActive)
        return;
    active = shouldBeActive;
    repaint();
    if (notification != juce::dontSendNotification && stateChangeCallback != nullptr)
        stateChangeCallback(active);
}

void ToggleBadgeControl::setAccent(juce::Colour newAccent) { accent = newAccent; repaint(); }
void ToggleBadgeControl::setTheme(const ThemeContext& newTheme) { theme = newTheme; repaint(); }
void ToggleBadgeControl::setSwitchStyle(bool shouldUseSwitchStyle)
{
    switchStyle = shouldUseSwitchStyle;
    repaint();
}
void ToggleBadgeControl::setStateChangeCallback(std::function<void(bool)> callback) { stateChangeCallback = std::move(callback); }
void ToggleBadgeControl::setContextMenuCallback(std::function<void()> callback) { contextMenuCallback = std::move(callback); }

void ToggleBadgeControl::paint(juce::Graphics& g)
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;
    const auto bounds = getLocalBounds().toFloat();

    if (switchStyle)
    {
        const auto track = bounds.reduced(0.0f, bounds.getHeight() * 0.22f);
        g.setColour(active ? accent.withAlpha(0.72f) : theme.colour("border"));
        g.fillRoundedRectangle(track, track.getHeight() * 0.5f);
        g.setColour(active ? accent : theme.colour("secondaryText"));
        const auto thumbSize = juce::jmax(6.0f, track.getHeight() - 4.0f);
        const auto thumbX = active ? track.getRight() - thumbSize - 2.0f : track.getX() + 2.0f;
        g.fillEllipse(thumbX, track.getCentreY() - thumbSize * 0.5f, thumbSize, thumbSize);
        return;
    }

    const auto fill = active ? accent.withAlpha(0.22f) : theme.colour("darkCanvas");
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(active ? accent : theme.colour("border"));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    g.setColour(active ? accent : theme.colour("secondaryText"));
    g.setFont(systemUiFont(9.0f, juce::Font::bold));
    g.drawText(text, getLocalBounds(), juce::Justification::centred);
}

void ToggleBadgeControl::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
    {
        if (contextMenuCallback != nullptr)
            contextMenuCallback();
        return;
    }

    setToggleState(! active, juce::sendNotificationSync);
}

} // namespace wjn::common