#include "HistoryWindow.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace wjn::meterbridge
{
namespace
{
class HistoryWindowContent final : public juce::Component, private juce::Timer
{
public:
    HistoryWindowContent (const wjn::common::TextCatalog& localeToUse,
                          std::function<std::vector<wjn::common::HistorySample>()> historyProviderToUse,
                          std::function<wjn::common::LoudnessPreset()> presetProviderToUse,
                          std::function<int()> presetIdProviderToUse,
                          std::function<void (int)> presetIdSetterToUse,
                          int defaultWindowSeconds,
                          std::array<bool, 7> defaultVisibleMetrics,
                          std::vector<wjn::common::LoudnessPresetDefinition> presetDefinitionsToUse,
                          juce::StringArray targetNames,
                          std::function<void (int)> targetSetterToUse)
                : locale (localeToUse),
                    historyProvider (std::move (historyProviderToUse)),
          presetProvider (std::move (presetProviderToUse)),
          presetIdProvider (std::move (presetIdProviderToUse)),
          presetIdSetter (std::move (presetIdSetterToUse)),
          targetSetter (std::move (targetSetterToUse)),
          presetDefinitions (std::move (presetDefinitionsToUse))
    {
        addAndMakeVisible (chart);
        if (targetNames.size() > 0)
        {
            addAndMakeVisible (targetBox);
            targetBox.addItemList (targetNames, 1);
            targetBox.setSelectedId (1, juce::dontSendNotification);
            targetBox.onChange = [this]
            {
                if (targetSetter != nullptr)
                    targetSetter (targetBox.getSelectedId() - 1);
                updatePresetSelection();
                updateChart();
            };
        }
        addAndMakeVisible (standardBox);
        for (size_t index = 0; index < presetDefinitions.size(); ++index)
            standardBox.addItem (presetDefinitions[index].name, static_cast<int> (index) + 1);
        standardBox.addItem (locale.text ("meterbridge.history.noPreset", "不使用预设"),
                     static_cast<int> (presetDefinitions.size()) + 1);
        updatePresetSelection();
        standardBox.onChange = [this]
        {
            if (presetIdSetter != nullptr)
                presetIdSetter (standardBox.getSelectedId());
            updateChart();
        };
        addAndMakeVisible (windowSlider);
        addAndMakeVisible (scanValueLabel);
        addAndMakeVisible (exportButton);

        chart.setScanCallback ([this] (float position)
        {
            updateScanDetails (position);
        });

        windowSlider.setRange (30.0, 3600.0, 30.0);
        windowSlider.setTextValueSuffix (" s");
        windowSlider.setValue (juce::jlimit (30.0, 3600.0,
                                             static_cast<double> (defaultWindowSeconds)),
                               juce::dontSendNotification);
        windowSlider.setValueChangeCallback ([this] (double value)
        {
            windowSeconds = static_cast<int> (value);
            updateChart();
        });

        const std::array<juce::String, 7> names {
            locale.text ("meterbridge.history.peakShort", "PEAK"),
            locale.text ("meterbridge.history.rms", "RMS"),
            locale.text ("meterbridge.history.truePeak", "dBTP"),
            locale.text ("meterbridge.history.momentaryShort", "M"),
            locale.text ("meterbridge.history.shortTermShort", "S"),
            locale.text ("meterbridge.history.integratedShort", "I"),
            locale.text ("meterbridge.history.lra", "LRA")
        };
        for (size_t index = 0; index < metricBadges.size(); ++index)
        {
            metricBadges[index].setText (names[index]);
            metricBadges[index].setToggleState (defaultVisibleMetrics[index], juce::dontSendNotification);
            metricBadges[index].setStateChangeCallback ([this] (bool) { updateChart(); });
            addAndMakeVisible (metricBadges[index]);
        }

        exportButton.setButtonText (locale.text ("meterbridge.history.exportCsv", "导出 CSV"));
        exportButton.onClick = [this] { exportCsv(); };
        windowSeconds = static_cast<int> (windowSlider.getValue());
        updateChart();
        startTimerHz (4);
        setSize (1120, 680);
    }

    ~HistoryWindowContent() override
    {
        stopTimer();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        auto controls = area.removeFromTop (40);
        exportButton.setBounds (controls.removeFromRight (100).reduced (2));
        controls.removeFromRight (8);
        if (targetBox.getNumItems() > 0)
        {
            targetBox.setBounds (controls.removeFromLeft (180).reduced (2));
            controls.removeFromLeft (4);
        }
        standardBox.setBounds (controls.removeFromLeft (150).reduced (2));
        controls.removeFromLeft (4);
        windowSlider.setBounds (controls.removeFromLeft (230).reduced (0, 8));

        auto badges = area.removeFromTop (32);
        const auto badgeWidth = juce::jmax (1, badges.getWidth() / static_cast<int> (metricBadges.size()));
        for (auto& badge : metricBadges)
            badge.setBounds (badges.removeFromLeft (badgeWidth).reduced (2));
        scanValueLabel.setBounds (area.removeFromTop (24).reduced (2, 2));
        chart.setBounds (area);
    }

private:
    static const std::array<juce::Colour, 7>& seriesColours()
    {
        static const std::array<juce::Colour, 7> colours {
            juce::Colour (0xffe74c3c), juce::Colour (0xfff1c40f), juce::Colour (0xffe91e63),
            juce::Colour (0xff3498db), juce::Colour (0xff1abc9c), juce::Colour (0xff9b59b6),
            juce::Colour (0xffe67e22)
        };
        return colours;
    }

    void timerCallback() override
    {
        updateChart();
    }

    void updatePresetSelection()
    {
        auto selectedId = presetIdProvider != nullptr ? presetIdProvider() : 1;
        selectedId = juce::jlimit (1, juce::jmax (1, static_cast<int> (presetDefinitions.size()) + 1), selectedId);
        standardBox.setSelectedId (selectedId, juce::dontSendNotification);
    }

    void updateChart()
    {
        const auto samples = historyProvider != nullptr ? historyProvider() : std::vector<wjn::common::HistorySample>();
        const auto preset = presetProvider != nullptr ? presetProvider() : wjn::common::LoudnessPreset();
        const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime (windowSeconds);

        std::vector<wjn::common::HistorySample> visibleSamples;
        visibleSamples.reserve (samples.size());
        for (const auto& sample : samples)
            if (sample.timestamp >= cutoff)
                visibleSamples.push_back (sample);
        lastVisibleSamples = visibleSamples;

        const std::array<juce::String, 7> names {
            locale.text ("meterbridge.history.peak", "Peak"),
            locale.text ("meterbridge.history.rms", "RMS"),
            locale.text ("meterbridge.history.truePeak", "dBTP"),
            locale.text ("meterbridge.history.momentary", "Momentary"),
            locale.text ("meterbridge.history.shortTerm", "Short-term"),
            locale.text ("meterbridge.history.integrated", "Integrated"),
            locale.text ("meterbridge.history.lra", "LRA")
        };
        const std::array<float, 7> minimums { -60.0f, -60.0f, -60.0f, -60.0f, -60.0f, -60.0f, 0.0f };
        const std::array<float, 7> maximums { 12.0f, 12.0f, 12.0f, 12.0f, 12.0f, 12.0f, 72.0f };
        std::vector<wjn::common::MeterHistoryChart::Series> series;
        for (size_t metric = 0; metric < names.size(); ++metric)
        {
            if (! metricBadges[metric].getToggleState())
                continue;
            wjn::common::MeterHistoryChart::Series item;
            item.name = names[metric];
            item.colour = seriesColours()[metric];
            item.minimum = minimums[metric];
            item.maximum = maximums[metric];
            item.visible = true;
            item.values.reserve (visibleSamples.size());
            for (const auto& sample : visibleSamples)
                item.values.push_back (sample.values[metric]);
            series.push_back (std::move (item));
        }
        chart.setSeries (std::move (series));
        chart.setHistoryAvailable (! visibleSamples.empty());
                chart.setValueRange ("+12 dB", "-60 dB");
                chart.setValueScale (-60.0f, 12.0f, "dB");
        chart.setSecondaryValueRange ("72 LU", "0 LU");
                chart.setSecondaryValueScale (0.0f, 72.0f, "LU");
        chart.setReferenceLines ({
                        { locale.text ("meterbridge.history.target", "目标") + " " + juce::String (preset.targetLufs, 1),
                            juce::Colour (0xfff39c12),
              preset.targetLufs, -60.0f, 12.0f },
                        { locale.text ("meterbridge.history.truePeakReference", "TP") + " "
                                    + juce::String (preset.truePeakMaxDbtp, 1), juce::Colour (0xffc0392b),
              preset.truePeakMaxDbtp, -60.0f, 12.0f }
        });
    }

    void updateScanDetails (float position)
    {
        if (position < 0.0f || lastVisibleSamples.empty())
        {
            scanValueLabel.setText ({}, juce::dontSendNotification);
            return;
        }

        const auto sampleIndex = juce::jlimit (0, static_cast<int> (lastVisibleSamples.size()) - 1,
                                               juce::roundToInt (position
                                                                 * static_cast<float> (lastVisibleSamples.size() - 1)));
        const auto& sample = lastVisibleSamples[static_cast<size_t> (sampleIndex)];
        const std::array<juce::String, 7> names {
            locale.text ("meterbridge.history.peak", "Peak"),
            locale.text ("meterbridge.history.rms", "RMS"),
            locale.text ("meterbridge.history.truePeak", "dBTP"),
            locale.text ("meterbridge.history.momentary", "M"),
            locale.text ("meterbridge.history.shortTerm", "S"),
            locale.text ("meterbridge.history.integrated", "I"),
            locale.text ("meterbridge.history.lra", "LRA")
        };
        auto text = sample.timestamp.toString ("%H:%M:%S", true, true, true);
        for (size_t index = 0; index < sample.values.size(); ++index)
            text << "  " << names[index] << ": " << juce::String (sample.values[index], 1);
        scanValueLabel.setText (text, juce::dontSendNotification);
    }

    void exportCsv()
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            locale.text ("meterbridge.history.exportTitle", "导出历史 CSV"), juce::File(), "*.csv");
        juce::Component::SafePointer<HistoryWindowContent> safeThis (this);
        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                      | juce::FileBrowserComponent::canSelectFiles,
                                  [safeThis] (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr || chooser.getResult() == juce::File())
                return;
            const auto samples = safeThis->historyProvider != nullptr
                ? safeThis->historyProvider() : std::vector<wjn::common::HistorySample>();
            juce::String csv = "timestamp,peak_dbfs,rms_dbfs,true_peak_dbtp,momentary_lufs,short_term_lufs,integrated_lufs,lra_lu\r\n";
            for (const auto& sample : samples)
            {
                csv << sample.timestamp.toString ("%Y-%m-%d %H:%M:%S", true, true, true);
                for (const auto value : sample.values)
                    csv << "," << juce::String (value, 3);
                csv << "\r\n";
            }
            chooser.getResult().replaceWithText (csv);
        });
    }

    const wjn::common::TextCatalog& locale;
    std::function<std::vector<wjn::common::HistorySample>()> historyProvider;
    std::function<wjn::common::LoudnessPreset()> presetProvider;
    std::function<int()> presetIdProvider;
    std::function<void (int)> presetIdSetter;
    std::function<void (int)> targetSetter;
    std::vector<wjn::common::LoudnessPresetDefinition> presetDefinitions;
    wjn::common::MeterHistoryChart chart;
    juce::ComboBox targetBox;
    juce::ComboBox standardBox;
    wjn::common::SettingsSlider windowSlider;
    wjn::common::NexusLabel scanValueLabel;
    wjn::common::NexusButton exportButton;
    std::array<wjn::common::ToggleBadgeControl, 7> metricBadges;
    std::unique_ptr<juce::FileChooser> fileChooser;
    int windowSeconds = 30;
    std::vector<wjn::common::HistorySample> lastVisibleSamples;
};

} // namespace

HistoryWindow::HistoryWindow (juce::String title,
                               const wjn::common::TextCatalog& locale,
                               std::function<std::vector<wjn::common::HistorySample>()> historyProvider,
                               std::function<wjn::common::LoudnessPreset()> presetProvider,
                               std::function<int()> presetIdProvider,
                               std::function<void (int)> presetIdSetter,
                               int defaultWindowSeconds,
                               std::array<bool, 7> defaultVisibleMetrics,
                               std::vector<wjn::common::LoudnessPresetDefinition> presetDefinitions,
                               juce::StringArray targetNames,
                               std::function<void (int)> targetSetter)
    : juce::DialogWindow (std::move (title), juce::Colour (0xff121316), true)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new HistoryWindowContent (locale, std::move (historyProvider), std::move (presetProvider),
                                               std::move (presetIdProvider), std::move (presetIdSetter),
                                               defaultWindowSeconds, defaultVisibleMetrics,
                                               std::move (presetDefinitions), std::move (targetNames),
                                               std::move (targetSetter)), true);
    centreWithSize (1120, 680);
    setResizable (true, true);
    setVisible (true);
}

void HistoryWindow::closeButtonPressed()
{
    delete this;
}

} // namespace wjn::meterbridge
