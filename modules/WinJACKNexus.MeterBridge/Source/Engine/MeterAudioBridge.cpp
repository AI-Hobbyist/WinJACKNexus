#include "MeterAudioBridge.h"

#include <algorithm>
#include <cstring>

namespace wjn::meterbridge
{

MeterAudioBridge::MeterAudioBridge()
    : jackClient (std::make_unique<wjn::common::JackClient>())
{
}

MeterAudioBridge::~MeterAudioBridge()
{
    disconnect();
}

bool MeterAudioBridge::connect (const juce::StringArray& inputNames)
{
    if (jackClient == nullptr)
        jackClient = std::make_unique<wjn::common::JackClient>();

    if (jackClient->getStatus().connected)
    {
        jackClient->setProcessCallback (nullptr, nullptr);
        jackClient->deactivate();
        audioState.reset();
    }
    else
    {
        disconnect();
        if (! jackClient->open ("JackMeterBridge", maxAudioFrames))
            return false;
    }

    if (! jackClient->configurePorts (inputNames, {}))
    {
        jackClient->setProcessCallback (nullptr, nullptr);
        audioState.reset();
        return false;
    }

    audioState = std::make_unique<AudioState>();
    auto& queues = audioState->queues;
    queues.reserve (static_cast<size_t> (inputNames.size()));
    for (int index = 0; index < inputNames.size(); ++index)
        queues.push_back (std::make_unique<Queue>());

    jackClient->setProcessCallback (&MeterAudioBridge::processAudio, audioState.get());
    if (! jackClient->activate())
    {
        jackClient->setProcessCallback (nullptr, nullptr);
        audioState.reset();
        return false;
    }

    return true;
}

void MeterAudioBridge::disconnect()
{
    if (jackClient != nullptr)
        jackClient->close();
    audioState.reset();
}

void MeterAudioBridge::prepareForProcessExit() noexcept
{
    if (jackClient == nullptr)
        return;

    jackClient->setProcessCallback (nullptr, nullptr);
    jackClient.release();
    audioState.release();
}

bool MeterAudioBridge::pop (int channelIndex, AudioBlock& block) noexcept
{
    if (audioState == nullptr
        || ! juce::isPositiveAndBelow (channelIndex, static_cast<int> (audioState->queues.size())))
        return false;
    return audioState->queues[static_cast<size_t> (channelIndex)]->pop (block);
}

void MeterAudioBridge::resetQueues() noexcept
{
    if (audioState == nullptr)
        return;
    for (auto& queue : audioState->queues)
        queue->reset();
}

wjn::common::JackClient::Status MeterAudioBridge::getStatus() const noexcept
{
    return jackClient != nullptr ? jackClient->getStatus() : wjn::common::JackClient::Status {};
}

juce::String MeterAudioBridge::getLastError() const
{
    return jackClient != nullptr ? jackClient->getLastError() : juce::String();
}

void MeterAudioBridge::processAudio (const float* const* inputs, int inputChannels,
                                     float* const* outputs, int outputChannels,
                                     int frameCount, void* userData) noexcept
{
    juce::ignoreUnused (outputs, outputChannels);
    auto* state = static_cast<AudioState*> (userData);
    if (state == nullptr || inputs == nullptr || frameCount <= 0
        || frameCount > maxAudioFrames)
        return;

    const auto channelCount = std::min (inputChannels, static_cast<int> (state->queues.size()));
    for (int channel = 0; channel < channelCount; ++channel)
    {
        if (inputs[channel] == nullptr)
            continue;

        state->queues[static_cast<size_t> (channel)]->pushWith (
            [source = inputs[channel], frameCount] (AudioBlock& block) noexcept
            {
                block.frameCount = frameCount;
                std::memcpy (block.samples.data(), source,
                             static_cast<size_t> (frameCount) * sizeof (float));
            });
    }
}

} // namespace wjn::meterbridge
