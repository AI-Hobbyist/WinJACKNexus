#pragma once

#include "Audio/AudioSettings.h"
#include "Audio/RealtimeTypes.h"

#include <memory>

namespace mixerpro
{

class AudioEngine final : public AudioProcessCallback
{
public:
    struct Snapshot
    {
        double sampleRate = 48000.0;
        int blockSize = 128;
        int inputChannels = 2;
        int outputChannels = 2;
    };

    void prepare(const EffectiveAudioDeviceSettings& settings);
    void start() noexcept;
    void stop() noexcept;
    bool isRunning() const noexcept;

    std::shared_ptr<const Snapshot> getSnapshot() const noexcept;

    void process(AudioProcessContext& context) noexcept override;
    void processBlock(const juce::AudioBuffer<float>& input,
                      juce::AudioBuffer<float>& output) noexcept;

private:
    std::shared_ptr<const Snapshot> currentSnapshot;
    bool running = false;
};

} // namespace mixerpro
