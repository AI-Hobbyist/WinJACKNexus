#include "OnOffSwitch.h"

#include "Theme.h"

namespace wjn::common
{

OnOffSwitch::OnOffSwitch()
{
    setWantsKeyboardFocus(true);
    setRepaintsOnMouseActivity(true);
}

void OnOffSwitch::setTheme(const ThemeContext& newTheme)
{
    theme = newTheme;
    repaint();
}

void OnOffSwitch::setToggleState(bool shouldBeOn, juce::NotificationType notification)
{
    if (isOn == shouldBeOn)
        return;

    isOn = shouldBeOn;
    repaint();

    if (notification != juce::dontSendNotification && stateChangeCallback != nullptr)
        stateChangeCallback(isOn);
}

bool OnOffSwitch::getToggleState() const noexcept
{
    return isOn;
}

void OnOffSwitch::setStateChangeCallback(std::function<void(bool)> callback)
{
    stateChangeCallback = std::move(callback);
}

void OnOffSwitch::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    const auto trackHeight = juce::jmin(18.0f, bounds.getHeight());
    const auto track = juce::Rectangle<float>(bounds.getX(),
                                              bounds.getCentreY() - trackHeight * 0.5f,
                                              bounds.getWidth(),
                                              trackHeight);
    const auto accent = theme.colour("accent");
    const auto trackColour = isOn ? accent : theme.colour("border");

    graphics.setColour(trackColour);
    graphics.fillRoundedRectangle(track, trackHeight * 0.5f);

    const auto thumbDiameter = juce::jmin(track.getHeight() + 4.0f, bounds.getHeight());
    const auto thumbX = isOn ? track.getRight() - thumbDiameter : track.getX();
    const auto thumb = juce::Rectangle<float>(thumbX,
                                               bounds.getCentreY() - thumbDiameter * 0.5f,
                                               thumbDiameter,
                                               thumbDiameter);
    graphics.setColour(theme.colour("primaryText"));
    graphics.fillEllipse(thumb);
    graphics.setColour(theme.colour("darkCanvas").withAlpha(0.35f));
    graphics.drawEllipse(thumb, 1.0f);

    if (hasKeyboardFocus(true))
    {
        graphics.setColour(accent.withAlpha(0.8f));
        graphics.drawRoundedRectangle(bounds, 2.0f, 1.0f);
    }
}

void OnOffSwitch::resized()
{
    repaint();
}

bool OnOffSwitch::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey)
    {
        toggle(juce::sendNotificationSync);
        return true;
    }

    return false;
}

void OnOffSwitch::mouseDown(const juce::MouseEvent&)
{
    grabKeyboardFocus();
    toggle(juce::sendNotificationSync);
}

void OnOffSwitch::toggle(juce::NotificationType notification)
{
    setToggleState(! isOn, notification);
}

} // namespace wjn::common