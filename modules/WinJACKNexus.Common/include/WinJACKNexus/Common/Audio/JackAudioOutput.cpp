#include "JackAudioOutput.h"

#include <algorithm>
#include <cstring>

namespace wjn::common
{

bool JackAudioOutput::open(const juce::String& clientName, int channels, int blockSize) noexcept
{
    close();
    if (channels < 1 || channels > maxChannels || blockSize <= 0 || blockSize > JackClient::maxBlockFrames)
        return false;
    if (!client.open(clientName, blockSize))
        return false;

    juce::StringArray names;
    for (int index = 0; index < channels; ++index)
        names.add("out_" + juce::String(index + 1));
    if (!client.configurePorts({}, names))
    {
        client.close();
        return false;
    }
    channelCount = channels;
    blocks.reset();
    readBlock.channels = 0;
    readBlock.frames = 0;
    readOffset = 0;
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
    blocks.reset();
    channelCount = 0;
    readBlock.channels = 0;
    readBlock.frames = 0;
    readOffset = 0;
}

bool JackAudioOutput::isOpen() const noexcept { return client.getStatus().connected; }
JackClient::Status JackAudioOutput::getStatus() const noexcept { return client.getStatus(); }
const juce::String& JackAudioOutput::getLastError() const noexcept { return client.getLastError(); }

void JackAudioOutput::submitBlock(const float* const* inputs, int channels, int frames) noexcept
{
    if (inputs == nullptr || frames <= 0 || frames > JackClient::maxBlockFrames)
        return;
    const auto copiedChannels = (std::min)({ channels, channelCount, maxChannels });
    writeBlock.channels = copiedChannels;
    writeBlock.frames = frames;
    for (int channel = 0; channel < copiedChannels; ++channel)
        if (inputs[channel] != nullptr)
            std::memcpy(writeBlock.samples[static_cast<size_t>(channel)].data(), inputs[channel],
                        static_cast<size_t>(frames) * sizeof(float));
    for (int channel = copiedChannels; channel < channelCount; ++channel)
        std::fill_n(writeBlock.samples[static_cast<size_t>(channel)].data(), frames, 0.0f);
    blocks.push(writeBlock);
}

void JackAudioOutput::process(const float* const*, int, float* const* outputs,
                              int outputChannels, int frames, void* userData) noexcept
{
    auto* owner = static_cast<JackAudioOutput*>(userData);
    if (owner == nullptr || outputs == nullptr || frames <= 0)
        return;
    for (int channel = 0; channel < outputChannels; ++channel)
    {
        if (outputs[channel] == nullptr)
            continue;
        std::fill_n(outputs[channel], frames, 0.0f);
    }

    int outputOffset = 0;
    while (outputOffset < frames)
    {
        if (owner->readOffset >= owner->readBlock.frames)
        {
            if (! owner->blocks.pop (owner->readBlock))
                break;
            owner->readOffset = 0;
        }

        const auto copiedFrames = (std::min) (frames - outputOffset,
                                              owner->readBlock.frames - owner->readOffset);
        const auto copiedChannels = (std::min) ({ outputChannels, owner->readBlock.channels,
                                                   owner->channelCount });
        for (int channel = 0; channel < copiedChannels; ++channel)
        {
            if (outputs[channel] == nullptr)
                continue;
            std::memcpy (outputs[channel] + outputOffset,
                         owner->readBlock.samples[static_cast<size_t> (channel)].data()
                             + owner->readOffset,
                         static_cast<size_t> (copiedFrames) * sizeof (float));
        }
        owner->readOffset += copiedFrames;
        outputOffset += copiedFrames;
    }
}

} // namespace wjn::common
