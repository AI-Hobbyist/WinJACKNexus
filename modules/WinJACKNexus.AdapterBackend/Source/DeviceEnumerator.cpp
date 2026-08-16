#include <WinJACKNexus/AdapterBackend/DeviceEnumerator.h>

#include <memory>

#include <WinJACKNexus/Common/Audio/JackAudioOutput.h>

namespace wjn::adapter::backend
{

std::vector<AudioDeviceInfo> enumerateWasapiDevices (bool input, juce::WASAPIDeviceMode mode)
{
    std::vector<AudioDeviceInfo> devices;
    std::unique_ptr<juce::AudioIODeviceType> deviceType (
        juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (mode));
    if (deviceType == nullptr)
        return devices;

    deviceType->scanForDevices();
    for (const auto& deviceName : deviceType->getDeviceNames (input))
        devices.push_back ({ deviceName, deviceName });

    return devices;
}

std::vector<juce::MidiDeviceInfo> enumerateMidiDevices (bool input)
{
    const auto availableDevices = input ? juce::MidiInput::getAvailableDevices()
                                        : juce::MidiOutput::getAvailableDevices();
    std::vector<juce::MidiDeviceInfo> devices;
    devices.reserve (static_cast<size_t> (availableDevices.size()));
    for (const auto& device : availableDevices)
        devices.push_back (device);
    return devices;
}

int detectWasapiChannelCount (const juce::String& deviceName, bool input,
                              juce::WASAPIDeviceMode mode)
{
    if (deviceName.isEmpty())
        return 2;

    std::unique_ptr<juce::AudioIODeviceType> deviceType (
        juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (mode));
    if (deviceType == nullptr)
        return 2;

    deviceType->scanForDevices();
    std::unique_ptr<juce::AudioIODevice> device (
        deviceType->createDevice (input ? juce::String() : deviceName,
                                  input ? deviceName : juce::String()));
    if (device == nullptr)
        return 2;

    const auto channelNames = input ? device->getInputChannelNames()
                                    : device->getOutputChannelNames();
    return juce::jlimit (1, wjn::common::JackAudioOutput::maxChannels,
                         channelNames.size());
}

double detectWasapiSampleRate (const juce::String& deviceName, bool input,
                               juce::WASAPIDeviceMode mode)
{
    if (deviceName.isEmpty())
        return 0.0;

    std::unique_ptr<juce::AudioIODeviceType> deviceType (
        juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (mode));
    if (deviceType == nullptr)
        return 0.0;

    deviceType->scanForDevices();
    std::unique_ptr<juce::AudioIODevice> device (
        deviceType->createDevice (input ? juce::String() : deviceName,
                                  input ? deviceName : juce::String()));
    return device != nullptr ? device->getCurrentSampleRate() : 0.0;
}

juce::String makeStableDeviceKey (const juce::String& kind,
                                  const juce::String& direction,
                                  const juce::String& streamType,
                                  const juce::String& identifier)
{
    juce::StringArray parts { kind, direction, streamType, identifier };
    return parts.joinIntoString ("|").toLowerCase();
}

} // namespace wjn::adapter::backend
