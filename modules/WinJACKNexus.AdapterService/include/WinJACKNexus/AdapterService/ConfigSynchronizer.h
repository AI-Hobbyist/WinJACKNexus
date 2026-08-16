#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include <WinJACKNexus/AdapterService/ServiceConfig.h>

namespace wjn::adapter::service
{

struct DiscoveredDevice
{
    juce::String kind;
    juce::String driver;
    juce::String direction;
    juce::String streamType;
    juce::String device;
    juce::String guid;
    int channels = 0;
    double sampleRate = 0.0;
    juce::String wasapiMode = "shared";
    bool allowedForNew = true;

    juce::String stableKey() const;
};

struct SynchronizationResult
{
    bool changed = false;
    int added = 0;
    int removed = 0;
    juce::StringArray removedClientNames;
};

class ConfigSynchronizer final
{
public:
    static std::vector<DiscoveredDevice> enumerateCurrentDevices (
        const wjn::adapter::backend::AudioDeviceFilterSettings& filters);

    static SynchronizationResult synchronize (ServiceConfig& config,
                                              const std::vector<DiscoveredDevice>& devices,
                                              juce::String& error);

    static bool synchronizeFile (const juce::File& file, juce::String& error,
                                 SynchronizationResult* result = nullptr);
};

} // namespace wjn::adapter::service