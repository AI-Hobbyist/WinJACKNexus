#pragma once

#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>

namespace wjn::adapter::backend
{

struct AudioDeviceInfo
{
    juce::String name;
    juce::String identifier;
};

std::vector<AudioDeviceInfo> enumerateWasapiDevices (bool input,
                                                     juce::WASAPIDeviceMode mode);
std::vector<juce::MidiDeviceInfo> enumerateMidiDevices (bool input);
int detectWasapiChannelCount (const juce::String& deviceName, bool input,
                              juce::WASAPIDeviceMode mode);
double detectWasapiSampleRate (const juce::String& deviceName, bool input,
                               juce::WASAPIDeviceMode mode);
juce::String makeStableDeviceKey (const juce::String& kind,
                                  const juce::String& direction,
                                  const juce::String& streamType,
                                  const juce::String& identifier);

} // namespace wjn::adapter::backend
