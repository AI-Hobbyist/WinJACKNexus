#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "CascadeDeviceSelector.h"
#include <WinJACKNexus/Common/UI/CommonControls.h>
#include <WinJACKNexus/Common/Serialization/AdapterConfig.h>
#include <WinJACKNexus/Common/UI/OpenGLAcceleration.h>

namespace wjn::adapter
{

/** 顶层内容组件。
 *
 *  骨架阶段：仅填充暗色背景的空窗口。
 *  M1.1 将实现：TabbedComponent（Physical Audio / Virtual Playback / System MIDI），
 *  每页内 In | Out 上下分割；并接入 WinJACKNexus.Common 的 Theme 色板。
 */
class MainComponent final : public juce::Component,
                             private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    // Component
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void showAudioFilterSettingsDialog();
    juce::File getGlobalConfigFile() const;
    bool loadGlobalSettings();
    bool saveGlobalSettings() const;
    void createNewConfiguration();
    void openConfiguration();
    void saveConfiguration();
    void chooseConfigurationFile(bool saveAs);
    juce::File getAdapterSavesDirectory() const;
    void refreshConfigurationList();
    void loadSelectedConfiguration();
    bool loadConfigurationFile(const juce::File& file, bool showError);
    bool saveConfigurationToFile(const juce::File& file);
    void applyConfiguration(const wjn::common::AdapterConfig& configuration);
    wjn::common::AdapterConfig collectConfiguration();
    void showConfigurationError(const juce::String& message);

    CascadeDeviceSelector::AudioDeviceFilterSettings audioDeviceFilterSettings;
    juce::File currentConfigurationFile;
    juce::Array<juce::File> configurationFiles;
    std::unique_ptr<juce::FileChooser> configurationFileChooser;
    wjn::common::NexusButton newConfigurationButton;
    wjn::common::NexusButton openConfigurationButton;
    wjn::common::NexusButton saveConfigurationButton;
    wjn::common::NexusButton refreshConfigurationButton;
    juce::ComboBox configurationSelector;
    wjn::common::NexusButton settingsButton;
    wjn::common::NexusTabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    wjn::common::OpenGLAcceleration openGlAcceleration;
    bool updatingConfigurationSelector = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace wjn::adapter
