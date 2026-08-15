#include "AudioLed.h"

#include "Theme.h"

namespace wjn::common
{

AudioLed::AudioLed()
{
    setOpaque (false);
}

void AudioLed::setLevel (float newLevel, bool shouldClip)
{
    const auto nextLevel = juce::jlimit (0.0f, 1.0f, newLevel);
    const auto nextPeak = juce::jmax (peak, nextLevel);
    const auto nextPeakHoldMs = shouldClip ? 1500 : peakHoldMs;
    repaintPending = repaintPending || level != nextLevel || peak != nextPeak
                   || clipping != shouldClip || peakHoldMs != nextPeakHoldMs;
    level = nextLevel;
    peak = nextPeak;
    clipping = shouldClip;
    peakHoldMs = nextPeakHoldMs;
}

void AudioLed::update()
{
    const auto previousLevel = level;
    const auto previousPeak = peak;
    const auto previousPeakHoldMs = peakHoldMs;
    const auto previousClipping = clipping;
    level *= 0.86f;
    if (peakHoldMs > 0)
        peakHoldMs = juce::jmax (0, peakHoldMs - 50);
    else
    {
        clipping = false;
        peak *= 0.88f;
    }

    if (level < 0.01f)
        level = 0.0f;
    if (peak < 0.01f)
        peak = 0.0f;

    if (repaintPending || previousLevel != level || previousPeak != peak
        || previousPeakHoldMs != peakHoldMs || previousClipping != clipping)
    {
        repaintPending = false;
        repaint();
    }
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
