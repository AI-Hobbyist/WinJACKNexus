#include "LoudnessPresetLibrary.h"

namespace wjn::common
{

LoudnessPresetLibrary::LoudnessPresetLibrary()
{
    addBuiltin("spotify_normal", "Spotify", { -14.0f, -1.0f, 1.0f });
    addBuiltin("apple_music", "Apple Music", { -16.0f, -1.0f, 1.0f });
    addBuiltin("youtube", "YouTube", { -14.0f, -2.0f, 1.0f });
    addBuiltin("ebu_r128", "EBU R128", { -23.0f, -1.0f, 0.5f });
    addBuiltin("atsc_a85", "ATSC A/85", { -24.0f, -2.0f, 1.0f });
    addBuiltin("apple_podcast", "Apple Podcasts", { -16.0f, -1.0f, 1.0f });
    addBuiltin("acx_audible", "Audible / ACX", { -20.0f, -3.0f, 2.0f });
    addBuiltin("netflix", "Netflix", { -27.0f, -2.0f, 1.0f });
    addBuiltin("ebu_r128_s1", "EBU R128 s1", { -15.0f, -1.0f, 0.5f });
    refresh();
}

const std::vector<LoudnessPresetDefinition>& LoudnessPresetLibrary::getPresets() const noexcept { return presets; }

LoudnessPreset LoudnessPresetLibrary::get(const juce::String& id) const noexcept
{
    for (const auto& preset : presets)
        if (preset.id == id)
            return preset.values;
    return {};
}

void LoudnessPresetLibrary::addBuiltin(const juce::String& id, const juce::String& name, LoudnessPreset values)
{
    presets.push_back({ id, name, values, false });
}

juce::File LoudnessPresetLibrary::getPresetDirectory() const
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory().getChildFile("presets");
}

void LoudnessPresetLibrary::refresh()
{
    presets.erase(std::remove_if(presets.begin(), presets.end(),
        [](const auto& preset) { return preset.custom; }), presets.end());
    auto directory = getPresetDirectory();
    directory.createDirectory();
    for (const auto& file : directory.findChildFiles(juce::File::findFiles, false, "*.loudness"))
    {
        const auto parsed = juce::JSON::parse(file);
        if (auto* object = parsed.getDynamicObject())
        {
            const auto id = object->getProperty("id").toString().trim();
            const auto name = object->getProperty("name").toString().trim();
            if (id.isEmpty() || name.isEmpty())
                continue;
            const auto existing = std::find_if(presets.begin(), presets.end(),
                [&id](const auto& item) { return item.id == id; });
            if (existing == presets.end())
                presets.push_back({ id, name,
                    { static_cast<float>(object->getProperty("targetLufs")),
                      static_cast<float>(object->getProperty("truePeakMaxDbtp")),
                      static_cast<float>(object->getProperty("toleranceLu")) }, true });
        }
    }
}

bool LoudnessPresetLibrary::saveCustom(juce::String name, LoudnessPreset values)
{
    name = name.trim();
    if (name.isEmpty())
        return false;
    auto id = "custom_" + name.toLowerCase().replaceCharacters(" \\/:*?\"<>|", "__________").substring(0, 48);
    auto file = getPresetDirectory().getChildFile(id + ".loudness");
    auto object = juce::DynamicObject::Ptr(new juce::DynamicObject());
    object->setProperty("format", "WinJACKNexusLoudnessPreset");
    object->setProperty("version", 1);
    object->setProperty("id", id);
    object->setProperty("name", name);
    object->setProperty("targetLufs", values.targetLufs);
    object->setProperty("truePeakMaxDbtp", values.truePeakMaxDbtp);
    object->setProperty("toleranceLu", values.toleranceLu);
    if (!file.replaceWithText(juce::JSON::toString(juce::var(object))))
        return false;
    refresh();
    return true;
}

} // namespace wjn::common
