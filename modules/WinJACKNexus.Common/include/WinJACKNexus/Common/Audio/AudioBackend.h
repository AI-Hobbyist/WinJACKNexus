#pragma once

#include "AudioSettings.h"
#include "RealtimeTypes.h"

namespace wjn::common
{

class AudioBackend
{
public:
    virtual ~AudioBackend() = default;

    virtual bool open(const AudioDeviceSettings& settings) = 0;
    virtual void close() noexcept = 0;
    virtual bool start(AudioProcessCallback& callback) noexcept = 0;
    virtual void stop() noexcept = 0;
    virtual bool isOpen() const noexcept = 0;
    virtual bool isRunning() const noexcept = 0;
    virtual EffectiveAudioSettings getEffectiveSettings() const noexcept = 0;
};

} // namespace wjn::common
