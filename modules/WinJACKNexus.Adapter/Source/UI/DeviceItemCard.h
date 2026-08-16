#pragma once

#include <array>

#include <juce_gui_basics/juce_gui_basics.h>

#include <WinJACKNexus/Common/UI/AudioLed.h>
#include <WinJACKNexus/Common/UI/CommonControls.h>
#include <WinJACKNexus/Common/UI/LcdDisplayControl.h>
#include <WinJACKNexus/Common/UI/MidiLed.h>
#include <WinJACKNexus/Common/UI/OnOffSwitch.h>
#include <WinJACKNexus/AdapterBackend/RealEngine.h>

namespace wjn::adapter
{

class DeviceItemCard final : public juce::Component
{
public:
    struct Data
    {
        juce::String id;
        juce::String clientName;
        juce::String driver;
        juce::String streamType;
        juce::String device;
        juce::String midiDeviceIdentifier;
        juce::String audioDeviceName;
        int channels = 2;
        double sampleRate = 0.0;
        bool midi = false;
        bool input = false;
        bool paused = false;
        juce::WASAPIDeviceMode wasapiMode = juce::WASAPIDeviceMode::shared;
    };

    using RenameCallback = std::function<bool (DeviceItemCard&, juce::String)>;
    using VoidCallback = std::function<void (DeviceItemCard&)>;

    DeviceItemCard (Data data, RenameCallback onRename, VoidCallback onPause, VoidCallback onRemove);
    ~DeviceItemCard() override;

    const Data& getData() const noexcept { return data; }
    void setChannels (int channelCount);
    void setPaused (bool shouldPause);
    bool startClient();
    bool isClientStartComplete() const noexcept;
    void refresh();
    void releaseClient();
    bool renameClient (const juce::String& clientName) { return realEngine.renameClient (clientName); }

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void commitName();
    void repaintLcdIfDue();
    void configureLcd();
    void setAudioLevel (const RealEngine::AudioLevels& levels, float level, bool clipping,
                        const RealEngine::AudioStatus& status);
    void setMidiLevels (const std::array<float, 16>& levels);
    void clearMidiLevels();
    void paintLcd (juce::Graphics&, juce::Rectangle<float>, const juce::Font&, juce::Colour);
    void paintAudioLcd (juce::Graphics&, juce::Rectangle<float>, const juce::Font&, juce::Colour);
    void paintMidiLcd (juce::Graphics&, juce::Rectangle<float>, const juce::Font&, juce::Colour);

    Data data;
    RenameCallback renameCallback;
    VoidCallback pauseCallback;
    VoidCallback removeCallback;
    wjn::common::NexusTextEditor clientNameEditor;
    juce::ComboBox channelSelector;
    wjn::common::OnOffSwitch pauseSwitch;
    wjn::common::NexusButton removeButton;
    wjn::common::AudioLed audioLed;
    wjn::common::LcdDisplayControl lcdDisplay;
    wjn::common::MidiLed midiLed;
    RealEngine realEngine;
    juce::Array<float> audioLevels;
    std::array<float, 16> midiLevels {};
    RealEngine::AudioStatus audioStatus;
    bool audioClipping = false;
    bool midiMode = false;
    juce::uint32 lastLcdRepaintTime = 0;
    bool firstPaintTraced = false;
    bool firstResizeTraced = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeviceItemCard)
};

} // namespace wjn::adapter
