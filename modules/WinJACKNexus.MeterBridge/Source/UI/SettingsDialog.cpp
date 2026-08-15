#include "SettingsDialog.h"

namespace wjn::meterbridge
{

namespace
{
}

SettingsDialog::SettingsDialog (const wjn::common::TextCatalog& localeToUse,
                                 const MeterProject& project,
                                 const std::vector<wjn::common::LoudnessPresetDefinition>& presets)
    : locale (localeToUse)
{
    addSlider (0, locale.text ("meterbridge.settings.maxChannels", "最大通道数"),
               1.0, 4096.0, 1.0, project.channelLimit, "");
    addSlider (1, locale.text ("meterbridge.settings.historyWindow", "默认历史窗口"),
               30.0, 3600.0, 30.0, project.historyWindowSeconds, " s");
    addSlider (2, locale.text ("meterbridge.settings.silenceThreshold", "静音阈值"),
               -120.0, 0.0, 1.0, project.silenceThresholdDb, " dB");
    addSlider (3, locale.text ("meterbridge.settings.silenceDuration", "静音持续时间"),
               0.1, 3600.0, 0.1, project.silenceDurationSeconds, " s");
    addSlider (4, locale.text ("meterbridge.settings.peakHold", "Peak 保持时间"),
               0.0, 60.0, 0.1, project.peakHoldDurationSeconds, " s");
    addSlider (5, locale.text ("meterbridge.settings.logInterval", "日志保存间隔"),
               0.1, 3600.0, 0.1, project.logSaveIntervalSeconds, " s");

    logDirectoryLabel.setText (locale.text ("meterbridge.settings.logRoot", "日志根目录"),
                               juce::dontSendNotification);
    addAndMakeVisible (logDirectoryLabel);
    logDirectoryEditor.setText (project.logRootDirectory, false);
    addAndMakeVisible (logDirectoryEditor);
    browseButton.setButtonText (locale.text ("meterbridge.settings.browse", "浏览"));
    browseButton.onClick = [this] { chooseLogDirectory(); };
    addAndMakeVisible (browseButton);

    presetLabel.setText (locale.text ("meterbridge.settings.defaultPreset", "默认响度预设"),
                         juce::dontSendNotification);
    addAndMakeVisible (presetLabel);
    for (size_t index = 0; index < presets.size(); ++index)
    {
        presetIds.push_back (presets[index].id);
        presetBox.addItem (presets[index].name, static_cast<int> (index) + 1);
    }
    presetBox.addItem (locale.text ("meterbridge.settings.noPreset", "不使用预设"),
                       static_cast<int> (presets.size()) + 1);
    presetBox.setSelectedId (static_cast<int> (presets.size()) + 1, juce::dontSendNotification);
    for (size_t index = 0; index < presetIds.size(); ++index)
        if (presetIds[index] == project.defaultPresetId)
            presetBox.setSelectedId (static_cast<int> (index) + 1, juce::dontSendNotification);
    addAndMakeVisible (presetBox);

    openGlAccelerationToggle.setText (locale.text ("meterbridge.settings.opengl", "OpenGL 加速"));
    openGlAccelerationToggle.setToggleState (project.openGlAccelerationEnabled,
                                             juce::dontSendNotification);
    addAndMakeVisible (openGlAccelerationToggle);

    const std::array<juce::String, 7> metricNames {
        locale.text ("meterbridge.history.peak", "Peak"),
        locale.text ("meterbridge.history.rms", "RMS"),
        locale.text ("meterbridge.history.truePeak", "dBTP"),
        locale.text ("meterbridge.history.momentary", "Momentary"),
        locale.text ("meterbridge.history.shortTerm", "Short-term"),
        locale.text ("meterbridge.history.integrated", "Integrated"),
        locale.text ("meterbridge.history.lra", "LRA")
    };
    for (size_t index = 0; index < metricBadges.size(); ++index)
    {
        metricBadges[index].setText (metricNames[index]);
        metricBadges[index].setToggleState (project.visibleMetrics[index], juce::dontSendNotification);
        addAndMakeVisible (metricBadges[index]);
    }

    setSize (720, 360);
}

MeterProject SettingsDialog::getProject (MeterProject base) const
{
    base.channelLimit = static_cast<int> (sliders[0].getValue());
    base.historyWindowSeconds = static_cast<int> (sliders[1].getValue());
    base.silenceThresholdDb = static_cast<float> (sliders[2].getValue());
    base.silenceDurationSeconds = static_cast<float> (sliders[3].getValue());
    base.peakHoldDurationSeconds = static_cast<float> (sliders[4].getValue());
    base.logSaveIntervalSeconds = static_cast<float> (sliders[5].getValue());
    base.logRootDirectory = logDirectoryEditor.getText().trim();
    base.openGlAccelerationEnabled = openGlAccelerationToggle.getToggleState();
    base.defaultPresetId = juce::isPositiveAndBelow (presetBox.getSelectedId() - 1,
                                                       static_cast<int> (presetIds.size()))
        ? presetIds[static_cast<size_t> (presetBox.getSelectedId() - 1)] : juce::String();
    for (size_t index = 0; index < base.visibleMetrics.size(); ++index)
        base.visibleMetrics[index] = metricBadges[index].getToggleState();
    base.ensureDefaults();
    return base;
}

void SettingsDialog::resized()
{
    auto area = getLocalBounds().reduced (12);
    for (int index = 0; index < 6; ++index)
        layoutSlider (area.removeFromTop (34), index);

    auto logArea = area.removeFromTop (32);
    logDirectoryLabel.setBounds (logArea.removeFromLeft (130));
    browseButton.setBounds (logArea.removeFromRight (68).reduced (0, 2));
    logArea.removeFromRight (6);
    logDirectoryEditor.setBounds (logArea.reduced (0, 2));

    auto presetArea = area.removeFromTop (32);
    presetLabel.setBounds (presetArea.removeFromLeft (130));
    presetBox.setBounds (presetArea.reduced (0, 2));

    openGlAccelerationToggle.setBounds (area.removeFromTop (30).reduced (2));

    auto metrics = area.removeFromTop (30);
    const auto metricWidth = juce::jmax (1, metrics.getWidth() / static_cast<int> (metricBadges.size()));
    for (auto& badge : metricBadges)
        badge.setBounds (metrics.removeFromLeft (metricWidth).reduced (2));
}

void SettingsDialog::addSlider (int index, const juce::String& name, double minimum, double maximum,
                                double interval, double value, const juce::String& suffix)
{
    labels[static_cast<size_t> (index)].setText (name, juce::dontSendNotification);
    suffixes[static_cast<size_t> (index)] = suffix;
    addAndMakeVisible (labels[static_cast<size_t> (index)]);
    addAndMakeVisible (sliders[static_cast<size_t> (index)]);
    sliders[static_cast<size_t> (index)].setRange (minimum, maximum, interval);
    sliders[static_cast<size_t> (index)].setTextValueSuffix (suffix);
    sliders[static_cast<size_t> (index)].setValue (value, juce::dontSendNotification);
}

void SettingsDialog::layoutSlider (juce::Rectangle<int> area, int index)
{
    labels[static_cast<size_t> (index)].setBounds (area.removeFromLeft (170));
    sliders[static_cast<size_t> (index)].setBounds (area.reduced (0, 5));
}

void SettingsDialog::chooseLogDirectory()
{
    directoryChooser = std::make_unique<juce::FileChooser> (locale.text ("meterbridge.settings.chooseLogDirectory",
                                                                         "选择日志目录"),
                                                            juce::File (logDirectoryEditor.getText()));
    juce::Component::SafePointer<SettingsDialog> safeThis (this);
    directoryChooser->launchAsync (juce::FileBrowserComponent::openMode
                                       | juce::FileBrowserComponent::canSelectDirectories,
                                   [safeThis] (const juce::FileChooser& chooser)
    {
        if (safeThis != nullptr && chooser.getResult() != juce::File())
            safeThis->logDirectoryEditor.setText (chooser.getResult().getFullPathName(), false);
    });
}

void SettingsDialogLauncher::show (juce::Component& owner, const MeterProject& project,
                                   const wjn::common::TextCatalog& locale,
                                   const std::vector<wjn::common::LoudnessPresetDefinition>& presets,
                                   std::function<void(MeterProject)> onApply)
{
    auto* content = new SettingsDialog (locale, project, presets);
    auto* alert = new juce::AlertWindow (locale.text ("meterbridge.settings.title", "MeterBridge 设置"),
                                         locale.text ("meterbridge.settings.message", "Common UI 设置面板"),
                                         juce::AlertWindow::NoIcon);
    alert->addCustomComponent (content);
    alert->addButton (locale.text ("meterbridge.settings.apply", "应用"), 1);
    alert->addButton (locale.text ("meterbridge.settings.cancel", "取消"), 0);
    alert->setSize (820, 620);
    const auto projectCopy = project;
    alert->enterModalState (true,
        juce::ModalCallbackFunction::create ([alert, content, projectCopy,
                                              onApply = std::move (onApply)] (int result)
        {
            if (result == 1 && onApply != nullptr)
                onApply (content->getProject (projectCopy));
        }), true);
    juce::ignoreUnused (owner);
}

} // namespace wjn::meterbridge
