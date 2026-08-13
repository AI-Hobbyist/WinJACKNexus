#include "CommonControls.h"

#include "Theme.h"

namespace wjn::common
{
namespace
{

juce::Font defaultControlFont (float height)
{
    return juce::Font (juce::FontOptions (juce::Font::getSystemUIFontName(), height,
                                          juce::Font::plain));
}

} // namespace

NexusLabel::NexusLabel (juce::String componentName, juce::String labelText)
    : juce::Label (std::move (componentName), std::move (labelText))
{
    setFont (defaultControlFont (13.0f));
    setTheme (ThemeContext {});
}

void NexusLabel::setTheme (const ThemeContext& newTheme)
{
    setColour (juce::Label::textColourId, newTheme.colour ("primaryText"));
}

void NexusLabel::setHeadingStyle()
{
    setFont (defaultControlFont (15.0f).withStyle (juce::Font::bold));
}

NexusButton::NexusButton (juce::String buttonText)
    : juce::TextButton (buttonText)
{
    setTheme (ThemeContext {});
}

void NexusButton::setTheme (const ThemeContext& newTheme)
{
    setColour (juce::TextButton::buttonColourId, newTheme.colour ("rackPanel"));
    setColour (juce::TextButton::buttonOnColourId, newTheme.colour ("accent"));
    setColour (juce::TextButton::textColourOffId, newTheme.colour ("primaryText"));
    setColour (juce::TextButton::textColourOnId, newTheme.colour ("primaryText"));
}

NexusTextEditor::NexusTextEditor (juce::String componentName)
    : juce::TextEditor (componentName)
{
    setFont (defaultControlFont (14.0f));
    setJustification (juce::Justification::centred);
    setTheme (ThemeContext {});
}

void NexusTextEditor::setTheme (const ThemeContext& newTheme)
{
    setColour (juce::TextEditor::textColourId, newTheme.colour ("primaryText"));
    setColour (juce::TextEditor::backgroundColourId, newTheme.colour ("darkCanvas"));
    setColour (juce::TextEditor::outlineColourId, newTheme.colour ("border"));
    setColour (juce::TextEditor::focusedOutlineColourId, newTheme.colour ("accent"));
}

NexusViewport::NexusViewport (juce::String componentName)
    : juce::Viewport (componentName)
{
    setTheme (ThemeContext {});
}

void NexusViewport::setTheme (const ThemeContext& newTheme)
{
    setColour (juce::ScrollBar::backgroundColourId, newTheme.colour ("darkCanvas"));
    setColour (juce::ScrollBar::thumbColourId, newTheme.colour ("border"));
}

NexusPanel::NexusPanel()
{
    setOpaque (true);
    setTheme (ThemeContext {});
}

void NexusPanel::setTheme (const ThemeContext& newTheme)
{
    theme = newTheme;
    repaint();
}

void NexusPanel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto radius = theme.metric ("panelRadius", 4.0f);
    g.setColour (theme.colour ("rackPanel"));
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (theme.colour ("border"));
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
}

NexusTabbedComponent::NexusTabbedComponent (juce::TabbedButtonBar::Orientation orientation)
    : juce::TabbedComponent (orientation)
{
    setTabBarDepth (36);
    setOutline (0);
    setTheme (ThemeContext {});
}

void NexusTabbedComponent::setTheme (const ThemeContext& newTheme)
{
    setColour (juce::TabbedComponent::backgroundColourId, newTheme.colour ("darkCanvas"));
    setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
    getTabbedButtonBar().setColour (juce::TabbedButtonBar::tabTextColourId,
                                    newTheme.colour ("secondaryText"));
    getTabbedButtonBar().setColour (juce::TabbedButtonBar::frontTextColourId,
                                    newTheme.colour ("primaryText"));
    getTabbedButtonBar().setColour (juce::TabbedButtonBar::frontOutlineColourId,
                                    newTheme.colour ("accent"));
}

} // namespace wjn::common