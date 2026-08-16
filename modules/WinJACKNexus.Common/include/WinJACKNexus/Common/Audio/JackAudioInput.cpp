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
    if (!client.configurePorts(names, {}))
    {
        client.close();
        return false;
    }

    channelCount = channels;
    return true;
}

bool JackAudioInput::open(JackClientHub& newHub, const juce::String& clientName,
                          int channels, int blockSize) noexcept
{
    close();
    if (channels < 0 || channels > JackClientHub::maxPortsPerGroup
        || blockSize <= 0 || blockSize > JackClient::maxBlockFrames)
        return false;

    buffers->resize(static_cast<size_t>(channels));
    bufferPointers.resize(static_cast<size_t>(channels));
    for (int index = 0; index < channels; ++index)
        bufferPointers[static_cast<size_t>(index)] = (*buffers)[static_cast<size_t>(index)].data();

    juce::StringArray names;
    for (int index = 0; index < channels; ++index)
        names.add(JackClientHub::audioPortName(clientName, index));

    const auto handle = newHub.registerAudioPorts(names, {}, &JackAudioInput::process, this);
    if (handle == JackClientHub::invalidPortHandle)
        return false;

    hub = &newHub;
    hubHandle = handle;
    channelCount = channels;
    return true;
}

bool JackAudioInput::start(BlockCallback newCallback, void* userData) noexcept
{
    callback = newCallback;
    callbackUserData = userData;
    if (hub != nullptr)
        return hub->start(hubHandle);
    client.setProcessCallback(&JackAudioInput::process, this);
    return client.activate();
}

void JackAudioInput::stop() noexcept
{
    if (hub != nullptr)
        hub->stop(hubHandle);
    else
        client.deactivate();
}

void JackAudioInput::close() noexcept
{
    stop();
    if (hub != nullptr)
        hub->unregister(hubHandle);
    hub = nullptr;
    hubHandle = JackClientHub::invalidPortHandle;
    client.close();
    callback = nullptr;
    callbackUserData = nullptr;
    channelCount = 0;
}

bool JackAudioInput::rename(const juce::String& clientName) noexcept
{
    if (hub != nullptr)
    {
        juce::StringArray names;
        for (int index = 0; index < channelCount; ++index)
            names.add(JackClientHub::audioPortName(clientName, index));
        return hub->renameAudioPorts(hubHandle, names, {});
    }
    return client.rename (clientName);
}

bool JackAudioInput::isOpen() const noexcept
{
    return hub != nullptr ? hub->isRouteOpen(hubHandle) : client.getStatus().connected;
}

JackClient::Status JackAudioInput::getStatus() const noexcept
{
    return hub != nullptr ? hub->getStatus() : client.getStatus();
}

const juce::String& JackAudioInput::getLastError() const noexcept
{
    return hub != nullptr ? hub->getLastError() : client.getLastError();
}

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
