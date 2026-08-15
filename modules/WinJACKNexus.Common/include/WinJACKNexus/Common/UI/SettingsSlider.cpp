#include "SettingsSlider.h"

#include "Theme.h"

namespace wjn::common
{

namespace
{

constexpr float valueTextWidth = 76.0f;

}

SettingsSlider::SettingsSlider()
{
    setWantsKeyboardFocus(true);
    setRepaintsOnMouseActivity(true);
}

void SettingsSlider::setTheme(const ThemeContext& newTheme)
{
    theme = newTheme;
    repaint();
}

void SettingsSlider::setRange(double newMinimum, double newMaximum, double newInterval)
{
    minimum = juce::jmin(newMinimum, newMaximum);
    maximum = juce::jmax(newMinimum, newMaximum);
    interval = juce::jmax(0.0, newInterval);
    setValue(value, juce::dontSendNotification);
}

void SettingsSlider::setValue(double newValue, juce::NotificationType notification)
{
    const auto clamped = juce::jlimit(minimum, maximum, newValue);
    const auto quantized = interval > 0.0
        ? minimum + std::round((clamped - minimum) / interval) * interval
        : clamped;
    const auto nextValue = juce::jlimit(minimum, maximum, quantized);

    if (juce::approximatelyEqual(value, nextValue))
        return;

    value = nextValue;
    repaint();
    notifyValueChanged(notification);
}

double SettingsSlider::getValue() const noexcept
{
    return value;
}

void SettingsSlider::setTextValueSuffix(juce::String suffix)
{
    textValueSuffix = std::move(suffix);
    repaint();
}

void SettingsSlider::setValueChangeCallback(std::function<void(double)> callback)
{
    valueChangeCallback = std::move(callback);
}

void SettingsSlider::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat().reduced(2.0f, 4.0f);
    const auto trackBounds = getTrackBounds();
    const auto trackHeight = juce::jmin(4.0f, bounds.getHeight());
    const auto track = juce::Rectangle<float>(trackBounds.getX(),
                                              trackBounds.getCentreY() - trackHeight * 0.5f,
                                              trackBounds.getWidth(),
                                              trackHeight);
    const auto accent = theme.colour("accent");
    const auto ratio = maximum > minimum
        ? static_cast<float>((value - minimum) / (maximum - minimum))
        : 0.0f;
    const auto thumbWidth = juce::jmin(10.0f, bounds.getWidth());
    const auto thumbX = track.getX() + ratio * (track.getWidth() - thumbWidth);
    const auto thumb = juce::Rectangle<float>(thumbX,
                                               bounds.getY(),
                                               thumbWidth,
                                               bounds.getHeight());

    graphics.setColour(theme.colour("border"));
    graphics.fillRoundedRectangle(track, trackHeight * 0.5f);
    graphics.setColour(accent);
    graphics.fillRoundedRectangle(track.withWidth(thumbX + thumbWidth * 0.5f - track.getX()),
                                  trackHeight * 0.5f);
    graphics.setColour(theme.colour("primaryText"));
    graphics.fillRoundedRectangle(thumb, 2.0f);

    graphics.setFont(juce::FontOptions(11.0f));
    graphics.drawFittedText(juce::String(value, interval < 1.0 ? 1 : 0) + textValueSuffix,
                            bounds.withLeft(trackBounds.getRight() + 4.0f).toNearestInt(),
                            juce::Justification::centredRight, 1);

    if (hasKeyboardFocus(true))
    {
        graphics.setColour(accent.withAlpha(0.8f));
        graphics.drawRoundedRectangle(bounds, 2.0f, 1.0f);
    }
}

juce::Rectangle<float> SettingsSlider::getTrackBounds() const noexcept
{
    const auto bounds = getLocalBounds().toFloat().reduced(2.0f, 4.0f);
    return bounds.withRight(juce::jmax(bounds.getX() + 1.0f,
                                       bounds.getRight() - valueTextWidth));
}

bool SettingsSlider::keyPressed(const juce::KeyPress& key)
{
    const auto step = interval > 0.0 ? interval : (maximum - minimum) / 100.0;
    if (key == juce::KeyPress::leftKey)
    {
        setValue(value - step, juce::sendNotificationSync);
        return true;
    }
    if (key == juce::KeyPress::rightKey)
    {
        setValue(value + step, juce::sendNotificationSync);
        return true;
    }
    if (key == juce::KeyPress::homeKey)
    {
        setValue(minimum, juce::sendNotificationSync);
        return true;
    }
    if (key == juce::KeyPress::endKey)
    {
        setValue(maximum, juce::sendNotificationSync);
        return true;
    }

    return false;
}

void SettingsSlider::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    dragStartX = static_cast<float>(event.position.x);
    dragStartValue = value;
    setValueFromPosition(static_cast<float>(event.position.x), juce::sendNotificationSync);
}

void SettingsSlider::mouseDrag(const juce::MouseEvent& event)
{
    const auto trackWidth = juce::jmax(1.0f, getTrackBounds().getWidth());
    const auto delta = static_cast<float>(event.position.x) - dragStartX;
    const auto valueDelta = static_cast<double>(delta / trackWidth) * (maximum - minimum);
    setValue(dragStartValue + valueDelta, juce::sendNotificationSync);
}

void SettingsSlider::setValueFromPosition(float x, juce::NotificationType notification)
{
    const auto trackBounds = getTrackBounds();
    const auto trackWidth = juce::jmax(1.0f, trackBounds.getWidth());
    const auto ratio = juce::jlimit(0.0f, 1.0f, (x - trackBounds.getX()) / trackWidth);
    setValue(minimum + static_cast<double>(ratio) * (maximum - minimum), notification);
}

void SettingsSlider::notifyValueChanged(juce::NotificationType notification)
{
    if (notification != juce::dontSendNotification && valueChangeCallback != nullptr)
        valueChangeCallback(value);
}

} // namespace wjn::common