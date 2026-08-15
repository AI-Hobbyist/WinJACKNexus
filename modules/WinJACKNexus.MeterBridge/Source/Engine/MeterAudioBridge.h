#pragma once

#include <WinJACKNexus/Common/Audio/JackClient.h>
#include <WinJACKNexus/Common/IO/SpscRingBuffer.h>

#include <array>
#include <memory>
#include <vector>

namespace wjn::meterbridge
{

class MeterAudioBridge final
{
public:
    static constexpr int maxAudioFrames = 1024;

    struct AudioBlock
    {
        int frameCount = 0;
        std::array<float, maxAudioFrames> samples {};
    };

    MeterAudioBridge();
    ~MeterAudioBridge();

    bool connect (const juce::StringArray& inputNames);
    void disconnect();
    void prepareForProcessExit() noexcept;
    bool pop (int channelIndex, AudioBlock& block) noexcept;
    void resetQueues() noexcept;
    wjn::common::JackClient::Status getStatus() const noexcept;
    juce::String getLastError() const;

private:
    using Queue = wjn::common::SpscRingBuffer<AudioBlock, 32>;

    struct AudioState
    {
        std::vector<std::unique_ptr<Queue>> queues;
    };

    static void processAudio (const float* const* inputs, int inputChannels,
                              float* const* outputs, int outputChannels,
                              int frameCount, void* userData) noexcept;

    std::unique_ptr<wjn::common::JackClient> jackClient;
    std::unique_ptr<AudioState> audioState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterAudioBridge)
};

} // namespace wjn::meterbridge
