#include "NexusLookAndFeel.h"

#include "Theme.h"

namespace wjn::common
{

NexusLookAndFeel::NexusLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::darkCanvas);
    setColour (juce::TabbedButtonBar::tabTextColourId, theme::secondaryText);
    setColour (juce::TabbedButtonBar::frontTextColourId, theme::primaryText);
    setColour (juce::TabbedButtonBar::frontOutlineColourId, theme::activeTab);
    setColour (juce::TextButton::buttonColourId, theme::rackPanel);
    setColour (juce::TextButton::textColourOffId, theme::primaryText);
    setColour (juce::TextEditor::backgroundColourId, theme::rackPanel);
    setColour (juce::TextEditor::textColourId, theme::primaryText);
    setColour (juce::TextEditor::outlineColourId, theme::border);
    setColour (juce::TextEditor::focusedOutlineColourId, theme::activeTab);
    setColour (juce::Label::textColourId, theme::primaryText);
}

juce::Font NexusLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions()
                           .withName (juce::Font::getSystemUIFontName())
                           .withPointHeight (13.0f));
}

juce::Font NexusLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::FontOptions (juce::Font::getSystemUIFontName(),
                                          juce::jmin (14.0f, juce::jmax (11.0f, buttonHeight * 0.45f)),
                                          juce::Font::plain));
}

} // namespace wjn::common
