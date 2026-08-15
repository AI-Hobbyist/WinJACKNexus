#pragma once

#include <atomic>
#include <array>
#include <functional>
#include <memory>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include <WinJACKNexus/Common/Audio/JackAudioInput.h>
#include <WinJACKNexus/Common/Audio/JackAudioOutput.h>
#include <WinJACKNexus/Common/IO/SpscRingBuffer.h>
#include <WinJACKNexus/Common/Midi/JackMidiInput.h>
#include <WinJACKNexus/Common/Midi/JackMidiOutput.h>

namespace wjn::adapter
{

class RealEngine final : private juce::Thread,
                         private juce::MidiInputCallback,
                         private juce::AudioIODeviceCallback
{
public:
    struct Configuration
    {
        juce::String clientName;
        juce::String midiDeviceIdentifier;
        juce::String audioDeviceName;
        int channels = 2;
        bool midi = false;
        bool input = false;
        juce::WASAPIDeviceMode wasapiMode = juce::WASAPIDeviceMode::shared;
    };

    static constexpr int maxAudioChannels = wjn::common::JackAudioOutput::maxChannels;
    using AudioLevels = std::array<float, maxAudioChannels>;
    struct AudioStatus
    {
        double wdmSampleRate = 0.0;
        double jackSampleRate = 0.0;
        bool resampling = false;
    };

    using AudioCallback = std::function<void (const AudioLevels&, float, bool,
                                              const AudioStatus&)>;
    using MidiCallback = std::function<void (const std::array<float, 16>&)>;

    RealEngine();
    ~RealEngine() override;

    void setAudioCallback (AudioCallback callback);
    void setMidiCallback (MidiCallback callback);
    bool start (Configuration configuration);
    bool renameClient (const juce::String& clientName);
    void stop();
    void refresh();

private:
    void run() override;
    bool startEngine();
    void stopEngine();
    static void processAudio (const float* const* inputs, int channels,
                              int frames, void* userData) noexcept;
    void appendRenderInput (const float* const* inputs, int channels,
                            int frames) noexcept;
    void compactRenderInput() noexcept;
    void processRenderAudio() noexcept;
    void publishMidiMessage (const juce::MidiMessage& message) noexcept;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message) override;
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int inputChannels,
                                           float* const* outputs, int outputChannels, int frames,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    bool startAudioDevice();
    void resetResamplers() noexcept;
    void compactCaptureInput() noexcept;
    void processCaptureAudio (int channels) noexcept;

    struct AudioBlock
    {
        static constexpr int maxFrames = wjn::common::JackClient::maxBlockFrames * 2;

        int channels;
        int frames;
        std::array<std::array<float, maxFrames>,
               wjn::common::JackAudioOutput::maxChannels> samples;
    };

    static constexpr int captureInputCapacity = AudioBlock::maxFrames
                                              + wjn::common::JackClient::maxBlockFrames * 2;
    using CaptureInputStorage = std::array<std::array<float, captureInputCapacity>,
                                           maxAudioChannels>;

    Configuration configuration;
    AudioCallback audioCallback;
    MidiCallback midiCallback;
    wjn::common::JackAudioInput audioInput;
    wjn::common::JackAudioOutput audioOutput;
    wjn::common::JackMidiInput jackMidiInput;
    wjn::common::JackMidiOutput jackMidiOutput;
    std::unique_ptr<juce::MidiInput> systemMidiInput;
    std::unique_ptr<juce::MidiOutput> systemMidiOutput;
    std::unique_ptr<juce::AudioIODeviceType> windowsAudioType;
    std::unique_ptr<juce::AudioIODevice> windowsAudioDevice;
    wjn::common::SpscRingBuffer<AudioBlock, 4> jackToDeviceBlocks;
    AudioBlock jackToDeviceWriteBlock;
    AudioBlock jackToDeviceReadBlock;
    AudioBlock deviceToJackBlock;
    std::array<const float*, wjn::common::JackAudioOutput::maxChannels> deviceToJackPointers {};
    std::array<juce::LagrangeInterpolator, wjn::common::JackAudioOutput::maxChannels> captureResamplers;
    std::array<juce::LagrangeInterpolator, wjn::common::JackAudioOutput::maxChannels> renderResamplers;
    std::unique_ptr<CaptureInputStorage> captureInputSamples;
    CaptureInputStorage renderInputSamples {};
    int renderInputChannels = 0;
    int renderInputReadOffset = 0;
    int renderInputFrames = 0;
    double renderInputPhase = 1.0;
    int captureInputReadOffset = 0;
    int captureInputFrames = 0;
    double captureInputPhase = 1.0;
    int renderReadOffset = 0;
    double deviceSampleRate = 48000.0;
    double jackSampleRate = 48000.0;
    std::atomic<float> audioPeak { 0.0f };
    std::array<std::atomic<float>, maxAudioChannels> audioPeaks {};
    std::atomic<bool> audioClipping { false };
    std::atomic<bool> active { false };
    std::atomic<juce::uint64> pendingMidiEvents { 0 };
    juce::uint64 deliveredMidiEvents = 0;
    std::array<std::atomic<float>, 16> midiLevels {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RealEngine)
};

} // namespace wjn::adapter