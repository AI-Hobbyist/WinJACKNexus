#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ThemeContext.h"

namespace wjn::common
{

class OnOffSwitch final : public juce::Component
{
public:
    OnOffSwitch();

    void setTheme(const ThemeContext& newTheme);
    void setToggleState(bool shouldBeOn, juce::NotificationType notification);
    bool getToggleState() const noexcept;
    void setStateChangeCallback(std::function<void(bool)> callback);

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void toggle(juce::NotificationType notification);

    ThemeContext theme;
    bool isOn = false;
    std::function<void(bool)> stateChangeCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OnOffSwitch)
};

} // namespace wjn::common