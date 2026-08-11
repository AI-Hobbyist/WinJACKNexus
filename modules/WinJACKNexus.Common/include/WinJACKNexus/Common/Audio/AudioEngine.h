#pragma once

#include "AudioSettings.h"
#include "RealtimeTypes.h"

#include <atomic>

namespace wjn::common
{

class AudioEngine final : public AudioProcessCallback
{
public:
    struct Snapshot
    {
        double sampleRate = 48000.0;
        int blockSize = 1024;
        int inputChannels = 0;
        int outputChannels = 0;
    };

    void prepare(const EffectiveAudioSettings& settings) noexcept;
    void start() noexcept;
    void stop() noexcept;
    bool isRunning() const noexcept;
    Snapshot getSnapshot() const noexcept;

    void process(AudioProcessContext& context) noexcept override;

private:
    Snapshot snapshot;
    std::atomic<bool> running { false };
};

} // namespace wjn::common
