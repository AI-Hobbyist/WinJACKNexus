#pragma once

#include <WinJACKNexus/Common/UI/ThemeContext.h>

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::common
{

class RouteSelectorControl final : public juce::Component
{
public:
    RouteSelectorControl(juce::String label = "IN", juce::String value = {});

    void setLabel(juce::String newLabel);
    void setOptions(juce::StringArray newOptions);
    void setSelectedIndex(int newIndex);
    int getSelectedIndex() const noexcept { return selectedIndex; }
    juce::String getSelectedValue() const;
    void setAccent(juce::Colour newAccent);
    void setTheme(const ThemeContext& newTheme);
    void setSelectionChangeCallback(std::function<void(int, const juce::String&)> callback);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    juce::String label;
    juce::String fallbackValue;
    juce::StringArray options;
    ThemeContext theme;
    juce::Colour accent { 0xff8de3ff };
    int selectedIndex = -1;
    std::function<void(int, const juce::String&)> selectionChangeCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RouteSelectorControl)
};

} // namespace wjn::common