#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace wjn::common
{

struct ClientMapping
{
    juce::String id;
    juce::String clientName;
    juce::String kind = "Audio";
    juce::String driver = "WASAPI";
    juce::String direction = "Out";
    juce::String streamType = "Playback";
    juce::String device;
    juce::String guid;
    std::vector<int> channels;
    double sampleRate = 0.0;
    bool paused = true;
    juce::String wasapiMode = "shared";
};

struct AdapterConfig
{
    juce::String format = "WinJACKNexus.Adapter";
    int version = 1;
    juce::String created;
    std::vector<ClientMapping> clients;

    juce::var toJson() const;
    static AdapterConfig fromJson(const juce::var& json);
    bool saveToFile(const juce::File& file) const;
    static AdapterConfig loadFromFile(const juce::File& file);
    static AdapterConfig createDefault();

    bool isValid() const noexcept { return valid; }

private:
    static AdapterConfig invalidDefault();
    bool valid = true;
};

} // namespace wjn::common
