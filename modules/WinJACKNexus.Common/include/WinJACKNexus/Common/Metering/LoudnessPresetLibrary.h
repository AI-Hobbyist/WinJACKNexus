#pragma once

#include <algorithm>
#include <vector>

#include <juce_core/juce_core.h>

namespace wjn::common
{

struct LoudnessPreset
{
    float targetLufs = -23.0f;
    float truePeakMaxDbtp = -1.0f;
    float toleranceLu = 0.5f;
};

struct LoudnessPresetDefinition
{
    juce::String id;
    juce::String name;
    LoudnessPreset values;
    bool custom = false;
};

class LoudnessPresetLibrary final
{
public:
    LoudnessPresetLibrary();
    const std::vector<LoudnessPresetDefinition>& getPresets() const noexcept;
    LoudnessPreset get(const juce::String& id) const noexcept;
    bool saveCustom(juce::String name, LoudnessPreset values);
    void refresh();

private:
    void addBuiltin(const juce::String& id, const juce::String& name, LoudnessPreset values);
    juce::File getPresetDirectory() const;
    std::vector<LoudnessPresetDefinition> presets;
};

} // namespace wjn::common
