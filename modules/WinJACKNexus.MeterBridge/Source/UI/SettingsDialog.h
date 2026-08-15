#pragma once

#include "../Model/MeterProject.h"

#include <WinJACKNexus/Common/Localization/TextCatalog.h>
#include <WinJACKNexus/Common/Metering/LoudnessPresetLibrary.h>
#include <WinJACKNexus/Common/UI/CommonControls.h>
#include <WinJACKNexus/Common/UI/SettingsSlider.h>
#include <WinJACKNexus/Common/UI/ToggleBadgeControl.h>

#include <array>
#include <functional>
#include <vector>

namespace wjn::meterbridge
{

class SettingsDialog final : public juce::Component
{
public:
    SettingsDialog (const wjn::common::TextCatalog& locale,
                    const MeterProject& project,
                    const std::vector<wjn::common::LoudnessPresetDefinition>& presets);

    MeterProject getProject (MeterProject base) const;
    void resized() override;

private:
    void addSlider (int index, const juce::String& name, double minimum, double maximum,
                    double interval, double value, const juce::String& suffix);
    void layoutSlider (juce::Rectangle<int> area, int index);
    void chooseLogDirectory();

    const wjn::common::TextCatalog& locale;
    std::array<wjn::common::NexusLabel, 6> labels;
    std::array<wjn::common::SettingsSlider, 6> sliders;
    std::array<juce::String, 6> suffixes;
    wjn::common::NexusLabel logDirectoryLabel;
    wjn::common::NexusTextEditor logDirectoryEditor;
    wjn::common::NexusButton browseButton;
    wjn::common::NexusLabel presetLabel;
    juce::ComboBox presetBox;
    std::array<wjn::common::ToggleBadgeControl, 7> metricBadges;
    wjn::common::ToggleBadgeControl openGlAccelerationToggle;
    std::vector<juce::String> presetIds;
    std::unique_ptr<juce::FileChooser> directoryChooser;
};

class SettingsDialogLauncher
{
public:
    static void show (juce::Component& owner, const MeterProject& project,
                      const wjn::common::TextCatalog& locale,
                      const std::vector<wjn::common::LoudnessPresetDefinition>& presets,
                      std::function<void(MeterProject)> onApply);
};

} // namespace wjn::meterbridge
