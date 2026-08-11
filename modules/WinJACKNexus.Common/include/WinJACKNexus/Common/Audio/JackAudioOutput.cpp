#include "JackAudioOutput.h"

#include <algorithm>
#include <cstring>

namespace wjn::common
{

bool JackAudioOutput::open(const juce::String& clientName, int channels, int blockSize) noexcept
{
    close();
    if (channels < 0 || blockSize <= 0 || blockSize > JackClient::maxBlockFrames)
        return false;
    buffers->resize(static_cast<size_t>(channels));
    if (!client.open(clientName, blockSize))
        return false;

    juce::StringArray names;
    for (int index = 0; index < channels; ++index)
        names.add("out_" + juce::String(index + 1));
    if (!client.configurePorts(names, {}))
    {
        client.close();
        return false;
    }
    channelCount = channels;
    pendingFrames.store(0, std::memory_order_release);
    return true;
}

bool JackAudioOutput::start() noexcept
{
    client.setProcessCallback(&JackAudioOutput::process, this);
    return client.activate();
}

void JackAudioOutput::stop() noexcept { client.deactivate(); }

void JackAudioOutput::close() noexcept
{
    stop();
    client.close();
    pendingFrames.store(0, std::memory_order_release);
    pendingChannels.store(0, std::memory_order_release);
    channelCount = 0;
}

bool JackAudioOutput::isOpen() const noexcept { return client.getStatus().connected; }
JackClient::Status JackAudioOutput::getStatus() const noexcept { return client.getStatus(); }
const juce::String& JackAudioOutput::getLastError() const noexcept { return client.getLastError(); }

void JackAudioOutput::submitBlock(const float* const* inputs, int channels, int frames) noexcept
{
    if (inputs == nullptr || frames <= 0 || frames > JackClient::maxBlockFrames)
        return;
    const auto copiedChannels = (std::min)(channels, channelCount);
    for (int channel = 0; channel < copiedChannels; ++channel)
        if (inputs[channel] != nullptr)
            std::memcpy((*buffers)[static_cast<size_t>(channel)].data(), inputs[channel],
                        static_cast<size_t>(frames) * sizeof(float));
    for (int channel = copiedChannels; channel < channelCount; ++channel)
        std::fill_n((*buffers)[static_cast<size_t>(channel)].data(), frames, 0.0f);
    pendingChannels.store(copiedChannels, std::memory_order_relaxed);
    pendingFrames.store(frames, std::memory_order_release);
}

void JackAudioOutput::process(const float* const*, int, float* const* outputs,
                              int outputChannels, int frames, void* userData) noexcept
{
    auto* owner = static_cast<JackAudioOutput*>(userData);
    if (owner == nullptr || outputs == nullptr || frames <= 0)
        return;
    const auto availableFrames = owner->pendingFrames.exchange(0, std::memory_order_acq_rel);
    const auto availableChannels = owner->pendingChannels.load(std::memory_order_relaxed);
    const auto copiedFrames = (std::min)(frames, availableFrames);
    const auto copiedChannels = (std::min)({ outputChannels, availableChannels, owner->channelCount });
    for (int channel = 0; channel < outputChannels; ++channel)
    {
        if (outputs[channel] == nullptr)
            continue;
        std::fill_n(outputs[channel], frames, 0.0f);
        if (channel < copiedChannels)
            std::memcpy(outputs[channel], (*owner->buffers)[static_cast<size_t>(channel)].data(),
                        static_cast<size_t>(copiedFrames) * sizeof(float));
    }
}

} // namespace wjn::common
