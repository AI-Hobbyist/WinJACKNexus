#include "ChannelCard.h"

namespace wjn::common
{

ChannelCard::ChannelCard(int channelIndex)
{
    channelName.setText(juce::String::fromUTF8("输入 ") + juce::String(channelIndex + 1),
                        juce::dontSendNotification);
    channelName.setJustificationType(juce::Justification::centred);
    channelName.setEditable(true, false, false);
    channelName.onTextChange = [this]
    {
        if (onRename != nullptr)
            onRename(*this, channelName.getText().trim());
    };
    addAndMakeVisible(channelName);
    addAndMakeVisible(resetButton);
    addAndMakeVisible(recordSwitch);
    addAndMakeVisible(recordLabel);
    for (auto* meter : { &peak, &rms, &truePeak, &momentary, &shortTerm, &integrated, &range })
        addAndMakeVisible(meter);
    resetButton.setButtonText(juce::String::fromUTF8("重置"));
    resetButton.onClick = [this] { if (onReset != nullptr) onReset(*this); };
    recordLabel.setText(juce::String::fromUTF8("记录"), juce::dontSendNotification);
    recordLabel.setJustificationType(juce::Justification::centred);
    recordSwitch.setStateChangeCallback([this] (bool)
    {
        if (onRecord != nullptr)
            onRecord(*this);
    });
    presetBox.onChange = [this]
    {
        if (onPresetChange != nullptr)
            onPresetChange(*this, presetBox.getSelectedId());
    };
    groupBox.onChange = [this]
    {
        if (onGroupChange != nullptr)
            onGroupChange(*this, groupBox.getText());
    };
    addAndMakeVisible(presetBox);
    addAndMakeVisible(groupBox);
    setTheme(theme);
}

void ChannelCard::setChannelName(const juce::String& name) { channelName.setText(name, juce::dontSendNotification); }
juce::String ChannelCard::getChannelName() const { return channelName.getText(); }
void ChannelCard::setCardColour(juce::Colour colour)
{
    cardColour = colour;
    channelName.setColour(juce::Label::backgroundColourId, cardColour);
    channelName.setColour(juce::Label::textColourId,
                          cardColour.getPerceivedBrightness() > 0.55f
                              ? juce::Colours::black : juce::Colours::white);
    repaint();
}
juce::Colour ChannelCard::getCardColour() const noexcept { return cardColour; }
void ChannelCard::setGroupOptions(const juce::StringArray& names)
{
    const auto selected = groupBox.getText();
    groupBox.clear(juce::dontSendNotification);
    groupBox.addItemList(names, 1);
    if (selected.isNotEmpty())
        groupBox.setText(selected, juce::dontSendNotification);
}
void ChannelCard::setGroupName(const juce::String& name) { groupBox.setText(name, juce::dontSendNotification); }
juce::String ChannelCard::getGroupName() const { return groupBox.getText().trim(); }
void ChannelCard::setPresetOptions(const juce::StringArray& names, int selectedId)
{
    presetBox.clear(juce::dontSendNotification);
    presetBox.addItemList(names, 1);
    presetBox.setSelectedId(selectedId, juce::dontSendNotification);
}
int ChannelCard::getSelectedPresetId() const noexcept { return presetBox.getSelectedId(); }
void ChannelCard::setPreset(float targetLufs, float toleranceLu, float truePeakMaxDbtp)
{
    for (auto* meter : { &momentary, &shortTerm, &integrated })
        meter->setPreset(targetLufs, toleranceLu, truePeakMaxDbtp);
    truePeak.setPreset(targetLufs, toleranceLu, truePeakMaxDbtp);
}
void ChannelCard::setMeterValues(const MeterValues& values)
{
    peak.setValue(values.peakDbfs);
    rms.setValue(values.rmsDbfs);
    truePeak.setValue(values.truePeakDbtp);
    momentary.setValue(values.momentaryLufs);
    shortTerm.setValue(values.shortTermLufs);
    integrated.setValue(values.integratedLufs);
    range.setValue(values.lraLu);
}
void ChannelCard::resetMeterValues(const MeterValues& values)
{
    peak.resetValue(values.peakDbfs);
    rms.resetValue(values.rmsDbfs);
    truePeak.resetValue(values.truePeakDbtp);
    momentary.resetValue(values.momentaryLufs);
    shortTerm.resetValue(values.shortTermLufs);
    integrated.resetValue(values.integratedLufs);
    range.resetValue(values.lraLu);
}
void ChannelCard::setMeterThickness(float pixels) noexcept
{
    for (auto* meter : { &peak, &rms, &truePeak, &momentary, &shortTerm, &integrated, &range })
        meter->setBarThickness(pixels);
}
void ChannelCard::setPeakHoldDuration(float seconds)
{
    peak.setPeakHoldDuration(seconds);
    rms.setPeakHoldDuration(seconds);
}
void ChannelCard::setRecordState(bool shouldRecord, juce::NotificationType notification)
{
    recordSwitch.setToggleState(shouldRecord, notification);
}
bool ChannelCard::isRecording() const noexcept { return recordSwitch.getToggleState(); }
void ChannelCard::setActionLabels (const juce::String& resetText, const juce::String& recordText)
{
    resetButton.setButtonText (resetText);
    recordLabel.setText (recordText, juce::dontSendNotification);
}
void ChannelCard::setTheme(const ThemeContext& newTheme)
{
    theme = newTheme;
    for (auto* meter : { &peak, &rms, &truePeak, &momentary, &shortTerm, &integrated, &range })
        meter->setTheme(theme);
    recordSwitch.setTheme(theme);
    setCardColour(cardColour);
    repaint();
}
void ChannelCard::setOnReset(Action callback) { onReset = std::move(callback); }
void ChannelCard::setOnRecord(Action callback) { onRecord = std::move(callback); }
void ChannelCard::setOnRename(RenameAction callback) { onRename = std::move(callback); }
void ChannelCard::setOnPresetChange(PresetAction callback) { onPresetChange = std::move(callback); }
void ChannelCard::setOnGroupChange(GroupAction callback) { onGroupChange = std::move(callback); }
void ChannelCard::setOnDrag(DragAction callback) { onDrag = std::move(callback); }
void ChannelCard::setOnHistory(Action callback) { onHistory = std::move(callback); }
void ChannelCard::setOnContextMenu(ContextMenuAction callback) { onContextMenu = std::move(callback); }

void ChannelCard::resized()
{
    auto area = getLocalBounds().reduced(8);
    channelName.setBounds(area.removeFromTop(27));
    presetBox.setBounds(area.removeFromTop(27).reduced(0, 2));
    groupBox.setBounds(area.removeFromTop(25).reduced(0, 2));
    area.removeFromTop(4);

    auto metersArea = area.removeFromTop(juce::jmax(1, area.getHeight() - 39));
    constexpr int meterGap = 4;
    const auto width = juce::jmax(1, (metersArea.getWidth() - meterGap * 6) / 7);
    for (int index = 0; index < 7; ++index)
    {
        auto* meter = std::array<MeterComponent*, 7> { &peak, &rms, &truePeak,
                                                       &momentary, &shortTerm, &integrated, &range }[static_cast<size_t> (index)];
        meter->setBounds(metersArea.removeFromLeft(width));
        if (index < 6)
            metersArea.removeFromLeft(meterGap);
    }

    auto buttons = area;
    resetButton.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2).reduced(2));
    recordSwitch.setBounds(buttons.removeFromRight(34).reduced(2));
    recordLabel.setBounds(buttons.reduced(2));
}

void ChannelCard::paint(juce::Graphics& g)
{
    g.setColour(theme.colour("rackPanel"));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), theme.metric("panelRadius", 4.0f));
    g.setColour(cardColour);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), theme.metric("panelRadius", 4.0f), 1.0f);
}

void ChannelCard::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
    {
        if (onContextMenu != nullptr)
            onContextMenu(*this, event.getScreenPosition());
        return;
    }

    if (! event.mods.isLeftButtonDown())
        return;

    dragging = true;
    dragWithShift = event.mods.isShiftDown();
    if (onDrag != nullptr)
        onDrag(*this, {}, true, dragWithShift);
}

void ChannelCard::mouseDrag(const juce::MouseEvent& event)
{
    if (dragging && event.mods.isLeftButtonDown() && onDrag != nullptr)
        onDrag(*this, event.getOffsetFromDragStart(), false, dragWithShift);
}

void ChannelCard::mouseUp(const juce::MouseEvent&)
{
    dragging = false;
    dragWithShift = false;
}

} // namespace wjn::common
