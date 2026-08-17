#pragma once

namespace mixerpro
{

enum class BackendKind
{
    nullTest,
    jack2,
    asio
};

struct AudioDeviceSettings
{
    BackendKind backendKind = BackendKind::nullTest;
    double requestedSampleRate = 48000.0;
    int requestedBlockSize = 128;
    bool followExternalClock = true;
    bool allowBackendRestartOnApply = true;
};

struct EffectiveAudioDeviceSettings
{
    BackendKind backendKind = BackendKind::nullTest;
    double sampleRate = 48000.0;
    int blockSize = 128;
};

} // namespace mixerpro
