#pragma once

#include "JackClient.h"

#include <array>
#include <atomic>
#include <memory>

namespace wjn::common
{

class JackAudioOutput final
{
public:
    static constexpr int maxChannels = JackClient::maxPorts;

    JackAudioOutput()
        : buffers(std::make_unique<BufferStorage>())
    {
    }

    bool open(const juce::String& clientName, int channels, int blockSize) noexcept;
    bool start() noexcept;
    void stop() noexcept;
    void close() noexcept;
    bool isOpen() const noexcept;
    JackClient::Status getStatus() const noexcept;
    const juce::String& getLastError() const noexcept;
    void submitBlock(const float* const* inputs, int channels, int frames) noexcept;

private:
    using BufferStorage = std::array<std::array<float, JackClient::maxBlockFrames>, maxChannels>;
    static void process(const float* const*, int, float* const* outputs,
                        int outputChannels, int frames, void* userData) noexcept;

    JackClient client;
    std::unique_ptr<BufferStorage> buffers;
    std::atomic<int> pendingFrames { 0 };
    std::atomic<int> pendingChannels { 0 };
    int channelCount = 0;
};

} // namespace wjn::common
