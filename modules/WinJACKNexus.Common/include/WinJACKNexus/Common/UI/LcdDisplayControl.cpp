#include "LcdDisplayControl.h"

namespace wjn::common
{

LcdDisplayControl::LcdDisplayControl() = default;

void LcdDisplayControl::setContentPainter(ContentPainter newContentPainter)
{
    contentPainter = std::move(newContentPainter);
    repaint();
}
void LcdDisplayControl::setAccent(juce::Colour newAccent) { accent = newAccent; repaint(); }
void LcdDisplayControl::setTheme(const ThemeContext& newTheme) { theme = newTheme; repaint(); }

void LcdDisplayControl::paint(juce::Graphics& g)
{
    if (getWidth() < 80 || getHeight() < 48)
        return;

    auto outer = getLocalBounds().reduced(1);
    g.setColour(juce::Colour(0xff151b18));
    g.fillRect(outer);
    g.setColour(juce::Colour(0xff303b32));
    g.drawRect(outer, 1);
    g.setColour(juce::Colour(0xff080c09));
    g.fillRect(outer.reduced(3));

    const auto screen = outer.reduced(7, 6);
    g.setColour(juce::Colour(0xff8eae7b));
    g.fillRect(screen);
    g.setColour(juce::Colour(0xff172519));
    g.drawRect(screen, 1);

    if (contentPainter != nullptr)
    {
        const auto lcdFont = juce::Font(juce::FontOptions()
                                            .withName(juce::Font::getDefaultMonospacedFontName())
                                            .withPointHeight(10.0f));
        contentPainter(g, screen.toFloat().reduced(6.0f, 5.0f), lcdFont, juce::Colour(0xff142216));
    }
}

} // namespace wjn::common