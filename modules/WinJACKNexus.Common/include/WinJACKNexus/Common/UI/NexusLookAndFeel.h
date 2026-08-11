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

    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

private:
    ThemeContext theme;
    const FontManager* fontManager = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NexusLookAndFeel)
};

} // namespace wjn::common
