#pragma once

#include "ThemeContext.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::common
{

class NexusLabel final : public juce::Label
{
public:
    NexusLabel (juce::String componentName = {}, juce::String labelText = {});

    void setTheme (const ThemeContext& newTheme);
    void setHeadingStyle();
};

class NexusButton final : public juce::TextButton
{
public:
    explicit NexusButton (juce::String buttonText = {});

    void setTheme (const ThemeContext& newTheme);
};

class NexusTextEditor final : public juce::TextEditor
{
public:
    explicit NexusTextEditor (juce::String componentName = {});

    void setTheme (const ThemeContext& newTheme);
};

class NexusViewport final : public juce::Viewport
{
public:
    explicit NexusViewport (juce::String componentName = {});

    void setTheme (const ThemeContext& newTheme);
};

class NexusPanel : public juce::Component
{
public:
    NexusPanel();

    void setTheme (const ThemeContext& newTheme);
    void paint (juce::Graphics& g) override;

private:
    ThemeContext theme;
};

class NexusTabbedComponent final : public juce::TabbedComponent
{
public:
    explicit NexusTabbedComponent (juce::TabbedButtonBar::Orientation orientation
                                   = juce::TabbedButtonBar::TabsAtTop);

    void setTheme (const ThemeContext& newTheme);
};

class NexusPopupMenu final : public juce::PopupMenu
{
public:
    using Options = juce::PopupMenu::Options;

    NexusPopupMenu() = default;
};

} // namespace wjn::common