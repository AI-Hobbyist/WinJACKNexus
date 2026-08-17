#pragma once

#include "Audio/AudioBackend.h"

namespace mixerpro
{

class NullAudioBackend final : public AudioBackend
{
public:
    BackendInfo getBackendInfo() const override;
    std::vector<DeviceInfo> enumerateDevices() override;
    void open(const AudioDeviceSettings& settings) override;
    void close() override;
    void start(AudioProcessCallback* callback) override;
    void stop() override;
    BackendPortMap getPortMap() const override;
    void refreshPortMapAsync() override;
    EffectiveAudioDeviceSettings getEffectiveSettings() const override;

    bool isOpen() const noexcept;
    bool isRunning() const noexcept;

private:
    EffectiveAudioDeviceSettings effectiveSettings;
    AudioProcessCallback* activeCallback = nullptr;
    bool opened = false;
    bool running = false;
};

} // namespace mixerpro
