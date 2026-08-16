#pragma once

#include "JackClient.h"
#include "JackClientHub.h"
#include <WinJACKNexus/Common/IO/SpscRingBuffer.h>

#include <array>
#include <memory>
#include <vector>

namespace wjn::common
{

class JackAudioOutput final
{
public:
    static constexpr int maxChannels = 8;

    JackAudioOutput() = default;

    bool open(const juce::String& clientName, int channels, int blockSize) noexcept;
    bool open(JackClientHub& hub, const juce::String& clientName,
              int channels, int blockSize) noexcept;
    bool start() noexcept;
    void stop() noexcept;
    void close() noexcept;
    bool rename(const juce::String& clientName) noexcept;
    bool isOpen() const noexcept;
    JackClient::Status getStatus() const noexcept;
    const juce::String& getLastError() const noexcept;
    void submitBlock(const float* const* inputs, int channels, int frames) noexcept;

private:
    struct AudioBlock
    {
        int channels = 0;
        int frames = 0;
        std::array<std::array<float, JackClient::maxBlockFrames>, maxChannels> samples {};
    };

    static void process(const float* const*, int, float* const* outputs,
                        int outputChannels, int frames, void* userData) noexcept;

    JackClient client;
    JackClientHub* hub = nullptr;
    JackClientHub::PortHandle hubHandle = JackClientHub::invalidPortHandle;
    AudioBlock writeBlock;
    AudioBlock readBlock;
    SpscRingBuffer<AudioBlock, 8> blocks;
    int channelCount = 0;
    int readOffset = 0;
};

} // namespace wjn::common
