#pragma once

namespace wjn::common
{

enum class BackendKind
{
    jack2
};

struct AudioDeviceSettings
{
    BackendKind backend = BackendKind::jack2;
    double sampleRate = 48000.0;
    int blockSize = 1024;
    int inputChannels = 2;
    int outputChannels = 2;
};

struct EffectiveAudioSettings
{
    double sampleRate = 48000.0;
    int blockSize = 1024;
    int inputChannels = 0;
    int outputChannels = 0;
};

} // namespace wjn::common
