#include "AudioLed.h"

#include "Theme.h"

namespace wjn::common
{

AudioLed::AudioLed()
{
    setOpaque (false);
    startTimerHz (25);
}

void AudioLed::setLevel (float newLevel, bool shouldClip)
{
    level = juce::jlimit (0.0f, 1.0f, newLevel);
    peak = juce::jmax (peak, level);
    clipping = shouldClip;
    if (clipping)
        peakHoldMs = 1500;
}

void AudioLed::timerCallback()
{
    level *= 0.86f;
    if (peakHoldMs > 0)
        peakHoldMs = juce::jmax (0, peakHoldMs - 40);
    else
    {
        clipping = false;
        peak *= 0.88f;
    }

    if (level < 0.01f)
        level = 0.0f;
    if (peak < 0.01f)
        peak = 0.0f;
    repaint();
}

void AudioLed::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto diameter = juce::jmax (0.0f, juce::jmin (bounds.getWidth(), bounds.getHeight()) - 4.0f);
    bounds = bounds.withSizeKeepingCentre (diameter, diameter);
    const auto glow = clipping ? theme::ledClipping : theme::ledActiveGreen;
    const auto intensity = juce::jmax (0.18f, level);

    g.setColour (theme::ledOff);
    g.fillEllipse (bounds);

    if (intensity > 0.02f)
    {
        g.setColour (glow.withAlpha (0.16f * intensity));
        g.fillEllipse (bounds.expanded (5.0f));
        g.setColour (glow.withAlpha (0.32f * intensity));
        g.fillEllipse (bounds.expanded (2.5f));
    }

    g.setColour (glow.withAlpha (0.35f + 0.65f * intensity));
    g.fillEllipse (bounds.reduced (2.0f));
    g.setColour (juce::Colours::white.withAlpha (0.48f * intensity));
    g.fillEllipse (bounds.reduced (5.0f).translated (-2.0f, -2.0f));

}

} // namespace wjn::common
