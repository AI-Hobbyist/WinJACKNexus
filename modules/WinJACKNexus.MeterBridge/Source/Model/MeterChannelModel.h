#pragma once

#include "MeterProject.h"

namespace wjn::meterbridge
{

class MeterChannelModel final
{
public:
    explicit MeterChannelModel (MeterProject& projectToUse);

    int size() const noexcept;
    const MeterChannelState& get (int index) const;
    MeterChannelState& get (int index);
    bool addChannel();
    bool removeChannel (int index);
    void setChannelName (int index, juce::String name);
    void setPresetId (int index, juce::String presetId);
    void setRecord (int index, bool shouldRecord);
    void setGroupId (int index, juce::String groupId);
    void addGroup (juce::String name);
    bool renameGroup (juce::String groupId, juce::String name);
    bool removeGroup (juce::String groupId);
    juce::StringArray getGroupNames() const;
    int indexOf (const juce::String& channelId) const noexcept;

private:
    MeterProject& project;
};

} // namespace wjn::meterbridge
