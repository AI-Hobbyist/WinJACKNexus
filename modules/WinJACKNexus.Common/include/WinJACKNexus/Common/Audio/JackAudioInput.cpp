#include "JackAudioInput.h"

#include <algorithm>

namespace wjn::common
{

bool JackAudioInput::open(const juce::String& clientName, int channels, int blockSize) noexcept
{
    close();
    if (channels < 0 || blockSize <= 0 || blockSize > JackClient::maxBlockFrames)
        return false;
    buffers->resize(static_cast<size_t>(channels));
    bufferPointers.resize(static_cast<size_t>(channels));
    for (int index = 0; index < channels; ++index)
        bufferPointers[static_cast<size_t>(index)] = (*buffers)[static_cast<size_t>(index)].data();
    if (!client.open(clientName, blockSize))
        return false;

    juce::StringArray names;
    for (int index = 0; index < channels; ++index)
        names.add("in_" + juce::String(index + 1));
    if (!client.configurePorts({}, names))
    {
        client.close();
        return false;
    }

    channelCount = channels;
    return true;
}

bool JackAudioInput::start(BlockCallback newCallback, void* userData) noexcept
{
    callback = newCallback;
    callbackUserData = userData;
    client.setProcessCallback(&JackAudioInput::process, this);
    return client.activate();
}

void JackAudioInput::stop() noexcept { client.deactivate(); }

void JackAudioInput::close() noexcept
{
    stop();
    client.close();
    callback = nullptr;
    callbackUserData = nullptr;
    channelCount = 0;
}

bool JackAudioInput::isOpen() const noexcept { return client.getStatus().connected; }
JackClient::Status JackAudioInput::getStatus() const noexcept { return client.getStatus(); }
const juce::String& JackAudioInput::getLastError() const noexcept { return client.getLastError(); }

const float* JackAudioInput::getChannelData(int channel) const noexcept
{
    return channel >= 0 && channel < channelCount ? bufferPointers[static_cast<size_t>(channel)] : nullptr;
}

void JackAudioInput::process(const float* const* inputs, int inputChannels,
                             float* const*, int, int frames, void* userData) noexcept
{
    auto* owner = static_cast<JackAudioInput*>(userData);
    if (owner == nullptr || frames <= 0 || frames > JackClient::maxBlockFrames)
        return;

    const auto channels = (std::min)(inputChannels, owner->channelCount);
    for (int channel = 0; channel < channels; ++channel)
        if (inputs != nullptr && inputs[channel] != nullptr)
            std::copy_n(inputs[channel], frames, (*owner->buffers)[static_cast<size_t>(channel)].data());
    for (int channel = channels; channel < owner->channelCount; ++channel)
        std::fill_n((*owner->buffers)[static_cast<size_t>(channel)].data(), frames, 0.0f);
    if (owner->callback != nullptr)
        owner->callback(owner->bufferPointers.data(), owner->channelCount, frames, owner->callbackUserData);
}

} // namespace wjn::common
