#include "DeviceItemCard.h"
#include <WinJACKNexus/AdapterBackend/DebugTrace.h>

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

juce::String formatSampleRate (double sampleRate)
{
    return sampleRate > 0.0 ? juce::String (sampleRate / 1000.0, 1) + " kHz" : "--";
}

juce::String channelName (int index)
{
    static const char* const surroundNames[] {
        "前左", "前右", "中置", "低频", "后左", "后右", "后环左", "后环右"
    };
    return index < juce::numElementsInArray (surroundNames)
               ? fromUtf8 (surroundNames[index])
               : fromUtf8 ("声道 ") + juce::String (index + 1);
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

DeviceItemCard::DeviceItemCard (Data itemData, wjn::common::JackClientHub* newJackHub,
                                                                juce::String newJackHubClientName, RenameCallback onRename,
                                                                VoidCallback onPause, VoidCallback onRemove)
    : data (std::move (itemData)), renameCallback (std::move (onRename)),
      pauseCallback (std::move (onPause)), removeCallback (std::move (onRemove)),
            jackHub (newJackHub), jackHubClientName (std::move (newJackHubClientName)),
      midiMode (data.midi)
{
        debug::trace ("card ctor body begin card=" + debug::pointerText (this)
                                    + " midi=" + juce::String (data.midi ? 1 : 0)
                                    + " channels=" + juce::String (data.channels));
    for (int index = 0; index < juce::jmax (1, data.channels); ++index)
    audioLevels.add (0.0f);
        debug::trace ("card after audioLevels size=" + juce::String (audioLevels.size()));

    addAndMakeVisible (audioLed);
    addAndMakeVisible (midiLed);
    audioLed.setVisible (! midiMode);
    midiLed.setVisible (midiMode);
        debug::trace ("card after leds");

    realEngine.setAudioCallback ([this] (const RealEngine::AudioLevels& levels,
                                         float level, bool clipping,
                                         const RealEngine::AudioStatus& status)
    {
        setAudioLevel (levels, level, clipping, status);
    });
    realEngine.setMidiCallback ([this] (const std::array<float, 16>& levels)
    {
        setMidiLevels (levels);
        midiLed.trigger();
    });
    debug::trace ("card after engine callbacks");

    addAndMakeVisible (clientNameEditor);
    clientNameEditor.setText (data.clientName, juce::dontSendNotification);
    clientNameEditor.onReturnKey = [this] { commitName(); };
    clientNameEditor.onFocusLost = [this] { commitName(); };
    debug::trace ("card after client editor");

    addAndMakeVisible (channelSelector);
    channelSelector.setTooltip (fromUtf8 ("声道数"));
    for (int channelCount = 1; channelCount <= RealEngine::maxAudioChannels; ++channelCount)
        channelSelector.addItem (juce::String (channelCount), channelCount);
    channelSelector.setVisible (! midiMode);
    channelSelector.setSelectedId (juce::jlimit (1, RealEngine::maxAudioChannels, data.channels),
                                   juce::dontSendNotification);
    channelSelector.onChange = [this]
    {
        setChannels (channelSelector.getSelectedId());
    };

    addAndMakeVisible (lcdDisplay);
    configureLcd();
    lcdDisplay.setPowered (false);
    debug::trace ("card after lcd");

    addAndMakeVisible (pauseSwitch);
    data.paused = true;
    pauseSwitch.setToggleState (false, juce::dontSendNotification);
    pauseSwitch.setStateChangeCallback ([this] (bool isOn)
    {
        setPaused (! isOn);
        if (pauseCallback != nullptr)
            pauseCallback (*this);
    });
    debug::trace ("card after pause switch");

    addAndMakeVisible (removeButton);
    removeButton.setButtonText (fromUtf8 ("删除"));
    removeButton.onClick = [this]
    {
        if (removeCallback != nullptr)
            removeCallback (*this);
    };
    debug::trace ("card ctor complete");
}

DeviceItemCard::~DeviceItemCard()
{
    releaseClient();
}

void DeviceItemCard::releaseClient()
{
    realEngine.setAudioCallback (nullptr);
    realEngine.setMidiCallback (nullptr);
    realEngine.stop();
}

bool DeviceItemCard::startClient()
{
    data.paused = false;
    pauseSwitch.setToggleState (true, juce::dontSendNotification);
    lcdDisplay.setPowered (true);
    const auto started = realEngine.start ({ data.clientName, data.midiDeviceIdentifier,
                                             data.audioDeviceName, data.channels, data.midi,
                                             data.input, data.wasapiMode,
                                             jackHub, jackHubClientName });
    repaint();
    lcdDisplay.repaint();
    return started;
}

bool DeviceItemCard::isClientStartComplete() const noexcept
{
    return realEngine.isStartComplete();
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

void DeviceItemCard::setAudioLevel (const RealEngine::AudioLevels& levels,
                                    float level, bool clipping,
                                    const RealEngine::AudioStatus& status)
{
    auto changed = audioClipping != clipping
                || audioStatus.wdmSampleRate != status.wdmSampleRate
                || audioStatus.jackSampleRate != status.jackSampleRate
                || audioStatus.resampling != status.resampling;
    audioClipping = clipping;
    audioStatus = status;
    audioLed.setLevel (level, clipping);

    for (int index = 0; index < audioLevels.size(); ++index)
    {
        const auto channelLevel = index < static_cast<int> (levels.size())
                                     ? levels[static_cast<size_t> (index)]
                                     : 0.0f;
        const auto limitedLevel = juce::jlimit (0.0f, 1.0f, channelLevel);
        changed = changed || audioLevels[index] != limitedLevel;
        audioLevels.set (index, limitedLevel);
    }

    if (changed)
        repaintLcdIfDue();
}

void DeviceItemCard::refresh()
{
    realEngine.refresh();
    audioLed.update();
    midiLed.update();
}

void DeviceItemCard::setMidiLevels (const std::array<float, 16>& levels)
{
    auto changed = false;
    for (size_t index = 0; index < midiLevels.size(); ++index)
        changed = changed || midiLevels[index] != levels[index];

    midiLevels = levels;
    if (changed)
        repaintLcdIfDue();
}

void DeviceItemCard::clearMidiLevels()
{
    auto changed = false;
    for (const auto level : midiLevels)
        changed = changed || level != 0.0f;

    midiLevels.fill (0.0f);
    if (changed)
        repaintLcdIfDue();
}

void DeviceItemCard::repaintLcdIfDue()
{
    constexpr auto minimumIntervalMs = static_cast<juce::uint32> (50);
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastLcdRepaintTime < minimumIntervalMs)
        return;

    lastLcdRepaintTime = now;
    lcdDisplay.repaint();
}

void DeviceItemCard::setChannels (int channelCount)
{
    if (midiMode)
        return;

    const auto newChannelCount = juce::jlimit (1, RealEngine::maxAudioChannels, channelCount);
    if (data.channels == newChannelCount)
        return;

    const auto wasPaused = data.paused;
    data.channels = newChannelCount;
    audioLevels.clear();
    for (int index = 0; index < data.channels; ++index)
        audioLevels.add (0.0f);

    channelSelector.setSelectedId (data.channels, juce::dontSendNotification);
    setPaused (wasPaused);
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
    drawLcdText (g, data.device + "  " + fromUtf8 (data.input ? "输入" : "输出"),
                 deviceHeader, headerFont, ink, juce::Justification::centredLeft, true);
    const auto sampleStatus = fromUtf8 ("WDM采样率 ") + formatSampleRate (audioStatus.wdmSampleRate)
                             + fromUtf8 (" | JACK采样率 ")
                             + formatSampleRate (audioStatus.jackSampleRate)
                             + fromUtf8 (" | 重采样 ")
                             + fromUtf8 (audioStatus.resampling ? "是" : "否");
    drawLcdText (g, sampleStatus, sampleHeader, smallFont, ink,
                 juce::Justification::centred, true);
    const auto currentLevel = audioLevels.isEmpty() ? 0.0f : audioLevels.getFirst();
    const auto currentDb = 20.0f * std::log10 (juce::jmax (0.001f, currentLevel));
    drawLcdText (g, fromUtf8 ("音量 ") + juce::String (currentDb, 1) + " dB",
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
            drawHorizontalMeter (g, row, channelCount == 1 ? fromUtf8 ("单声道")
                                                            : (index == 0 ? fromUtf8 ("左")
                                                                          : fromUtf8 ("右")),
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

    drawLcdText (g, audioClipping ? fromUtf8 ("削波") : fromUtf8 ("电平"),
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
    drawLcdText (g, fromUtf8 ("通道模式：全通道"), modeHeader, headerFont, ink,
                 juce::Justification::centred, true);
    drawLcdText (g, fromUtf8 ("音色库 A"), header, headerFont, ink,
                 juce::Justification::centredRight, true);

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

        const auto level = midiLevels[static_cast<size_t> (index)];
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

    drawLcdText (g, fromUtf8 ("MIDI 通道"), scale, smallFont, ink, juce::Justification::centredRight);
}

void DeviceItemCard::setPaused (bool shouldPause)
{
    data.paused = shouldPause;
    pauseSwitch.setToggleState (! shouldPause, juce::dontSendNotification);
    lcdDisplay.setPowered (! shouldPause);
    if (data.paused)
    {
        realEngine.stop();
        for (int index = 0; index < audioLevels.size(); ++index)
            audioLevels.set (index, 0.0f);
        audioStatus = {};
        audioClipping = false;
        audioLed.setLevel (0.0f, false);
        clearMidiLevels();
    }
    else
        startClient();
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
        const auto renamed = renameCallback == nullptr || renameCallback (*this, name);
        if (renamed)
            data.clientName = name;
        else
            clientNameEditor.setText (data.clientName, juce::dontSendNotification);
    }
}

void DeviceItemCard::paint (juce::Graphics& g)
{
    if (! firstPaintTraced)
    {
        firstPaintTraced = true;
        debug::trace ("card first paint card=" + debug::pointerText (this));
    }
    g.setColour (data.paused ? wjn::common::theme::border : wjn::common::theme::rackPanel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    g.setColour (data.paused ? wjn::common::theme::secondaryText : wjn::common::theme::border);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
}

void DeviceItemCard::resized()
{
    const auto traceFirstResize = ! firstResizeTraced;
    if (traceFirstResize)
    {
        firstResizeTraced = true;
        debug::trace ("card first resized begin card=" + debug::pointerText (this)
                      + " bounds=" + getBounds().toString());
    }
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
    channelSelector.setBounds (header.removeFromRight (62));
    header.removeFromRight (10);
    clientNameEditor.setBounds (header);
    area.removeFromTop (6);
    lcdDisplay.setBounds (area);
    if (traceFirstResize)
        debug::trace ("card first resized complete card=" + debug::pointerText (this));
}

} // namespace wjn::adapter
