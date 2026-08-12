#pragma once

#include <WinJACKNexus/Common/UI/ThemeContext.h>

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::common
{

class LcdDisplayControl final : public juce::Component
{
public:
    using ContentPainter = std::function<void(juce::Graphics&,
                                              juce::Rectangle<float>,
                                              const juce::Font&,
                                              juce::Colour)>;

    LcdDisplayControl();

    void setContentPainter(ContentPainter newContentPainter);
    void setAccent(juce::Colour newAccent);
    void setTheme(const ThemeContext& newTheme);

    void paint(juce::Graphics&) override;

private:
    ContentPainter contentPainter;
    juce::Colour accent { 0xff8de3ff };
    ThemeContext theme;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LcdDisplayControl)
};

} // namespace wjn::common