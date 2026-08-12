#pragma once

#include <WinJACKNexus/Common/UI/ThemeContext.h>

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::common
{

class ToggleBadgeControl final : public juce::Component
{
public:
    ToggleBadgeControl(juce::String text = {}, bool active = false);

    void setText(juce::String newText);
    void setToggleState(bool shouldBeActive, juce::NotificationType notification = juce::sendNotificationAsync);
    bool getToggleState() const noexcept { return active; }
    void setAccent(juce::Colour newAccent);
    void setTheme(const ThemeContext& newTheme);
    void setStateChangeCallback(std::function<void(bool)> callback);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    juce::String text;
    ThemeContext theme;
    juce::Colour accent { 0xff8de3ff };
    bool active = false;
    std::function<void(bool)> stateChangeCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToggleBadgeControl)
};

} // namespace wjn::common