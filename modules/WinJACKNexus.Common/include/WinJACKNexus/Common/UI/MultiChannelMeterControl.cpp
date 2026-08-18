#include "MultiChannelMeterControl.h"

#include <algorithm>
#include <cmath>

namespace wjn::common
{

MultiChannelMeterControl::MultiChannelMeterControl(int visibleChannels)
{
    setChannelCount(visibleChannels);
}

void MultiChannelMeterControl::setChannelCount(int newChannelCount)
{
    channelCount = juce::jlimit(1, maxChannels, newChannelCount);
    repaint();
}

void MultiChannelMeterControl::setPeakDb(const std::array<float, maxChannels>& values)
{
    peakDb = values;
    repaint();
}

void MultiChannelMeterControl::setHoldDb(const std::array<float, maxChannels>& values)
{
    holdDb = values;
    repaint();
}

void MultiChannelMeterControl::setOverload(bool shouldShowOverload)
{
    overload = shouldShowOverload;
    repaint();
}

void MultiChannelMeterControl::setShowsOutput(bool shouldShowOutput, juce::NotificationType notification)
{
    if (showsOutput == shouldShowOutput)
        return;
    showsOutput = shouldShowOutput;
    repaint();
    if (notification != juce::dontSendNotification && sourceChangeCallback != nullptr)
        sourceChangeCallback(showsOutput);
}

void MultiChannelMeterControl::setAccent(juce::Colour newAccent)
{
    accent = newAccent;
    repaint();
}

void MultiChannelMeterControl::setTheme(const ThemeContext& newTheme)
{
    theme = newTheme;
    repaint();
}

void MultiChannelMeterControl::setSourceChangeCallback(std::function<void(bool)> callback)
{
    sourceChangeCallback = std::move(callback);
}

float MultiChannelMeterControl::scaleValueToNormalised(float value) const noexcept
{
    return juce::jlimit(0.0f, 1.0f, (value + 60.0f) / 60.0f);
}

void MultiChannelMeterControl::paintSegmentedBar(juce::Graphics& g, juce::Rectangle<int> bounds,
                                                 float value, std::array<float, 4> stops,
                                                 std::array<juce::Colour, 4> colours) const
{
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
        return;

    const auto fillHeight = juce::roundToInt(static_cast<float>(bounds.getHeight()) * value);
    const auto fill = bounds.withTrimmedTop(bounds.getHeight() - fillHeight);
    if (fill.isEmpty())
        return;

    auto segment = fill;
    for (int index = 0; index < 3; ++index)
    {
        const auto start = juce::jlimit(0.0f, 1.0f, stops[static_cast<size_t>(index)]);
        const auto end = juce::jlimit(start, 1.0f, stops[static_cast<size_t>(index + 1)]);
        const auto top = fill.getBottom() - juce::roundToInt(static_cast<float>(bounds.getHeight()) * end);
        const auto bottom = fill.getBottom() - juce::roundToInt(static_cast<float>(bounds.getHeight()) * start);
        auto area = juce::Rectangle<int>(fill.getX(), top, fill.getWidth(), bottom - top);
        if (! area.isEmpty())
        {
            g.setColour(colours[static_cast<size_t>(index)]);
            g.fillRect(area);
        }
    }
}

void MultiChannelMeterControl::paint(juce::Graphics& g)
{
    if (getWidth() < 28 || getHeight() < 96)
        return;

    auto peak = peakDb;
    auto hold = holdDb;
    if (! showsOutput)
    {
        for (int channel = 0; channel < channelCount; ++channel)
        {
            peak[static_cast<size_t>(channel)] -= 5.0f;
            hold[static_cast<size_t>(channel)] -= 5.0f;
        }
    }

    const auto green = theme.colour("meterNormal").isTransparent() ? juce::Colour(0xff42d96f) : theme.colour("meterNormal");
    const auto yellow = theme.colour("meterWarning").isTransparent() ? juce::Colour(0xffe0bf35) : theme.colour("meterWarning");
    const auto red = theme.colour("meterClipping").isTransparent() ? juce::Colour(0xffe34b4b) : theme.colour("meterClipping");
    const auto text = theme.colour("primaryText").isTransparent() ? juce::Colour(0xffc9d1da) : theme.colour("primaryText");

    if (channelCount > 2)
    {
        g.setColour(showsOutput ? green : accent);
        g.fillRect(getLocalBounds().removeFromTop(3));
        auto meterArea = getLocalBounds().reduced(1, 2);
        const auto cellWidth = meterArea.getWidth() / channelCount;
        for (int channel = 0; channel < channelCount; ++channel)
        {
            auto cell = meterArea.removeFromLeft(channel + 1 == channelCount ? meterArea.getWidth() : cellWidth);
            auto meter = cell;
            g.setColour(juce::Colour(0xff111418));
            g.fillRect(meter);
            paintSegmentedBar(g, meter,
                              scaleValueToNormalised(peak[static_cast<size_t>(channel)]),
                              { 0.0f, scaleValueToNormalised(-18.0f), scaleValueToNormalised(-6.0f), 1.0f },
                              { green, yellow, red, red });
            const auto holdY = meter.getBottom() - juce::roundToInt(static_cast<float>(meter.getHeight())
                                                                      * scaleValueToNormalised(hold[static_cast<size_t>(channel)]));
            g.setColour(overload ? juce::Colour(0xffff5b58) : text);
            g.fillRect(juce::Rectangle<int>(meter.getX(), holdY, meter.getWidth(), 1));
        }
        return;
    }

    auto title = getLocalBounds().removeFromTop(14);
    g.setColour(text);
    g.setFont(systemUiFont(8.0f, juce::Font::bold));
    g.drawText(showsOutput ? "OUT" : "IN", title, juce::Justification::centred);

    const std::array<const char*, maxChannels> labels { channelCount == 1 ? "M" : "L", "R", "C", "F", "s", "S", "r", "R" };
    auto meterArea = getLocalBounds().withTrimmedTop(14);
    std::array<juce::Rectangle<int>, maxChannels> cells {};
    const auto cellWidth = meterArea.getWidth() / channelCount;
    for (int channel = 0; channel < channelCount; ++channel)
        cells[static_cast<size_t>(channel)] = meterArea.removeFromLeft(channel + 1 == channelCount ? meterArea.getWidth() : cellWidth);

    for (int channel = 0; channel < channelCount; ++channel)
    {
        auto cell = cells[static_cast<size_t>(channel)].reduced(1, 0);
        auto label = cell.removeFromTop(10);
        auto valueBox = cell.removeFromBottom(18).reduced(0, 2);
        auto meter = cell.withSizeKeepingCentre(juce::jmin(14, juce::jmax(4, cell.getWidth() - 2)), cell.getHeight()).reduced(0, 4);

        g.setColour(text);
        g.setFont(systemUiFont(channelCount > 2 ? 6.0f : 8.0f, juce::Font::bold));
        g.drawText(labels[static_cast<size_t>(channel)], label, juce::Justification::centred);
        g.setColour(juce::Colour(0xff101318));
        g.fillRoundedRectangle(meter.toFloat(), 3.0f);
        paintSegmentedBar(g, meter.reduced(4, 5),
                          scaleValueToNormalised(peak[static_cast<size_t>(channel)]),
                          { 0.0f, scaleValueToNormalised(-18.0f), scaleValueToNormalised(-6.0f), 1.0f },
                          { green, yellow, red, red });

        const auto holdY = meter.getBottom() - juce::roundToInt(static_cast<float>(meter.getHeight())
                                                                  * scaleValueToNormalised(hold[static_cast<size_t>(channel)]));
        g.setColour(overload ? juce::Colour(0xffff5b58) : text);
        g.fillRect(juce::Rectangle<int>(meter.getX() + 3, holdY, meter.getWidth() - 6, 2));
        g.setColour(juce::Colour(0xff111418));
        g.fillRoundedRectangle(valueBox.toFloat(), 3.0f);
        g.setColour(overload ? juce::Colour(0xffff5b58) : accent);
        g.setFont(systemUiFont(8.0f, juce::Font::bold));
        g.drawText(juce::String(juce::roundToInt(peak[static_cast<size_t>(channel)])), valueBox, juce::Justification::centred);
    }
}

void MultiChannelMeterControl::mouseDown(const juce::MouseEvent&)
{
    setShowsOutput(! showsOutput, juce::sendNotificationSync);
}

} // namespace wjn::common