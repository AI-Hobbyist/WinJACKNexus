#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Engine/MeterAudioBridge.h"
#include "../Model/MeterChannelModel.h"
#include "../Model/MeterProject.h"

#include <WinJACKNexus/Common/Audio/MeterEngine.h>
#include <WinJACKNexus/Common/Audio/SilenceDetector.h>
#include <WinJACKNexus/Common/IO/CsvLogWriter.h>
#include <WinJACKNexus/Common/Localization/TextCatalog.h>
#include <WinJACKNexus/Common/Metering/HistoryTypes.h>
#include <WinJACKNexus/Common/Metering/LoudnessPresetLibrary.h>
#include <WinJACKNexus/Common/UI/ChannelCard.h>
#include <WinJACKNexus/Common/UI/CommonControls.h>
#include <WinJACKNexus/Common/UI/LcdDisplayControl.h>
#include <WinJACKNexus/Common/UI/OpenGLAcceleration.h>
#include <WinJACKNexus/Common/UI/ThemeContext.h>

#include <array>
#include <deque>
#include <map>
#include <memory>
#include <vector>

namespace wjn::meterbridge
{

class MeterGroupPanel;

class MeterBridgeMainComponent final : public juce::Component,
                                       private juce::Timer
{
public:
    explicit MeterBridgeMainComponent (const wjn::common::TextCatalog& locale);
    ~MeterBridgeMainComponent() override;

    void prepareForShutdown();
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    int indexOfCard (const wjn::common::ChannelCard* card) const noexcept;
    void configureCard (wjn::common::ChannelCard& card, int index);
    void rebuildCards();
    void rebuildGroups();
    void layoutCards();
    void connectJack();
    void consumeAudio();
    void updateStatus();
    void resetChannel (int index, bool resetSilenceDetector = true);
    void writeCsvRecord (int index, const wjn::common::MeterValues& values);
    void showHistory (int index);
    void showGlobalHistory();
    std::vector<int> globalHistoryChannelIndices (int targetIndex) const;
    std::vector<wjn::common::HistorySample> globalHistoryForTarget (int targetIndex) const;
    juce::StringArray globalHistoryTargets() const;
    wjn::common::LoudnessPreset globalPresetForTarget (int targetIndex) const;
    int globalPresetMenuIdForTarget (int targetIndex) const;
    void setGlobalPresetForTarget (int targetIndex, int menuId);
    void showSettings();
    void showSaveDialog();
    void showLoadDialog();
    void showSaveProjectPresetDialog();
    void showCustomPresetDialog();
    void addGroup();
    void addChannel();
    void removeChannel(int index);
    void removeGroup(const juce::String& groupId);
    bool renameGroup(const juce::String& groupId, const juce::String& name);
    void showEmptyAreaMenu(juce::Point<int> screenPosition);
    void showCardMenu(wjn::common::ChannelCard& card, juce::Point<int> screenPosition);
    void handleDrag(wjn::common::ChannelCard& card, juce::Point<int> delta,
                    bool started, bool shiftDown);
    void reorderDraggedItem(int horizontalPosition, int dragDelta);
    void swapChannelState(int first, int second);
    void refreshPresetLists();
    void refreshProjectPresetList();
    void saveProject (const juce::File& file);
    bool loadProject (const juce::File& file);
    void saveGlobalSettings();
    bool loadGlobalSettings();
    juce::File defaultProjectFile() const;
    juce::File projectPresetDirectory() const;
    juce::File projectPresetFileForName (const juce::String& name) const;
    juce::File defaultGlobalConfigFile() const;
    juce::File defaultLogDirectory() const;
    juce::String presetIdForMenuId (int menuId) const;
    juce::StringArray presetNames() const;
    juce::String groupIdForName (const juce::String& name) const;
    juce::String groupNameForId (const juce::String& id) const;
    void applyProjectSettings (MeterProject newProject);
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent& event) override;

    const wjn::common::TextCatalog& locale;
    MeterProject project;
    MeterChannelModel channelModel;
    wjn::common::LoudnessPresetLibrary presetLibrary;
    MeterAudioBridge audioBridge;
    wjn::common::CsvLogWriter csvLogWriter;
    std::vector<std::unique_ptr<wjn::common::ChannelCard>> cards;
    std::vector<wjn::common::ChannelCard*> cardLayoutOrder;
    std::vector<wjn::common::MeterEngine> meterEngines;
    std::vector<wjn::common::SilenceDetector> silenceDetectors;
    std::vector<wjn::common::MeterValues> latestValues;
    std::vector<std::deque<wjn::common::HistorySample>> histories;
    std::vector<juce::int64> lastHistoryTimesMs;
    std::vector<juce::int64> lastLogTimesMs;
    std::vector<juce::File> currentLogFiles;
    std::map<std::string, juce::Colour> channelColours;
    std::vector<std::unique_ptr<MeterGroupPanel>> groupPanels;
    juce::Component* dragTarget = nullptr;
    juce::Point<int> dragOrigin;
    std::vector<juce::File> projectPresetFiles;

    wjn::common::NexusViewport cardsViewport;
    juce::Component cardsContent;
    wjn::common::NexusButton saveButton;
    wjn::common::NexusButton loadButton;
    wjn::common::NexusButton saveProjectPresetButton;
    juce::ComboBox projectPresetBox;
    wjn::common::NexusButton refreshPresetsButton;
    wjn::common::NexusButton customPresetButton;
    wjn::common::NexusButton addChannelButton;
    wjn::common::NexusButton removeChannelButton;
    wjn::common::NexusButton addGroupButton;
    wjn::common::NexusButton resetAllButton;
    wjn::common::NexusButton globalHistoryButton;
    wjn::common::NexusButton settingsButton;
    wjn::common::LcdDisplayControl statusLcd;
    wjn::common::NexusPanel toolbarPanel;
    std::unique_ptr<juce::FileChooser> fileChooser;
    wjn::common::ThemeContext theme;
    wjn::common::OpenGLAcceleration openGlAcceleration;
    juce::String statusLineOne;
    juce::String statusLineTwo;
    juce::int64 lastJackAttemptMs = 0;
    bool shutdownPrepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterBridgeMainComponent)
};

} // namespace wjn::meterbridge
