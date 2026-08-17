#pragma once

#include "Audio/AudioSettings.h"
#include "Audio/RealtimeTypes.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace mixerpro
{

enum class PortDirection
{
    input,
    output
};

struct DeviceInfo
{
    juce::String identifier;
    juce::String displayName;
};

struct BackendInfo
{
    BackendKind kind = BackendKind::nullTest;
    juce::String displayName;
};

struct BackendPortIdentity
{
    BackendKind backend = BackendKind::nullTest;
    juce::String stableId;
    juce::String canonicalName;
    juce::String displayName;
    juce::StringArray aliases;
    PortDirection direction = PortDirection::input;
    int channelIndex = 0;
};

struct BackendPortMap
{
    std::vector<BackendPortIdentity> inputs;
    std::vector<BackendPortIdentity> outputs;
};

class AudioBackend
{
public:
    virtual ~AudioBackend() = default;

    virtual BackendInfo getBackendInfo() const = 0;
    virtual std::vector<DeviceInfo> enumerateDevices() = 0;
    virtual void open(const AudioDeviceSettings& settings) = 0;
    virtual void close() = 0;
    virtual void start(AudioProcessCallback* callback) = 0;
    virtual void stop() = 0;
    virtual BackendPortMap getPortMap() const = 0;
    virtual void refreshPortMapAsync() = 0;
    virtual EffectiveAudioDeviceSettings getEffectiveSettings() const = 0;
};

} // namespace mixerpro
