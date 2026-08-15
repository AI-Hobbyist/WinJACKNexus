#include "MeterProject.h"

#include <algorithm>

namespace wjn::meterbridge
{

namespace
{

juce::var makeGroupValue(const MeterGroupState& group)
{
    auto object = juce::DynamicObject::Ptr (new juce::DynamicObject());
    object->setProperty ("id", group.id);
    object->setProperty ("name", group.name);
    return juce::var (object);
}

juce::var makeChannelValue(const MeterChannelState& channel)
{
    auto object = juce::DynamicObject::Ptr (new juce::DynamicObject());
    object->setProperty ("id", channel.id);
    object->setProperty ("name", channel.name);
    object->setProperty ("groupId", channel.groupId);
    object->setProperty ("record", channel.record);
    object->setProperty ("presetId", channel.presetId);
    return juce::var (object);
}

juce::var getRootProperty (const juce::DynamicObject& root,
                           const juce::String& name,
                           const juce::var& fallback)
{
    return root.hasProperty (name) ? root.getProperty (name) : fallback;
}

void applyGlobalProperties (const juce::DynamicObject& root, MeterProject& project)
{
    project.channelLimit = static_cast<int> (getRootProperty (root, "channelLimit", project.channelLimit));
    project.historyWindowSeconds = static_cast<int> (getRootProperty (root, "historyWindowSeconds",
                                                                       project.historyWindowSeconds));
    project.silenceThresholdDb = static_cast<float> (getRootProperty (
        root, "silenceResetThresholdDB", getRootProperty (root, "silenceThresholdDb", project.silenceThresholdDb)));
    project.silenceDurationSeconds = static_cast<float> (getRootProperty (
        root, "silenceResetDurationSeconds", getRootProperty (root, "silenceDurationSeconds",
                                                                project.silenceDurationSeconds)));
    project.peakHoldDurationSeconds = static_cast<float> (getRootProperty (root, "peakHoldDurationSeconds",
                                                                            project.peakHoldDurationSeconds));
    project.logRootDirectory = getRootProperty (root, "logRootDirectory", project.logRootDirectory).toString();
    project.logSaveIntervalSeconds = static_cast<float> (getRootProperty (root, "logSaveIntervalSeconds",
                                                                            project.logSaveIntervalSeconds));
    project.defaultPresetId = getRootProperty (root, "defaultPresetId", project.defaultPresetId).toString().trim();

    if (auto* metrics = root.getProperty ("visibleMetrics").getArray())
        for (int index = 0; index < juce::jmin (7, metrics->size()); ++index)
            project.visibleMetrics[static_cast<size_t> (index)] = static_cast<bool> (metrics->getUnchecked (index));
}

} // namespace

void MeterProject::ensureDefaults()
{
    channelLimit = juce::jlimit (1, 4096, channelLimit);
    historyWindowSeconds = juce::jlimit (30, 3600, historyWindowSeconds);
    silenceThresholdDb = juce::jlimit (-120.0f, 0.0f, silenceThresholdDb);
    silenceDurationSeconds = juce::jlimit (0.1f, 3600.0f, silenceDurationSeconds);
    peakHoldDurationSeconds = juce::jlimit (0.0f, 60.0f, peakHoldDurationSeconds);
    logSaveIntervalSeconds = juce::jlimit (0.1f, 3600.0f, logSaveIntervalSeconds);

    if (groups.empty())
        groups.push_back ({ "ungrouped", "Ungrouped" });

    if (channels.empty())
    {
        const auto defaultCount = juce::jmin (8, channelLimit);
        for (int index = 0; index < defaultCount; ++index)
            channels.push_back ({ "ch" + juce::String (index + 1),
                                  "In" + juce::String (index + 1),
                                  "ungrouped", defaultPresetId, false });
    }

    if (channels.size() > static_cast<size_t> (channelLimit))
        channels.resize (static_cast<size_t> (channelLimit));

    for (size_t index = 0; index < channels.size(); ++index)
    {
        auto& channel = channels[index];
        if (channel.id.isEmpty())
            channel.id = "ch" + juce::String (index + 1);
        if (channel.name.trim().isEmpty())
            channel.name = "In" + juce::String (index + 1);
        if (channel.groupId.isEmpty())
            channel.groupId = groups.front().id;
        if (channel.presetId.isEmpty())
            channel.presetId = defaultPresetId;
    }
}

juce::var MeterProject::toJson() const
{
    auto root = juce::DynamicObject::Ptr (new juce::DynamicObject());
    root->setProperty ("format", "JackMeterBridgeConfig");
    root->setProperty ("version", 1);
    root->setProperty ("channelLimit", channelLimit);
    root->setProperty ("historyWindowSeconds", historyWindowSeconds);
    root->setProperty ("silenceResetThresholdDB", silenceThresholdDb);
    root->setProperty ("silenceResetDurationSeconds", silenceDurationSeconds);
    root->setProperty ("peakHoldDurationSeconds", peakHoldDurationSeconds);
    root->setProperty ("logRootDirectory", logRootDirectory);
    root->setProperty ("logSaveIntervalSeconds", logSaveIntervalSeconds);
    root->setProperty ("defaultPresetId", defaultPresetId);

    juce::Array<juce::var> metrics;
    for (const auto visible : visibleMetrics)
        metrics.add (visible);
    root->setProperty ("visibleMetrics", metrics);

    juce::Array<juce::var> groupValues;
    for (const auto& group : groups)
        groupValues.add (makeGroupValue (group));
    root->setProperty ("groups", groupValues);

    juce::Array<juce::var> channelValues;
    for (const auto& channel : channels)
        channelValues.add (makeChannelValue (channel));
    root->setProperty ("channels", channelValues);
    return juce::var (root);
}

juce::var MeterProject::toGlobalJson() const
{
    auto root = juce::DynamicObject::Ptr (new juce::DynamicObject());
    root->setProperty ("format", "WinJACKNexus.MeterBridgeGlobalConfig");
    root->setProperty ("version", 1);
    root->setProperty ("channelLimit", channelLimit);
    root->setProperty ("historyWindowSeconds", historyWindowSeconds);
    root->setProperty ("silenceResetThresholdDB", silenceThresholdDb);
    root->setProperty ("silenceResetDurationSeconds", silenceDurationSeconds);
    root->setProperty ("peakHoldDurationSeconds", peakHoldDurationSeconds);
    root->setProperty ("logRootDirectory", logRootDirectory);
    root->setProperty ("logSaveIntervalSeconds", logSaveIntervalSeconds);
    root->setProperty ("openGlAccelerationEnabled", openGlAccelerationEnabled);
    root->setProperty ("defaultPresetId", defaultPresetId);

    juce::Array<juce::var> metrics;
    for (const auto visible : visibleMetrics)
        metrics.add (visible);
    root->setProperty ("visibleMetrics", metrics);
    return juce::var (root);
}

bool MeterProject::fromJson(const juce::var& value, juce::String& error)
{
    error.clear();
    const auto* root = value.getDynamicObject();
    if (root == nullptr)
    {
        error = "Meter configuration must be a JSON object";
        return false;
    }

    const auto format = root->getProperty ("format").toString();
    if (format.isNotEmpty() && format != "JackMeterBridgeConfig")
    {
        error = "Unsupported MeterBridge configuration format";
        return false;
    }

    applyGlobalProperties (*root, *this);

    groups.clear();
    if (auto* groupValues = root->getProperty ("groups").getArray())
    {
        for (const auto& groupValue : *groupValues)
        {
            if (auto* groupObject = groupValue.getDynamicObject())
            {
                const auto id = groupObject->getProperty ("id").toString().trim();
                const auto name = groupObject->getProperty ("name").toString().trim();
                if (id.isNotEmpty() && name.isNotEmpty())
                    groups.push_back ({ id, name });
            }
            else if (groupValue.toString().trim().isNotEmpty())
            {
                const auto name = groupValue.toString().trim();
                groups.push_back ({ name.toLowerCase().replaceCharacters (" \\/", "__"), name });
            }
        }
    }

    channels.clear();
    auto* channelValues = root->getProperty ("channels").getArray();
    if (channelValues == nullptr || channelValues->isEmpty())
    {
        error = "Meter configuration contains no channels";
        ensureDefaults();
        return false;
    }

    channelLimit = juce::jlimit (1, 4096, channelLimit);
    const auto count = juce::jmin (channelValues->size(), channelLimit);
    for (int index = 0; index < count; ++index)
    {
        const auto* channelObject = channelValues->getUnchecked (index).getDynamicObject();
        if (channelObject == nullptr)
            continue;

        MeterChannelState channel;
        const auto getChannelProperty = [channelObject] (const juce::String& name,
                                                         const juce::var& fallback)
        {
            return channelObject->hasProperty (name) ? channelObject->getProperty (name) : fallback;
        };
        channel.id = getChannelProperty ("id", "ch" + juce::String (index + 1)).toString();
        channel.name = getChannelProperty ("name", "In" + juce::String (index + 1)).toString();
        const auto legacyGroup = getChannelProperty ("group", "ungrouped");
        channel.groupId = getChannelProperty ("groupId", legacyGroup).toString();
        channel.presetId = getChannelProperty ("presetId", defaultPresetId).toString();
        channel.record = static_cast<bool> (getChannelProperty ("record", false));
        channels.push_back (std::move (channel));
    }

    ensureDefaults();
    return ! channels.empty();
}

bool MeterProject::applyGlobalJson(const juce::var& value, juce::String& error)
{
    error.clear();
    const auto* root = value.getDynamicObject();
    if (root == nullptr)
    {
        error = "Global MeterBridge configuration must be a JSON object";
        return false;
    }

    const auto format = root->getProperty ("format").toString();
    if (format.isNotEmpty() && format != "WinJACKNexus.MeterBridgeGlobalConfig")
    {
        error = "Unsupported global MeterBridge configuration format";
        return false;
    }

    applyGlobalProperties (*root, *this);
    openGlAccelerationEnabled = static_cast<bool> (getRootProperty (
        *root, "openGlAccelerationEnabled", openGlAccelerationEnabled));

    ensureDefaults();
    return true;
}

bool MeterProject::saveGlobalToFile(const juce::File& file, juce::String& error) const
{
    error.clear();
    if (file == juce::File())
    {
        error = "No global MeterBridge configuration file was selected";
        return false;
    }
    file.getParentDirectory().createDirectory();
    if (! file.replaceWithText (juce::JSON::toString (toGlobalJson(), false)))
    {
        error = "Unable to save global MeterBridge configuration";
        return false;
    }
    return true;
}

bool MeterProject::loadGlobalFromFile(const juce::File& file, MeterProject& project, juce::String& error)
{
    error.clear();
    if (! file.existsAsFile())
    {
        error = "Global MeterBridge configuration file does not exist";
        return false;
    }

    const auto parsed = juce::JSON::parse (file);
    if (parsed.isVoid())
    {
        error = "Global MeterBridge configuration JSON is invalid";
        return false;
    }
    return project.applyGlobalJson (parsed, error);
}

bool MeterProject::saveToFile(const juce::File& file, juce::String& error) const
{
    error.clear();
    if (file == juce::File())
    {
        error = "No meter configuration file was selected";
        return false;
    }
    file.getParentDirectory().createDirectory();
    if (! file.replaceWithText (juce::JSON::toString (toJson(), false)))
    {
        error = "Unable to save meter configuration";
        return false;
    }
    return true;
}

bool MeterProject::loadFromFile(const juce::File& file, MeterProject& project, juce::String& error)
{
    error.clear();
    if (! file.existsAsFile())
    {
        error = "Meter configuration file does not exist";
        return false;
    }

    const auto parsed = juce::JSON::parse (file);
    if (parsed.isVoid())
    {
        error = "Meter configuration JSON is invalid";
        return false;
    }
    return project.fromJson (parsed, error);
}

} // namespace wjn::meterbridge
