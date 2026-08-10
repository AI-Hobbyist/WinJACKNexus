#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <WinJACKNexus/Common/UI/AudioLed.h>
#include <WinJACKNexus/Common/UI/MidiLed.h>
#include "../Engine/MockEngine.h"

namespace wjn::adapter
{

class DeviceItemCard final : public juce::Component
{
public:
    struct Data
    {
        juce::String clientName;
        juce::String driver;
        juce::String streamType;
        juce::String device;
        int channels = 2;
        bool paused = false;
    };

    using RenameCallback = std::function<void (DeviceItemCard&, juce::String)>;
    using VoidCallback = std::function<void (DeviceItemCard&)>;

    DeviceItemCard (Data data, RenameCallback onRename, VoidCallback onPause, VoidCallback onRemove);
    ~DeviceItemCard() override;

    const Data& getData() const noexcept { return data; }
    void setPaused (bool shouldPause);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void commitName();

    Data data;
    RenameCallback renameCallback;
    VoidCallback pauseCallback;
    VoidCallback removeCallback;
    juce::Label modeLabel;
    juce::Label deviceLabel;
    juce::Label sampleRateLabel;
    juce::TextEditor clientNameEditor;
    juce::ToggleButton pauseButton;
    juce::TextButton removeButton;
    wjn::common::AudioLed audioLed;
    wjn::common::MidiLed midiLed;
    MockEngine mockEngine;
    bool midiMode = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeviceItemCard)
};

} // namespace wjn::adapter
