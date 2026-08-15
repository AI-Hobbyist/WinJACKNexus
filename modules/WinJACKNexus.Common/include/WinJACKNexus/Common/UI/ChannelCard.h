#pragma once

#include "MeterComponent.h"
#include "OnOffSwitch.h"
#include "CommonControls.h"

#include <WinJACKNexus/Common/Audio/MeterEngine.h>

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::common
{

class ChannelCard final : public juce::Component
{
public:
    using Action = std::function<void(ChannelCard&)>;
    using RenameAction = std::function<void(ChannelCard&, const juce::String&)>;
    using PresetAction = std::function<void(ChannelCard&, int)>;
    using GroupAction = std::function<void(ChannelCard&, const juce::String&)>;
    using DragAction = std::function<void(ChannelCard&, juce::Point<int>, bool, bool)>;
    using ContextMenuAction = std::function<void(ChannelCard&, juce::Point<int>)>;

    explicit ChannelCard(int channelIndex);
    void setChannelName(const juce::String& name);
    juce::String getChannelName() const;
    void setCardColour(juce::Colour colour);
    juce::Colour getCardColour() const noexcept;
    void setGroupOptions(const juce::StringArray& names);
    void setGroupName(const juce::String& name);
    juce::String getGroupName() const;
    void setPresetOptions(const juce::StringArray& names, int selectedId);
    int getSelectedPresetId() const noexcept;
    void setPreset(float targetLufs, float toleranceLu, float truePeakMaxDbtp);
    void setMeterValues(const MeterValues& values);
    void resetMeterValues(const MeterValues& values);
    void setMeterThickness(float pixels) noexcept;
    void setPeakHoldDuration(float seconds);
    void setRecordState(bool shouldRecord, juce::NotificationType notification = juce::dontSendNotification);
    bool isRecording() const noexcept;
    void setActionLabels (const juce::String& resetText, const juce::String& recordText);
    void setTheme(const ThemeContext& theme);
    void setOnReset(Action callback);
    void setOnRecord(Action callback);
    void setOnRename(RenameAction callback);
    void setOnPresetChange(PresetAction callback);
    void setOnGroupChange(GroupAction callback);
    void setOnDrag(DragAction callback);
    void setOnHistory(Action callback);
    void setOnContextMenu(ContextMenuAction callback);
    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    NexusLabel channelName;
    NexusButton resetButton { juce::String::fromUTF8("重置") };
    OnOffSwitch recordSwitch;
    NexusLabel recordLabel;
    juce::ComboBox presetBox;
    juce::ComboBox groupBox;
    MeterComponent peak { "PEAK", MeterComponent::MeterType::decibels };
    MeterComponent rms { "RMS", MeterComponent::MeterType::decibels };
    MeterComponent truePeak { "dBTP", MeterComponent::MeterType::truePeak };
    MeterComponent momentary { "M", MeterComponent::MeterType::loudness, -60.0f, 0.0f };
    MeterComponent shortTerm { "S", MeterComponent::MeterType::loudness, -60.0f, 0.0f };
    MeterComponent integrated { "I", MeterComponent::MeterType::loudness, -60.0f, 0.0f };
    MeterComponent range { "LRA", MeterComponent::MeterType::range, 0.0f, 72.0f };
    ThemeContext theme;
    juce::Colour cardColour { 0xff3b4c59 };
    Action onReset;
    Action onRecord;
    RenameAction onRename;
    PresetAction onPresetChange;
    GroupAction onGroupChange;
    DragAction onDrag;
    Action onHistory;
    ContextMenuAction onContextMenu;
    bool dragging = false;
    bool dragWithShift = false;
};

} // namespace wjn::common
