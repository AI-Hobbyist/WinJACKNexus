#pragma once

#include <WinJACKNexus/Common/Audio/JackAudioInput.h>
#include <WinJACKNexus/Common/Audio/JackAudioOutput.h>
#include <WinJACKNexus/Common/Metering/MeterFrame.h>

#include <array>
#include <atomic>

namespace mixerpro
{

class CommonJackMixerRuntime final
{
public:
    static constexpr int channelCount = wjn::common::JackAudioOutput::maxChannels;

    CommonJackMixerRuntime();
    ~CommonJackMixerRuntime();

    bool start() noexcept;
    void stop() noexcept;
    bool isConnected() const noexcept;
    bool isRunning() const noexcept;
    wjn::common::JackClient::Status getStatus() const noexcept;
    juce::String getLastError() const;

    wjn::common::MeterFrame getInputMeter() const noexcept;
    wjn::common::MeterFrame getOutputMeter() const noexcept;

    void setFaderDb(float value) noexcept;
    float getFaderDb() const noexcept;
    void setPan(float value) noexcept;
    float getPan() const noexcept;
    void setMute(bool shouldMute) noexcept;
    bool isMuted() const noexcept;
    void setChannelGainDb(int channel, float value) noexcept;
    float getChannelGainDb(int channel) const noexcept;
    void setChannelMute(int channel, bool shouldMute) noexcept;
    bool isChannelMuted(int channel) const noexcept;
    void setChannelSolo(int channel, bool shouldSolo) noexcept;
    bool isChannelSolo(int channel) const noexcept;

private:
    static void processInput(const float* const* inputs, int channels,
                             int frames, void* userData) noexcept;
    void processInputBlock(const float* const* inputs, int channels, int frames) noexcept;
    void updateMixGains() noexcept;
    static wjn::common::MeterFrame readMeter(
        const std::array<std::atomic<float>, wjn::common::MeterFrame::maxChannels>& peaks,
        const std::array<std::atomic<float>, wjn::common::MeterFrame::maxChannels>& rms,
        const std::array<std::atomic<float>, wjn::common::MeterFrame::maxChannels>& holds) noexcept;

    wjn::common::JackClientHub jackHub;
    wjn::common::JackAudioInput audioInput;
    wjn::common::JackAudioOutput audioOutput;
    std::array<std::array<float, wjn::common::JackClient::maxBlockFrames>,
               wjn::common::JackAudioOutput::maxChannels> processedBuffers {};
    std::array<const float*, wjn::common::JackAudioOutput::maxChannels> processedPointers {};

    std::array<std::atomic<float>, wjn::common::MeterFrame::maxChannels> inputPeaks {};
    std::array<std::atomic<float>, wjn::common::MeterFrame::maxChannels> inputRms {};
    std::array<std::atomic<float>, wjn::common::MeterFrame::maxChannels> inputHolds {};
    std::array<std::atomic<float>, wjn::common::MeterFrame::maxChannels> outputPeaks {};
    std::array<std::atomic<float>, wjn::common::MeterFrame::maxChannels> outputRms {};
    std::array<std::atomic<float>, wjn::common::MeterFrame::maxChannels> outputHolds {};
    std::atomic<float> faderDb { 0.0f };
    std::atomic<float> pan { 0.0f };
    std::atomic<float> leftGain { 1.0f };
    std::atomic<float> rightGain { 1.0f };
    std::array<std::atomic<float>, channelCount> channelGainDb {};
    std::array<std::atomic<bool>, channelCount> channelMuted {};
    std::array<std::atomic<bool>, channelCount> channelSolo {};
    std::atomic<bool> muted { false };
    std::atomic<bool> running { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CommonJackMixerRuntime)
};

} // namespace mixerpro