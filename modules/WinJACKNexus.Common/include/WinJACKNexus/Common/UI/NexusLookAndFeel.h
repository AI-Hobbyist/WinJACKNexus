#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ThemeContext.h"
#include "FontManager.h"

namespace wjn::common
{

class NexusLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    NexusLookAndFeel();

    void setTheme(const ThemeContext& newTheme);
    void setFontManager(const FontManager* newFontManager) noexcept;

    juce::Font getLcdFont (float height, int style = juce::Font::plain) const;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getTabButtonFont (juce::TabBarButton&, float height) override;
    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool isMouseOver,
                        bool isMouseDown) override;
    void drawTabbedButtonBarBackground (juce::TabbedButtonBar&, juce::Graphics&) override;
    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&, int width,
                                       int height) override;

private:
    ThemeContext theme;
    const FontManager* fontManager = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NexusLookAndFeel)
};

} // namespace wjn::common
