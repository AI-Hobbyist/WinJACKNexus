#include "DeviceItemCard.h"

#include <WinJACKNexus/Common/UI/Theme.h>

namespace wjn::adapter
{
namespace
{

juce::Font systemFont (float height, int style = juce::Font::plain)
{
    return juce::Font (juce::FontOptions (juce::Font::getSystemUIFontName(), height, style));
}

juce::String fromUtf8 (const char* value)
{
    return juce::String::fromUTF8 (value);
}

} // namespace

DeviceItemCard::DeviceItemCard (Data itemData, RenameCallback onRename,
                                VoidCallback onPause, VoidCallback onRemove)
    : data (std::move (itemData)), renameCallback (std::move (onRename)),
      pauseCallback (std::move (onPause)), removeCallback (std::move (onRemove)),
      midiMode (data.driver.containsIgnoreCase ("MIDI"))
{
    addAndMakeVisible (audioLed);
    addAndMakeVisible (midiLed);
    audioLed.setVisible (! midiMode);
    midiLed.setVisible (midiMode);

    mockEngine.setAudioCallback ([this] (float level, bool clipping)
    {
        audioLed.setLevel (level, clipping);
    });
    mockEngine.setMidiCallback ([this]
    {
        midiLed.trigger();
    });
    mockEngine.start (midiMode);

    addAndMakeVisible (clientNameEditor);
    clientNameEditor.setText (data.clientName, juce::dontSendNotification);
    clientNameEditor.setJustification (juce::Justification::centred);
    clientNameEditor.setFont (systemFont (14.0f));
    clientNameEditor.setColour (juce::TextEditor::textColourId, wjn::common::theme::primaryText);
    clientNameEditor.setColour (juce::TextEditor::backgroundColourId, wjn::common::theme::darkCanvas);
    clientNameEditor.setColour (juce::TextEditor::outlineColourId, wjn::common::theme::border);
    clientNameEditor.setColour (juce::TextEditor::focusedOutlineColourId, wjn::common::theme::activeTab);
    clientNameEditor.onReturnKey = [this] { commitName(); };
    clientNameEditor.onFocusLost = [this] { commitName(); };

    addAndMakeVisible (modeLabel);
    modeLabel.setText (data.streamType, juce::dontSendNotification);
    modeLabel.setFont (systemFont (12.0f));
    modeLabel.setColour (juce::Label::textColourId, wjn::common::theme::secondaryText);

    addAndMakeVisible (deviceLabel);
    deviceLabel.setText (data.device, juce::dontSendNotification);
    deviceLabel.setFont (systemFont (12.0f));
    deviceLabel.setColour (juce::Label::textColourId, wjn::common::theme::secondaryText);
    deviceLabel.setTooltip (data.driver);

    addAndMakeVisible (sampleRateLabel);
    sampleRateLabel.setText ("48000 Hz  |  " + juce::String (data.channels) + " ch",
                             juce::dontSendNotification);
    sampleRateLabel.setFont (systemFont (12.0f));
    sampleRateLabel.setColour (juce::Label::textColourId, wjn::common::theme::secondaryText);

    addAndMakeVisible (pauseButton);
    pauseButton.setButtonText (fromUtf8 ("暂停"));
    pauseButton.setToggleState (data.paused, juce::dontSendNotification);
    pauseButton.onClick = [this]
    {
        data.paused = pauseButton.getToggleState();
        if (data.paused)
            mockEngine.stop();
        else
            mockEngine.start (midiMode);
        if (pauseCallback != nullptr)
            pauseCallback (*this);
        repaint();
    };

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
}

void DeviceItemCard::setPaused (bool shouldPause)
{
    data.paused = shouldPause;
    pauseButton.setToggleState (shouldPause, juce::dontSendNotification);
    repaint();
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
    auto ledArea = area.removeFromLeft (30);
    audioLed.setBounds (ledArea.reduced (3));
    midiLed.setBounds (ledArea.reduced (3));
    area.removeFromLeft (10);
    removeButton.setBounds (area.removeFromRight (56));
    area.removeFromRight (8);
    pauseButton.setBounds (area.removeFromRight (62));
    area.removeFromRight (12);
    sampleRateLabel.setBounds (area.removeFromRight (108));
    area.removeFromRight (12);
    deviceLabel.setBounds (area.removeFromRight (150));
    area.removeFromRight (12);
    modeLabel.setBounds (area.removeFromRight (92));
    area.removeFromRight (12);
    clientNameEditor.setBounds (area);
}

} // namespace wjn::adapter
