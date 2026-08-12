#include "SegmentedMeterControl.h"

namespace wjn::common
{

SegmentedMeterControl::SegmentedMeterControl(juce::String initialLabel, juce::Colour initialAccent)
    : label(std::move(initialLabel)), accent(initialAccent) {}

void SegmentedMeterControl::setLevel(float newLevel) { level = juce::jlimit(0.0f, 1.0f, newLevel); repaint(); }
void SegmentedMeterControl::setHold(float newHold) { hold = juce::jlimit(0.0f, 1.0f, newHold); repaint(); }
void SegmentedMeterControl::setTheme(const ThemeContext& newTheme) { theme = newTheme; repaint(); }

void SegmentedMeterControl::paint(juce::Graphics& g)
{
    if (getWidth() <= 16 || getHeight() <= 22)
        return;

    auto bounds = getLocalBounds().reduced(8, 6);
    auto text = bounds.removeFromBottom(16);
    auto rail = bounds.withSizeKeepingCentre(16, bounds.getHeight());
    g.setColour(theme.colour("darkCanvas"));
    g.fillRect(rail);

    const auto fillHeight = juce::roundToInt(level * static_cast<float>(rail.getHeight()));
    auto fill = rail.withTrimmedTop(rail.getHeight() - fillHeight);
    g.setColour(accent);
    g.fillRect(fill);

    const auto holdY = rail.getBottom() - juce::roundToInt(hold * static_cast<float>(rail.getHeight()));
    g.setColour(theme.colour("primaryText"));
    g.fillRect(juce::Rectangle<int>(rail.getX() - 2, holdY, rail.getWidth() + 4, 2));
    g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
    g.drawText(label, text, juce::Justification::centred);
}

} // namespace wjn::common