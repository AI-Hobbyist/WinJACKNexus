#include "MidiLed.h"

#include "Theme.h"

namespace wjn::common
{

MidiLed::MidiLed()
{
    setOpaque (false);
    startTimerHz (25);
}

void MidiLed::trigger()
{
    level = 1.0f;
    repaint();
}

void MidiLed::timerCallback()
{
    level *= 0.76f;
    if (level < 0.01f)
        level = 0.0f;
    repaint();
}

void MidiLed::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);
    const auto glow = theme::ledMidiActivity;
    const auto intensity = juce::jmax (0.12f, level);

    g.setColour (theme::ledOff);
    g.fillEllipse (bounds);
    g.setColour (glow.withAlpha (0.16f * intensity));
    g.fillEllipse (bounds.expanded (6.0f));
    g.setColour (glow.withAlpha (0.34f * intensity));
    g.fillEllipse (bounds.expanded (3.0f));
    g.setColour (glow.withAlpha (0.28f + 0.72f * intensity));
    g.fillEllipse (bounds.reduced (2.0f));
    g.setColour (juce::Colours::white.withAlpha (0.54f * intensity));
    g.fillEllipse (bounds.reduced (5.0f).translated (-2.0f, -2.0f));
}

} // namespace wjn::common
