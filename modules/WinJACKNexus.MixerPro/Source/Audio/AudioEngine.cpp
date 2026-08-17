#include "Audio/AudioEngine.h"

#include <algorithm>

namespace mixerpro
{

void AudioEngine::prepare(const EffectiveAudioDeviceSettings& settings)
{
    auto snapshot = std::make_shared<Snapshot>();
    snapshot->sampleRate = settings.sampleRate;
    snapshot->blockSize = settings.blockSize;
    snapshot->inputChannels = 2;
    snapshot->outputChannels = 2;
    currentSnapshot = std::move(snapshot);
}

void AudioEngine::start() noexcept
{
    running = true;
}

void AudioEngine::stop() noexcept
{
    running = false;
}

bool AudioEngine::isRunning() const noexcept
{
    return running;
}

std::shared_ptr<const AudioEngine::Snapshot> AudioEngine::getSnapshot() const noexcept
{
    return currentSnapshot;
}

void AudioEngine::process(AudioProcessContext& context) noexcept
{
    processBlock(context.input, context.output);
}

void AudioEngine::processBlock(const juce::AudioBuffer<float>& input,
                               juce::AudioBuffer<float>& output) noexcept
{
    output.clear();

    const auto snapshot = currentSnapshot;

    if (snapshot == nullptr || !running)
        return;

    const auto channelsToCopy = std::min({ input.getNumChannels(),
                                           output.getNumChannels(),
                                           snapshot->outputChannels });

    const auto samplesToCopy = std::min(input.getNumSamples(), output.getNumSamples());

    for (int channel = 0; channel < channelsToCopy; ++channel)
        output.copyFrom(channel, 0, input, channel, 0, samplesToCopy);
}

} // namespace mixerpro
