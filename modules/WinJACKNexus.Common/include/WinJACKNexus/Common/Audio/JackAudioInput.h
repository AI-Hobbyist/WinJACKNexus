#pragma once

#include "JackClient.h"
#include "JackClientHub.h"

#include <array>
#include <memory>
#include <vector>

namespace wjn::common
{

class JackAudioInput final
{
public:
    using BlockCallback = void (*)(const float* const* inputs, int channels,
                                    int frames, void* userData) noexcept;
    JackAudioInput() = default;

    bool open(const juce::String& clientName, int channels, int blockSize) noexcept;
    bool open(JackClientHub& hub, const juce::String& clientName,
              int channels, int blockSize) noexcept;
    bool start(BlockCallback callback, void* userData) noexcept;
    void stop() noexcept;
    void close() noexcept;
    bool rename(const juce::String& clientName) noexcept;
    bool isOpen() const noexcept;
    JackClient::Status getStatus() const noexcept;
    const juce::String& getLastError() const noexcept;
    const float* getChannelData(int channel) const noexcept;

private:
    using BufferStorage = std::vector<std::array<float, JackClient::maxBlockFrames>>;
    static void process(const float* const* inputs, int inputChannels,
                        float* const*, int, int frames, void* userData) noexcept;

    JackClient client;
    JackClientHub* hub = nullptr;
    JackClientHub::PortHandle hubHandle = JackClientHub::invalidPortHandle;
    std::unique_ptr<BufferStorage> buffers = std::make_unique<BufferStorage>();
    std::vector<const float*> bufferPointers;
    BlockCallback callback = nullptr;
    void* callbackUserData = nullptr;
    int channelCount = 0;
};

} // namespace wjn::common
