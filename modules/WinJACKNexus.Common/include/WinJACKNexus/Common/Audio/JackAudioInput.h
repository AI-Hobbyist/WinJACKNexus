#pragma once

#include "JackClient.h"

#include <array>
#include <memory>

namespace wjn::common
{

class JackAudioInput final
{
public:
    using BlockCallback = void (*)(const float* const* inputs, int channels,
                                    int frames, void* userData) noexcept;
    static constexpr int maxChannels = JackClient::maxPorts;

    JackAudioInput()
        : buffers(std::make_unique<BufferStorage>())
    {
        for (int index = 0; index < maxChannels; ++index)
            bufferPointers[static_cast<size_t>(index)] = (*buffers)[static_cast<size_t>(index)].data();
    }

    bool open(const juce::String& clientName, int channels, int blockSize) noexcept;
    bool start(BlockCallback callback, void* userData) noexcept;
    void stop() noexcept;
    void close() noexcept;
    bool isOpen() const noexcept;
    JackClient::Status getStatus() const noexcept;
    const juce::String& getLastError() const noexcept;
    const float* getChannelData(int channel) const noexcept;

private:
    using BufferStorage = std::array<std::array<float, JackClient::maxBlockFrames>, maxChannels>;
    static void process(const float* const* inputs, int inputChannels,
                        float* const*, int, int frames, void* userData) noexcept;

    JackClient client;
    std::unique_ptr<BufferStorage> buffers;
    std::array<const float*, maxChannels> bufferPointers {};
    BlockCallback callback = nullptr;
    void* callbackUserData = nullptr;
    int channelCount = 0;
};

} // namespace wjn::common
