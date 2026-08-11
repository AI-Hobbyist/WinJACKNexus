#include "AudioEngine.h"

#include <algorithm>
#include <cstring>

namespace wjn::common
{

void AudioEngine::prepare(const EffectiveAudioSettings& settings) noexcept
{
    snapshot.sampleRate = settings.sampleRate;
    snapshot.blockSize = std::max(0, settings.blockSize);
    snapshot.inputChannels = std::max(0, settings.inputChannels);
    snapshot.outputChannels = std::max(0, settings.outputChannels);
    running.store(false, std::memory_order_release);
}

void AudioEngine::start() noexcept
{
    running.store(true, std::memory_order_release);
}

void AudioEngine::stop() noexcept
{
    running.store(false, std::memory_order_release);
}

bool AudioEngine::isRunning() const noexcept
{
    return running.load(std::memory_order_acquire);
}

AudioEngine::Snapshot AudioEngine::getSnapshot() const noexcept
{
    return snapshot;
}

void AudioEngine::process(AudioProcessContext& context) noexcept
{
    if (context.frameCount <= 0 || snapshot.blockSize <= 0)
        return;

    const auto channels = std::max(0, std::min(context.inputChannels, context.outputChannels));
    const auto frames = std::min(context.frameCount, snapshot.blockSize);

    for (int channel = 0; channel < context.outputChannels; ++channel)
        if (context.outputs != nullptr && context.outputs[channel] != nullptr)
            std::memset(context.outputs[channel], 0, static_cast<size_t>(context.frameCount) * sizeof(float));

    if (!isRunning() || context.inputs == nullptr || context.outputs == nullptr)
        return;

    for (int channel = 0; channel < channels; ++channel)
        if (context.inputs[channel] != nullptr && context.outputs[channel] != nullptr)
            std::memcpy(context.outputs[channel], context.inputs[channel],
                        static_cast<size_t>(frames) * sizeof(float));
}

} // namespace wjn::common
