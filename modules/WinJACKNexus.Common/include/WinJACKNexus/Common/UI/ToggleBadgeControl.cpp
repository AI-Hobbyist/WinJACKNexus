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
void ToggleBadgeControl::setStateChangeCallback(std::function<void(bool)> callback) { stateChangeCallback = std::move(callback); }

void ToggleBadgeControl::paint(juce::Graphics& g)
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;
    const auto bounds = getLocalBounds().toFloat();
    const auto fill = active ? accent.withAlpha(0.22f) : theme.colour("darkCanvas");
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(active ? accent : theme.colour("border"));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    g.setColour(active ? accent : theme.colour("secondaryText"));
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    g.drawText(text, getLocalBounds(), juce::Justification::centred);
}

void ToggleBadgeControl::mouseDown(const juce::MouseEvent&)
{
    setToggleState(! active, juce::sendNotificationSync);
}

} // namespace wjn::common