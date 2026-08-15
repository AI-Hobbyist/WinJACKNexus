#pragma once

#include <WinJACKNexus/Common/Localization/TextCatalog.h>
#include <WinJACKNexus/Common/Metering/HistoryTypes.h>
#include <WinJACKNexus/Common/Metering/LoudnessPresetLibrary.h>
#include <WinJACKNexus/Common/UI/CommonControls.h>
#include <WinJACKNexus/Common/UI/MeterHistoryChart.h>
#include <WinJACKNexus/Common/UI/SettingsSlider.h>
#include <WinJACKNexus/Common/UI/ToggleBadgeControl.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace wjn::meterbridge
{

class HistoryWindow final : public juce::DialogWindow
{
public:
    HistoryWindow (juce::String title,
                   const wjn::common::TextCatalog& locale,
                   std::function<std::vector<wjn::common::HistorySample>()> historyProvider,
                   std::function<wjn::common::LoudnessPreset()> presetProvider,
                   std::function<int()> presetIdProvider,
                   std::function<void (int)> presetIdSetter,
                   int defaultWindowSeconds,
                   std::array<bool, 7> defaultVisibleMetrics,
                   std::vector<wjn::common::LoudnessPresetDefinition> presetDefinitions,
                   juce::StringArray targetNames = {},
                   std::function<void (int)> targetSetter = {});

    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HistoryWindow)
};

} // namespace wjn::meterbridge
