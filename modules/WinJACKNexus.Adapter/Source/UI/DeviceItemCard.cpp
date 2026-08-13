#include "DeviceItemCard.h"

#include <cmath>

namespace wjn::adapter
{
namespace
{

juce::String fromUtf8 (const char* value)
{
    return juce::String::fromUTF8 (value);
}

void drawLcdText (juce::Graphics& g, const juce::String& text,
                  juce::Rectangle<float> bounds, const juce::Font& font,
                  juce::Colour ink, juce::Justification justification,
                  bool fitted = false)
{
    g.setColour (ink);
    g.setFont (font);
    if (fitted)
        g.drawFittedText (text, bounds.toNearestInt(), justification, 1);
    else
        g.drawText (text, bounds.toNearestInt(), justification, false);
}

float levelRatio (float level)
{
    const auto decibels = 20.0f * std::log10 (juce::jmax (0.001f, level));
    return juce::jlimit (0.0f, 1.0f, (decibels + 60.0f) / 63.0f);
}

juce::String channelName (int index)
{
    static const char* const surroundNames[] { "FL", "FR", "C", "LFE", "SL", "SR", "SBL", "SBR" };
    return index < juce::numElementsInArray (surroundNames)
               ? juce::String (surroundNames[index])
               : "CH " + juce::String (index + 1);
}

void drawDbScale (juce::Graphics& g, juce::Rectangle<float> bounds,
                  const juce::Font& font, juce::Colour ink)
{
    static const char* const labels[] { "-60", "-40", "-20", "-10", "-3", "0", "+3" };
    static constexpr float positions[] { 0.0f, 0.317f, 0.635f, 0.794f, 0.905f, 0.952f, 1.0f };

    for (int index = 0; index < juce::numElementsInArray (labels); ++index)
    {
        const auto x = bounds.getX() + bounds.getWidth() * positions[index];
        drawLcdText (g, labels[index], { x - 16.0f, bounds.getY(), 32.0f, bounds.getHeight() },
                     font, ink, juce::Justification::centred);
    }
}

void drawHorizontalMeter (juce::Graphics& g, juce::Rectangle<float> row,
                          const juce::String& label, float level,
                          const juce::Font& font, juce::Colour ink)
{
    const auto labelArea = row.removeFromLeft (32.0f);
    const auto meterArea = row.reduced (2.0f, 4.0f);
    drawLcdText (g, label, labelArea, font, ink, juce::Justification::centredLeft);

    constexpr int segmentCount = 36;
    const auto filledSegments = static_cast<int> (std::ceil (levelRatio (level) * segmentCount));
    const auto segmentWidth = meterArea.getWidth() / static_cast<float> (segmentCount);
    for (int index = 0; index < segmentCount; ++index)
    {
        const auto segment = juce::Rectangle<float> (meterArea.getX() + index * segmentWidth + 1.0f,
                                                     meterArea.getY(),
                                                     juce::jmax (1.0f, segmentWidth - 2.0f),
                                                     meterArea.getHeight());
        g.setColour (index < filledSegments ? ink : ink.withAlpha (0.16f));
        g.fillRect (segment);
    }
}

} // namespace

DeviceItemCard::DeviceItemCard (Data itemData, RenameCallback onRename,
                                VoidCallback onPause, VoidCallback onRemove)
    : data (std::move (itemData)), renameCallback (std::move (onRename)),
      pauseCallback (std::move (onPause)), removeCallback (std::move (onRemove)),
      midiMode (data.midi)
{
    for (int index = 0; index < juce::jmax (1, data.channels); ++index)
    audioLevels.add (0.0f);

    addAndMakeVisible (audioLed);
    addAndMakeVisible (midiLed);
    audioLed.setVisible (! midiMode);
    midiLed.setVisible (midiMode);

    mockEngine.setAudioCallback ([this] (float level, bool clipping)
    {
        setAudioLevel (level, clipping);
    });
    mockEngine.setMidiCallback ([this]
    {
        ++midiPulse;
        midiLed.trigger();
        lcdDisplay.repaint();
    });

    addAndMakeVisible (clientNameEditor);
    clientNameEditor.setText (data.clientName, juce::dontSendNotification);
    clientNameEditor.onReturnKey = [this] { commitName(); };
    clientNameEditor.onFocusLost = [this] { commitName(); };

    addAndMakeVisible (lcdDisplay);
    configureLcd();
    mockEngine.start (midiMode);

    addAndMakeVisible (pauseSwitch);
    pauseSwitch.setToggleState (! data.paused, juce::dontSendNotification);
    pauseSwitch.setStateChangeCallback ([this] (bool isOn)
    {
        data.paused = ! isOn;
        if (data.paused)
            mockEngine.stop();
        else
            mockEngine.start (midiMode);
        if (pauseCallback != nullptr)
            pauseCallback (*this);
        repaint();
    });

    addAndMakeVisible (removeButton);
    removeButton.setButtonText (fromUtf8 ("删除"));
    removeButton.onClick = [this]
    {
        if (removeCallback != nullptr)
            removeCallback (*this);
    };
}

DeviceItemCard::~DeviceItemCard()
{
    mockEngine.stop();
    mockEngine.setAudioCallback (nullptr);
    mockEngine.setMidiCallback (nullptr);
}

void DeviceItemCard::configureLcd()
{
    lcdDisplay.setAccent (juce::Colour (0xff142216));
    lcdDisplay.setContentPainter ([this] (juce::Graphics& g, juce::Rectangle<float> bounds,
                                          const juce::Font& font, juce::Colour ink)
    {
        paintLcd (g, bounds, font, ink);
    });
}

void DeviceItemCard::setAudioLevel (float level, bool clipping)
{
    audioClipping = clipping;
    audioLed.setLevel (level, clipping);

    for (int index = 0; index < audioLevels.size(); ++index)
    {
        const auto variation = 0.82f + 0.14f * std::sin (0.7f * static_cast<float> (index + 1));
        audioLevels.set (index, juce::jlimit (0.0f, 1.0f, level * variation));
    }

    lcdDisplay.repaint();
}

void DeviceItemCard::paintLcd (juce::Graphics& g, juce::Rectangle<float> bounds,
                               const juce::Font& font, juce::Colour ink)
{
    if (midiMode)
        paintMidiLcd (g, bounds, font, ink);
    else
        paintAudioLcd (g, bounds, font, ink);
}

void DeviceItemCard::paintAudioLcd (juce::Graphics& g, juce::Rectangle<float> bounds,
                                    const juce::Font& font, juce::Colour ink)
{
    const auto headerFont = font.withHeight (13.0f);
    const auto smallFont = font.withHeight (9.0f);
    auto header = bounds.removeFromTop (18.0f);
    auto deviceHeader = header.removeFromLeft (header.getWidth() * 0.42f);
    auto sampleHeader = header.removeFromLeft (header.getWidth() * 0.55f);
    drawLcdText (g, data.device + "  " + (data.streamType == "Record" ? "IN" : "OUT"),
                 deviceHeader, headerFont, ink, juce::Justification::centredLeft, true);
    drawLcdText (g, "PCM 48.0kHz", sampleHeader, headerFont, ink, juce::Justification::centred, true);
    const auto currentLevel = audioLevels.isEmpty() ? 0.0f : audioLevels.getFirst();
    const auto currentDb = 20.0f * std::log10 (juce::jmax (0.001f, currentLevel));
    drawLcdText (g, "VOL " + juce::String (currentDb, 1) + "dB",
                 header, headerFont, ink, juce::Justification::centredRight, true);

    bounds.removeFromTop (3.0f);
    const auto channelCount = juce::jmax (1, data.channels);
    if (channelCount <= 2)
    {
        const auto scale = bounds.removeFromBottom (14.0f);
        const auto meterArea = bounds;
        const auto rowHeight = meterArea.getHeight() / static_cast<float> (channelCount);
        for (int index = 0; index < channelCount; ++index)
        {
            const auto row = meterArea.withY (meterArea.getY() + rowHeight * index)
                                       .withHeight (rowHeight);
            const auto level = index < audioLevels.size() ? audioLevels[index] : 0.0f;
            drawHorizontalMeter (g, row, channelCount == 1 ? "MONO" : (index == 0 ? "L" : "R"),
                                 level, smallFont, ink);
        }

        drawDbScale (g, { meterArea.getX() + 32.0f, scale.getY(), meterArea.getWidth() - 32.0f,
                          scale.getHeight() }, smallFont, ink);
        return;
    }

    auto chart = bounds;
    const auto scaleColumn = chart.removeFromLeft (28.0f);
    const auto labels = chart.removeFromTop (17.0f);
    const auto scale = chart.removeFromBottom (14.0f);
    const auto meterArea = chart;
    const auto columnWidth = meterArea.getWidth() / static_cast<float> (channelCount);

    const char* const dbLabels[] { "0", "-10", "-20", "-30", "-40", "-50", "-60" };
    for (int index = 0; index < juce::numElementsInArray (dbLabels); ++index)
    {
        const auto y = meterArea.getY() + meterArea.getHeight() * index / 6.0f - 5.0f;
        drawLcdText (g, dbLabels[index], scaleColumn.withY (y).withHeight (11.0f),
                     smallFont, ink, juce::Justification::centredRight);
    }

    for (int index = 0; index < channelCount; ++index)
    {
        const auto column = meterArea.withX (meterArea.getX() + index * columnWidth)
                                      .withWidth (columnWidth);
        const auto labelArea = labels.withX (column.getX()).withWidth (column.getWidth());
        drawLcdText (g, channelName (index), labelArea, smallFont, ink, juce::Justification::centred);

        const auto margin = juce::jmin (8.0f, column.getWidth() * 0.22f);
        const auto meter = column.reduced (margin, 1.0f);
        g.setColour (ink.withAlpha (0.24f));
        g.drawRect (meter, 1.0f);

        const auto level = index < audioLevels.size() ? audioLevels[index] : 0.0f;
        constexpr int segmentCount = 8;
        const auto filledSegments = static_cast<int> (std::ceil (levelRatio (level) * segmentCount));
        const auto segmentHeight = meter.getHeight() / static_cast<float> (segmentCount);
        for (int segment = 0; segment < segmentCount; ++segment)
        {
            const auto segmentBounds = juce::Rectangle<float> (
                meter.getX() + 2.0f,
                meter.getBottom() - (segment + 1) * segmentHeight + 1.0f,
                juce::jmax (1.0f, meter.getWidth() - 4.0f),
                juce::jmax (1.0f, segmentHeight - 2.0f));
            g.setColour (segment < filledSegments ? ink : ink.withAlpha (0.16f));
            g.fillRect (segmentBounds);
        }
    }

    drawLcdText (g, audioClipping ? "CLIP" : "LEVEL",
                 scale, smallFont, ink, juce::Justification::centredRight);
}

void DeviceItemCard::paintMidiLcd (juce::Graphics& g, juce::Rectangle<float> bounds,
                                   const juce::Font& font, juce::Colour ink)
{
    const auto headerFont = font.withHeight (13.0f);
    const auto smallFont = font.withHeight (9.0f);
    auto header = bounds.removeFromTop (18.0f);
    auto deviceHeader = header.removeFromLeft (header.getWidth() * 0.38f);
    auto modeHeader = header.removeFromLeft (header.getWidth() * 0.48f);
    drawLcdText (g, data.device, deviceHeader, headerFont, ink, juce::Justification::centredLeft, true);
    drawLcdText (g, "CH MODE : OMNI", modeHeader, headerFont, ink, juce::Justification::centred, true);
    drawLcdText (g, "BANK A", header, headerFont, ink, juce::Justification::centredRight, true);

    bounds.removeFromTop (3.0f);
    const auto scaleColumn = bounds.removeFromLeft (28.0f);
    const auto labels = bounds.removeFromTop (17.0f);
    const auto scale = bounds.removeFromBottom (14.0f);
    const auto meterArea = bounds;

    drawLcdText (g, "127", scaleColumn.withY (meterArea.getY() - 2.0f).withHeight (11.0f),
                 smallFont, ink, juce::Justification::centredRight);
    drawLcdText (g, "64", scaleColumn.withY (meterArea.getCentreY() - 5.0f).withHeight (11.0f),
                 smallFont, ink, juce::Justification::centredRight);
    drawLcdText (g, "0", scaleColumn.withY (meterArea.getBottom() - 9.0f).withHeight (11.0f),
                 smallFont, ink, juce::Justification::centredRight);

    constexpr int midiChannelCount = 16;
    const auto columnWidth = meterArea.getWidth() / static_cast<float> (midiChannelCount);
    const auto activeChannel = midiPulse % midiChannelCount;
    for (int index = 0; index < midiChannelCount; ++index)
    {
        const auto column = meterArea.withX (meterArea.getX() + index * columnWidth)
                                      .withWidth (columnWidth);
        drawLcdText (g, juce::String (index + 1), labels.withX (column.getX()).withWidth (column.getWidth()),
                     smallFont, ink, juce::Justification::centred);

        const auto margin = juce::jmin (6.0f, column.getWidth() * 0.2f);
        const auto meter = column.reduced (margin, 1.0f);
        g.setColour (ink.withAlpha (0.24f));
        g.drawRect (meter, 1.0f);

        const auto level = index == activeChannel ? 0.92f
                                                   : 0.14f + 0.04f * static_cast<float> ((index + midiPulse) % 4);
        constexpr int segmentCount = 8;
        const auto filledSegments = static_cast<int> (std::ceil (level * segmentCount));
        const auto segmentHeight = meter.getHeight() / static_cast<float> (segmentCount);
        for (int segment = 0; segment < segmentCount; ++segment)
        {
            const auto segmentBounds = juce::Rectangle<float> (
                meter.getX() + 1.5f,
                meter.getBottom() - (segment + 1) * segmentHeight + 1.0f,
                juce::jmax (1.0f, meter.getWidth() - 3.0f),
                juce::jmax (1.0f, segmentHeight - 2.0f));
            g.setColour (segment < filledSegments ? ink : ink.withAlpha (0.16f));
            g.fillRect (segmentBounds);
        }
    }

    drawLcdText (g, "MIDI CHANNELS", scale, smallFont, ink, juce::Justification::centredRight);
}

void DeviceItemCard::setPaused (bool shouldPause)
{
    data.paused = shouldPause;
    pauseSwitch.setToggleState (! shouldPause, juce::dontSendNotification);
    repaint();
    lcdDisplay.repaint();
}

void DeviceItemCard::commitName()
{
    auto name = clientNameEditor.getText().trim();
    if (name.isEmpty())
    {
        clientNameEditor.setText (data.clientName, juce::dontSendNotification);
        return;
    }

    if (name != data.clientName)
    {
        data.clientName = name;
        if (renameCallback != nullptr)
            renameCallback (*this, name);
    }
}

void DeviceItemCard::paint (juce::Graphics& g)
{
    g.setColour (data.paused ? wjn::common::theme::border : wjn::common::theme::rackPanel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    g.setColour (data.paused ? wjn::common::theme::secondaryText : wjn::common::theme::border);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
}

void DeviceItemCard::resized()
{
    auto area = getLocalBounds().reduced (10, 8);
    auto header = area.removeFromTop (30);
    auto ledArea = header.removeFromLeft (30);
    audioLed.setBounds (ledArea.reduced (3));
    midiLed.setBounds (ledArea.reduced (3));
    header.removeFromLeft (8);
    removeButton.setBounds (header.removeFromRight (58));
    header.removeFromRight (8);
    pauseSwitch.setBounds (header.removeFromRight (50));
    header.removeFromRight (10);
    clientNameEditor.setBounds (header);
    area.removeFromTop (6);
    lcdDisplay.setBounds (area);
}

} // namespace wjn::adapter
