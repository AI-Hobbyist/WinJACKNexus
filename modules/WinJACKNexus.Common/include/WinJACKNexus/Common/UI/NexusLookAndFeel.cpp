#include "NexusLookAndFeel.h"

#include "Theme.h"

namespace wjn::common
{
namespace
{

juce::Font systemFont (float height, int style = juce::Font::plain)
{
    return juce::Font (juce::FontOptions (juce::Font::getSystemUIFontName(), height, style));
}

} // namespace

NexusLookAndFeel::NexusLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::darkCanvas);
    setColour (juce::TabbedButtonBar::tabTextColourId, theme::secondaryText);
    setColour (juce::TabbedButtonBar::frontTextColourId, theme::primaryText);
    setColour (juce::TabbedButtonBar::frontOutlineColourId, theme::activeTab);
    setColour (juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedComponent::backgroundColourId, theme::darkCanvas);
    setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::buttonColourId, theme::rackPanel);
    setColour (juce::TextButton::textColourOffId, theme::primaryText);
    setColour (juce::TextEditor::backgroundColourId, theme::rackPanel);
    setColour (juce::TextEditor::textColourId, theme::primaryText);
    setColour (juce::TextEditor::outlineColourId, theme::border);
    setColour (juce::TextEditor::focusedOutlineColourId, theme::activeTab);
    setColour (juce::Label::textColourId, theme::primaryText);
}

void NexusLookAndFeel::setTheme(const ThemeContext& newTheme)
{
    theme = newTheme;
    setColour (juce::ResizableWindow::backgroundColourId, theme.colour("darkCanvas"));
    setColour (juce::TabbedButtonBar::tabTextColourId, theme.colour("secondaryText"));
    setColour (juce::TabbedButtonBar::frontTextColourId, theme.colour("primaryText"));
    setColour (juce::TabbedButtonBar::frontOutlineColourId, theme.colour("accent"));
    setColour (juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedComponent::backgroundColourId, theme.colour("darkCanvas"));
    setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::buttonColourId, theme.colour("rackPanel"));
    setColour (juce::TextButton::textColourOffId, theme.colour("primaryText"));
    setColour (juce::Label::textColourId, theme.colour("primaryText"));
    setColour (juce::TextEditor::backgroundColourId, theme.colour("rackPanel"));
    setColour (juce::TextEditor::textColourId, theme.colour("primaryText"));
    setColour (juce::TextEditor::outlineColourId, theme.colour("border"));
    setColour (juce::TextEditor::focusedOutlineColourId, theme.colour("accent"));
}

void NexusLookAndFeel::setFontManager(const FontManager* newFontManager) noexcept
{
    fontManager = newFontManager;
}

juce::Font NexusLookAndFeel::getLcdFont (float height, int style) const
{
    if (fontManager != nullptr)
        return fontManager->getFont ("common:lcd-zpix", height, style);

    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height,
                                          style));
}

juce::Font NexusLookAndFeel::getPopupMenuFont()
{
    return systemFont (13.0f);
}

juce::Font NexusLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return systemFont (juce::jmin (14.0f, juce::jmax (11.0f, buttonHeight * 0.45f)));
}

juce::Font NexusLookAndFeel::getTabButtonFont (juce::TabBarButton&, float height)
{
    const auto fontHeight = juce::jmin (14.0f, juce::jmax (11.0f, height * 0.48f));
    return systemFont (fontHeight);
}

void NexusLookAndFeel::drawTabButton (juce::TabBarButton& button, juce::Graphics& g,
                                      bool isMouseOver, bool isMouseDown)
{
    auto bounds = button.getLocalBounds().toFloat();
    const auto active = button.isFrontTab();
    const auto background = active ? theme.colour ("rackPanel") : theme.colour ("darkCanvas");

    g.setColour (background);
    g.fillRect (bounds);

    if (isMouseOver || isMouseDown)
    {
        g.setColour (theme.colour ("primaryText").withAlpha (isMouseDown ? 0.12f : 0.06f));
        g.fillRect (bounds);
    }

    if (active)
    {
        g.setColour (theme.colour ("accent"));
        g.fillRect (bounds.removeFromBottom (3.0f));
    }

    const auto textArea = button.getTextArea();
    const auto font = getTabButtonFont (button, static_cast<float> (textArea.getHeight()));
    g.setColour ((active ? theme.colour ("primaryText") : theme.colour ("secondaryText"))
                     .withAlpha (button.isEnabled() ? 1.0f : 0.35f));
    g.setFont (font);
    g.drawFittedText (button.getButtonText().trim(), textArea.getX(), textArea.getY(),
                      textArea.getWidth(), textArea.getHeight(), juce::Justification::centred, 1);
}

void NexusLookAndFeel::drawTabbedButtonBarBackground (juce::TabbedButtonBar& bar,
                                                       juce::Graphics& g)
{
    g.setColour (theme.colour ("darkCanvas"));
    g.fillRect (bar.getLocalBounds());
}

void NexusLookAndFeel::drawTabAreaBehindFrontButton (juce::TabbedButtonBar&,
                                                      juce::Graphics& g, int width, int height)
{
    g.setColour (theme.colour ("darkCanvas"));
    g.fillRect (0, 0, width, height);
}

} // namespace wjn::common
