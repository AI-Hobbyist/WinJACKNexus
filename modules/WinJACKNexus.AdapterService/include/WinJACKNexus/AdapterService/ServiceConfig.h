#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include <WinJACKNexus/AdapterBackend/DeviceFilter.h>

namespace wjn::adapter::service
{

struct ServiceClient
{
    juce::String id;
    juce::String clientName;
    bool enabled = true;
    juce::String status = "available";
    juce::String kind = "Audio";
    juce::String driver = "WASAPI";
    juce::String direction = "Out";
    juce::String streamType = "Playback";
    juce::String device;
    juce::String guid;
    std::vector<int> channels;
    double sampleRate = 0.0;
    juce::String wasapiMode = "shared";

    juce::String stableKey() const;
    bool isAvailable() const noexcept { return status.equalsIgnoreCase ("available"); }
};

struct ServiceConfig
{
    static constexpr int currentVersion = 1;

    juce::String format = "WinJACKNexus.AdapterService";
    int version = currentVersion;
    juce::String created;
    juce::String updated;
    bool autoRemoveLostDevices = false;
    wjn::adapter::backend::AudioDeviceFilterSettings filters;
    std::vector<ServiceClient> clients;

    juce::var toJson() const;
    static ServiceConfig fromJson(const juce::var& json, juce::String& error);
    static ServiceConfig loadFromFile(const juce::File& file, juce::String& error);
    static ServiceConfig createDefault();

    bool saveToFile(const juce::File& file, juce::String& error) const;
    bool validate(juce::String& error) const;
    bool isValid() const noexcept { return valid; }

private:
    static ServiceConfig invalidDefault();
    bool valid = true;
};

} // namespace wjn::adapter::service