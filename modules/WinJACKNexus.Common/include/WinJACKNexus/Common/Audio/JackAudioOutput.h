#pragma once

#include "JackClient.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace wjn::common
{

class JackAudioOutput final
{
public:
    JackAudioOutput() = default;

    bool open(const juce::String& clientName, int channels, int blockSize) noexcept;
    bool start() noexcept;
    void stop() noexcept;
    void close() noexcept;
    bool isOpen() const noexcept;
    JackClient::Status getStatus() const noexcept;
    const juce::String& getLastError() const noexcept;
    void submitBlock(const float* const* inputs, int channels, int frames) noexcept;

private:
    using BufferStorage = std::vector<std::array<float, JackClient::maxBlockFrames>>;
    static void process(const float* const*, int, float* const* outputs,
                        int outputChannels, int frames, void* userData) noexcept;

    JackClient client;
    std::unique_ptr<BufferStorage> buffers = std::make_unique<BufferStorage>();
    std::atomic<int> pendingFrames { 0 };
    std::atomic<int> pendingChannels { 0 };
    int channelCount = 0;
};

} // namespace wjn::common
