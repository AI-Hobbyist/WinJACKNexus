#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

namespace wjn::meterbridge
{

struct MeterGroupState
{
    juce::String id;
    juce::String name;
};

struct MeterChannelState
{
    juce::String id;
    juce::String name;
    juce::String groupId { "ungrouped" };
    juce::String presetId { "ebu_r128" };
    bool record = false;
};

struct MeterProject
{
    int channelLimit = 32;
    int historyWindowSeconds = 30;
    std::array<bool, 7> visibleMetrics { true, true, true, true, true, true, true };
    float silenceThresholdDb = -60.0f;
    float silenceDurationSeconds = 5.0f;
    float peakHoldDurationSeconds = 2.0f;
    juce::String logRootDirectory;
    float logSaveIntervalSeconds = 1.0f;
    bool openGlAccelerationEnabled = false;
    juce::String defaultPresetId { "ebu_r128" };
    std::vector<MeterGroupState> groups;
    std::vector<MeterChannelState> channels;

    void ensureDefaults();
    juce::var toJson() const;
    juce::var toGlobalJson() const;
    bool fromJson(const juce::var& value, juce::String& error);
    bool applyGlobalJson(const juce::var& value, juce::String& error);
    bool saveToFile(const juce::File& file, juce::String& error) const;
    bool saveGlobalToFile(const juce::File& file, juce::String& error) const;
    static bool loadFromFile(const juce::File& file, MeterProject& project, juce::String& error);
    static bool loadGlobalFromFile(const juce::File& file, MeterProject& project, juce::String& error);
};

} // namespace wjn::meterbridge
