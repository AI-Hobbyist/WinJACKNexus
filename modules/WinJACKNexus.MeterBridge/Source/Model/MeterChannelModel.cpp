#include "MeterChannelModel.h"

namespace wjn::meterbridge
{

MeterChannelModel::MeterChannelModel (MeterProject& projectToUse)
    : project (projectToUse)
{
    project.ensureDefaults();
}

int MeterChannelModel::size() const noexcept
{
    return static_cast<int> (project.channels.size());
}

const MeterChannelState& MeterChannelModel::get (int index) const
{
    return project.channels.at (static_cast<size_t> (index));
}

MeterChannelState& MeterChannelModel::get (int index)
{
    return project.channels.at (static_cast<size_t> (index));
}

bool MeterChannelModel::addChannel()
{
    if (size() >= project.channelLimit)
        return false;

    const auto number = size() + 1;
    project.channels.push_back ({ "ch" + juce::String (number),
                                  "In" + juce::String (number),
                                  project.groups.front().id,
                                  project.defaultPresetId,
                                  false });
    return true;
}

bool MeterChannelModel::removeChannel (int index)
{
    if (! juce::isPositiveAndBelow (index, size()) || size() <= 1)
        return false;

    project.channels.erase (project.channels.begin() + index);
    return true;
}

void MeterChannelModel::setChannelName (int index, juce::String name)
{
    if (! juce::isPositiveAndBelow (index, size()))
        return;
    name = name.trim();
    if (name.isNotEmpty())
        get (index).name = std::move (name);
}

void MeterChannelModel::setPresetId (int index, juce::String presetId)
{
    if (juce::isPositiveAndBelow (index, size()) && presetId.isNotEmpty())
        get (index).presetId = std::move (presetId);
}

void MeterChannelModel::setRecord (int index, bool shouldRecord)
{
    if (juce::isPositiveAndBelow (index, size()))
        get (index).record = shouldRecord;
}

void MeterChannelModel::setGroupId (int index, juce::String groupId)
{
    if (! juce::isPositiveAndBelow (index, size()))
        return;
    for (const auto& group : project.groups)
        if (group.id == groupId)
        {
            get (index).groupId = std::move (groupId);
            return;
        }
}

void MeterChannelModel::addGroup (juce::String name)
{
    name = name.trim();
    if (name.isEmpty())
        return;

    auto baseId = name.toLowerCase().replaceCharacters (" \\/:*?\"<>|", "__________");
    if (baseId.isEmpty())
        baseId = "group";
    auto id = baseId;
    int suffix = 2;
    while (std::any_of (project.groups.begin(), project.groups.end(), [&id] (const auto& group)
                        { return group.id == id; }))
        id = baseId + "_" + juce::String (suffix++);
    project.groups.push_back ({ id, std::move (name) });
}

bool MeterChannelModel::renameGroup (juce::String groupId, juce::String name)
{
    name = name.trim();
    if (groupId == "ungrouped" || name.isEmpty())
        return false;
    for (auto& group : project.groups)
        if (group.id == groupId)
        {
            group.name = std::move (name);
            return true;
        }
    return false;
}

bool MeterChannelModel::removeGroup (juce::String groupId)
{
    if (groupId == "ungrouped")
        return false;
    const auto iterator = std::find_if (project.groups.begin(), project.groups.end(),
                                        [&groupId] (const auto& group) { return group.id == groupId; });
    if (iterator == project.groups.end())
        return false;

    for (auto& channel : project.channels)
        if (channel.groupId == groupId)
            channel.groupId = "ungrouped";
    project.groups.erase (iterator);
    return true;
}

juce::StringArray MeterChannelModel::getGroupNames() const
{
    juce::StringArray names;
    for (const auto& group : project.groups)
        names.add (group.name);
    return names;
}

int MeterChannelModel::indexOf (const juce::String& channelId) const noexcept
{
    for (size_t index = 0; index < project.channels.size(); ++index)
        if (project.channels[index].id == channelId)
            return static_cast<int> (index);
    return -1;
}

} // namespace wjn::meterbridge
