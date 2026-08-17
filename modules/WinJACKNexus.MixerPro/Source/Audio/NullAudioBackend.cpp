#include "Audio/NullAudioBackend.h"

namespace mixerpro
{

BackendInfo NullAudioBackend::getBackendInfo() const
{
    return { BackendKind::nullTest, "Null/Test" };
}

std::vector<DeviceInfo> NullAudioBackend::enumerateDevices()
{
    return { { "null-test", "Null/Test Offline Device" } };
}

void NullAudioBackend::open(const AudioDeviceSettings& settings)
{
    effectiveSettings = { BackendKind::nullTest,
                          settings.requestedSampleRate,
                          settings.requestedBlockSize };
    opened = true;
}

void NullAudioBackend::close()
{
    stop();
    opened = false;
}

void NullAudioBackend::start(AudioProcessCallback* callback)
{
    activeCallback = callback;
    running = opened && activeCallback != nullptr;
}

void NullAudioBackend::stop()
{
    running = false;
    activeCallback = nullptr;
}

BackendPortMap NullAudioBackend::getPortMap() const
{
    BackendPortMap map;

    for (int index = 0; index < 2; ++index)
    {
        const auto suffix = juce::String(index + 1);

        map.inputs.push_back({ BackendKind::nullTest,
                               "null-in-" + suffix,
                               "null:input_" + suffix,
                               "Null Input " + suffix,
                               {},
                               PortDirection::input,
                               index });

        map.outputs.push_back({ BackendKind::nullTest,
                                "null-out-" + suffix,
                                "null:output_" + suffix,
                                "Null Output " + suffix,
                                {},
                                PortDirection::output,
                                index });
    }

    return map;
}

void NullAudioBackend::refreshPortMapAsync()
{
}

EffectiveAudioDeviceSettings NullAudioBackend::getEffectiveSettings() const
{
    return effectiveSettings;
}

bool NullAudioBackend::isOpen() const noexcept
{
    return opened;
}

bool NullAudioBackend::isRunning() const noexcept
{
    return running;
}

} // namespace mixerpro
