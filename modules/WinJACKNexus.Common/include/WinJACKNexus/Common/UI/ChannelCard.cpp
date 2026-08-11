#include "ChannelCard.h"

namespace wjn::common
{

ChannelCard::ChannelCard(int channelIndex)
{
    channelName.setText("输入 " + juce::String(channelIndex + 1), juce::dontSendNotification);
    channelName.setJustificationType(juce::Justification::centred);
    channelName.setEditable(true, false, false);
    addAndMakeVisible(channelName);
    addAndMakeVisible(resetButton);
    addAndMakeVisible(recordButton);
    for (auto* meter : { &peak, &rms, &truePeak, &momentary, &shortTerm, &integrated, &range })
        addAndMakeVisible(meter);
    resetButton.onClick = [this] { if (onReset != nullptr) onReset(*this); };
    recordButton.onClick = [this] { if (onRecord != nullptr) onRecord(*this); };
    presetBox.addItem("EBU R128", 1);
    presetBox.addItem("ATSC A/85", 2);
    presetBox.addItem("K-System", 3);
    presetBox.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(presetBox);
    setTheme(theme);
}

void ChannelCard::setChannelName(const juce::String& name) { channelName.setText(name, juce::dontSendNotification); }
juce::String ChannelCard::getChannelName() const { return channelName.getText(); }
void ChannelCard::setPreset(float targetLufs, float toleranceLu, float truePeakMaxDbtp)
{
    for (auto* meter : { &momentary, &shortTerm, &integrated })
        meter->setPreset(targetLufs, toleranceLu, truePeakMaxDbtp);
    truePeak.setPreset(targetLufs, toleranceLu, truePeakMaxDbtp);
}
void ChannelCard::setTheme(const ThemeContext& newTheme)
{
    theme = newTheme;
    for (auto* meter : { &peak, &rms, &truePeak, &momentary, &shortTerm, &integrated, &range })
        meter->setTheme(theme);
    repaint();
}
void ChannelCard::setOnReset(Action callback) { onReset = std::move(callback); }
void ChannelCard::setOnRecord(Action callback) { onRecord = std::move(callback); }

void ChannelCard::resized()
{
    auto area = getLocalBounds().reduced(8);
    auto header = area.removeFromTop(28);
    channelName.setBounds(header.removeFromLeft(110));
    recordButton.setBounds(header.removeFromRight(60));
    header.removeFromRight(6);
    resetButton.setBounds(header.removeFromRight(60));
    area.removeFromTop(4);
    presetBox.setBounds(area.removeFromTop(24).removeFromLeft(130));
    area.removeFromTop(4);
    const auto width = juce::jmax(1, area.getWidth() / 7);
    for (auto* meter : { &peak, &rms, &truePeak, &momentary, &shortTerm, &integrated, &range })
        meter->setBounds(area.removeFromLeft(width).reduced(2, 0));
}

void ChannelCard::paint(juce::Graphics& g)
{
    g.setColour(theme.colour("rackPanel"));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), theme.metric("panelRadius", 4.0f));
    g.setColour(theme.colour("border"));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), theme.metric("panelRadius", 4.0f), 1.0f);
}

} // namespace wjn::common
