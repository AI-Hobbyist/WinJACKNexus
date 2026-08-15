#include "MainComponent.h"

#include "CascadeDeviceSelector.h"
#include "DeviceItemCard.h"
#include "../DebugTrace.h"
#include <WinJACKNexus/Common/UI/CommonControls.h>
#include <WinJACKNexus/Common/UI/Theme.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace wjn::adapter
{
namespace
{

struct RecentFileComparator
{
    int compareElements (const juce::File& first, const juce::File& second) const
    {
        const auto firstTime = first.getLastModificationTime().toMilliseconds();
        const auto secondTime = second.getLastModificationTime().toMilliseconds();
        if (firstTime != secondTime)
            return firstTime > secondTime ? -1 : 1;

        return first.getFileName().compareIgnoreCase (second.getFileName());
    }
};

juce::String fromUtf8 (const char* value)
{
    return juce::String::fromUTF8 (value);
}

juce::String wasapiModeToString (juce::WASAPIDeviceMode mode)
{
    return mode == juce::WASAPIDeviceMode::exclusive ? "exclusive" : "shared";
}

juce::WASAPIDeviceMode wasapiModeFromString (const juce::String& value)
{
    return value.equalsIgnoreCase ("exclusive") ? juce::WASAPIDeviceMode::exclusive
                                                   : juce::WASAPIDeviceMode::shared;
}

class DeviceListSection final : public wjn::common::NexusPanel
{
public:
    DeviceListSection (juce::String title, bool input,
                                             CascadeDeviceSelector::AudioDeviceFilterSettings& audioFilterSettings,
                       bool midi = false, bool virtualAudio = false)
                : sectionTitle (std::move (title)), isInput (input), filterSettings (audioFilterSettings),
                    isMidi (midi), isVirtual (virtualAudio)
    {
        addAndMakeVisible (titleLabel);
        titleLabel.setText (sectionTitle, juce::dontSendNotification);
        titleLabel.setHeadingStyle();

        addAndMakeVisible (addButton);
        addButton.setButtonText (fromUtf8 ("添加设备"));
        addButton.onClick = [this]
        {
            auto addSelection = [this] (CascadeDeviceSelector::Selection selection)
            {
                addDevice (std::move (selection));
            };

            if (isMidi)
                CascadeDeviceSelector::showMidi (*this, isInput, addedDeviceIdentifiers,
                                                  std::move (addSelection));
            else if (isVirtual)
                CascadeDeviceSelector::showVirtual (*this, isInput, addedDeviceIdentifiers,
                                                    std::move (addSelection));
            else
                CascadeDeviceSelector::show (*this, isInput, addedDeviceIdentifiers,
                                             this->filterSettings,
                                             std::move (addSelection));
        };

        addAndMakeVisible (refreshButton);
        refreshButton.setButtonText (fromUtf8 ("刷新列表"));
        refreshButton.onClick = [this]
        {
            refreshDeviceList();
        };

        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&listContent, false);
        viewport.setScrollBarsShown (true, false);
        listContent.setOpaque (false);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        auto header = area.removeFromTop (32);
        addButton.setBounds (header.removeFromRight (104));
        header.removeFromRight (8);
        refreshButton.setBounds (header.removeFromRight (104));
        header.removeFromRight (8);
        titleLabel.setBounds (header);
        area.removeFromTop (8);
        viewport.setBounds (area);
        layoutCards();
    }

    std::vector<wjn::common::ClientMapping> collectMappings() const
    {
        std::vector<wjn::common::ClientMapping> mappings;
        for (const auto* card : cards)
        {
            const auto& data = card->getData();
            wjn::common::ClientMapping mapping;
            mapping.id = data.id;
            mapping.clientName = data.clientName;
            mapping.kind = data.midi ? "Midi" : "Audio";
            mapping.driver = data.driver;
            mapping.direction = data.input ? "In" : "Out";
            mapping.streamType = data.streamType;
            mapping.device = data.device;
            mapping.guid = data.midi ? data.midiDeviceIdentifier : data.audioDeviceName;
            mapping.sampleRate = data.sampleRate;
            mapping.paused = data.paused;
            mapping.wasapiMode = wasapiModeToString (data.wasapiMode);
            if (! data.midi)
                for (int channel = 0; channel < juce::jmax (1, data.channels); ++channel)
                    mapping.channels.push_back (channel);
            mappings.push_back (std::move (mapping));
        }
        return mappings;
    }

    void clearCards()
    {
        cards.clear (true);
        addedDeviceIdentifiers.clear();
        layoutCards();
    }

    void restoreMappings (const std::vector<wjn::common::ClientMapping>& mappings)
    {
        clearCards();
        for (const auto& mapping : mappings)
        {
            const auto mappingIsInput = mapping.direction.equalsIgnoreCase ("In");
            const auto mappingIsMidi = mapping.kind.equalsIgnoreCase ("Midi");
            if (mappingIsInput != isInput || mappingIsMidi != isMidi)
                continue;

            DeviceItemCard::Data data;
            data.id = mapping.id;
            data.clientName = mapping.clientName;
            data.driver = mapping.driver;
            data.streamType = mapping.streamType;
            data.device = mapping.device;
            data.channels = mappingIsMidi ? 0 : juce::jmax (1, static_cast<int> (mapping.channels.size()));
            data.sampleRate = mapping.sampleRate;
            data.midi = mappingIsMidi;
            data.input = mappingIsInput;
            data.paused = mapping.paused;
            data.wasapiMode = wasapiModeFromString (mapping.wasapiMode);
            if (mappingIsMidi)
                data.midiDeviceIdentifier = mapping.guid.isNotEmpty() ? mapping.guid : mapping.device;
            else
                data.audioDeviceName = mapping.guid.isNotEmpty() ? mapping.guid : mapping.device;

            addCard (std::move (data));
        }
    }

    void refresh()
    {
        for (auto* card : cards)
            card->refresh();
    }

    void releaseClients()
    {
        for (auto* card : cards)
            card->releaseClient();
    }

private:
    void refreshDeviceList()
    {
        repaint();
        listContent.repaint();
    }

    void addDevice (CascadeDeviceSelector::Selection selection)
    {
        debug::trace ("addDevice begin section=" + debug::pointerText (this)
                      + " midi=" + juce::String (selection.midi ? 1 : 0)
                      + " input=" + juce::String (isInput ? 1 : 0)
                      + " device=" + selection.device
                      + " identifier=" + selection.deviceIdentifier
                      + " channels=" + juce::String (selection.channels));
        const auto deviceIdentifier = selection.deviceIdentifier.isNotEmpty() ? selection.deviceIdentifier
                                                                                : selection.device;
        if (addedDeviceIdentifiers.contains (deviceIdentifier))
        {
            debug::trace ("addDevice duplicate identifier=" + deviceIdentifier);
            return;
        }

        DeviceItemCard::Data data;
        data.id = "cl-" + juce::String (cards.size() + 1).paddedLeft ('0', 3);
        data.clientName = makeClientName (selection.streamType, selection.midi);
        data.driver = selection.driver;
        data.streamType = selection.streamType;
        data.device = selection.device;
        if (selection.midi)
            data.midiDeviceIdentifier = selection.deviceIdentifier;
        else
            data.audioDeviceName = selection.deviceIdentifier;
        data.channels = selection.channels;
        data.midi = selection.midi;
        data.input = isInput;
        data.wasapiMode = selection.wasapiMode;
        debug::trace ("addDevice data ready client=" + data.clientName);

        addCard (std::move (data));
    }

    void addCard (DeviceItemCard::Data data)
    {
        const auto deviceIdentifier = data.midi ? data.midiDeviceIdentifier : data.audioDeviceName;
        const auto identifier = deviceIdentifier.isNotEmpty() ? deviceIdentifier : data.device;
        if (identifier.isEmpty() || addedDeviceIdentifiers.contains (identifier))
            return;

        const auto shouldStart = ! data.paused;
        data.paused = true;
        debug::trace ("addCard before card new");
        auto* card = new DeviceItemCard (
            std::move (data),
            [] (DeviceItemCard& card, juce::String name)
            {
                return card.renameClient (name);
            },
            [] (DeviceItemCard&) {},
            [this] (DeviceItemCard& card)
            {
                juce::Component::SafePointer<DeviceListSection> section (this);
                juce::Component::SafePointer<DeviceItemCard> cardToRemove (&card);
                juce::MessageManager::callAsync ([section, cardToRemove]
                {
                    if (section == nullptr || cardToRemove == nullptr)
                        return;

                    const auto& data = cardToRemove->getData();
                    const auto identifier = data.midi ? data.midiDeviceIdentifier : data.audioDeviceName;
                    section->addedDeviceIdentifiers.removeString (identifier.isNotEmpty() ? identifier : data.device);
                    section->cards.removeObject (cardToRemove, true);
                    section->layoutCards();
                });
            });
        debug::trace ("addCard after card new card=" + debug::pointerText (card));

        addedDeviceIdentifiers.add (identifier);
        cards.add (card);
        debug::trace ("addCard after cards.add count=" + juce::String (cards.size()));
        listContent.addAndMakeVisible (card);
        debug::trace ("addCard after listContent.addAndMakeVisible");
        layoutCards();
        if (shouldStart)
            card->setPaused (false);
        debug::trace ("addCard complete");
    }

    juce::String makeClientName (const juce::String& streamType, bool midi) const
    {
        if (midi)
        {
            const auto prefix = streamType == "Input" ? "WDM_MidiIn_" : "WDM_MidiOut_";
            return prefix + juce::String (cards.size() + 1).paddedLeft ('0', 2);
        }

        const auto isVirtualInput = streamType == "Loopback";
        const auto isPhysicalInput = isInput && ! isVirtualInput;
        const auto prefix = isVirtualInput ? "WDM_VirtualIn_"
                                           : (isPhysicalInput ? "WDM_AudioIn_" :
                                              (streamType == "Injector" ? "WDM_VirtualOut_" : "WDM_AudioOut_"));
        return prefix + juce::String (cards.size() + 1).paddedLeft ('0', 2);
    }

    void layoutCards()
    {
        constexpr int cardHeight = 190;
        auto width = juce::jmax (0, viewport.getWidth() - viewport.getScrollBarThickness());
        for (int index = 0; index < cards.size(); ++index)
            cards[index]->setBounds (0, index * (cardHeight + 8), width, cardHeight);

        listContent.setSize (width, juce::jmax (viewport.getHeight(), cards.size() * (cardHeight + 8)));
        viewport.setViewedComponent (&listContent, false);
    }

    juce::String sectionTitle;
    bool isInput = false;
    CascadeDeviceSelector::AudioDeviceFilterSettings& filterSettings;
    bool isMidi = false;
    bool isVirtual = false;
    wjn::common::NexusLabel titleLabel;
    wjn::common::NexusButton addButton;
    wjn::common::NexusButton refreshButton;
    wjn::common::NexusViewport viewport;
    juce::Component listContent;
    juce::OwnedArray<DeviceItemCard> cards;
    juce::StringArray addedDeviceIdentifiers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeviceListSection)
};

class TabPage final : public juce::Component
{
public:
    TabPage (juce::String inputTitle, juce::String outputTitle,
             CascadeDeviceSelector::AudioDeviceFilterSettings& filterSettings,
             bool midi = false, bool virtualAudio = false)
                : inputSection (std::move (inputTitle), true, filterSettings, midi, virtualAudio),
                    outputSection (std::move (outputTitle), false, filterSettings, midi, virtualAudio)
    {
        addAndMakeVisible (inputSection);
        addAndMakeVisible (outputSection);
    }

    std::vector<wjn::common::ClientMapping> collectMappings() const
    {
        auto mappings = inputSection.collectMappings();
        auto outputMappings = outputSection.collectMappings();
        mappings.insert (mappings.end(), outputMappings.begin(), outputMappings.end());
        return mappings;
    }

    void restoreMappings (const std::vector<wjn::common::ClientMapping>& mappings)
    {
        inputSection.restoreMappings (mappings);
        outputSection.restoreMappings (mappings);
    }

    void refresh()
    {
        inputSection.refresh();
        outputSection.refresh();
    }

    void releaseClients()
    {
        inputSection.releaseClients();
        outputSection.releaseClients();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        auto inputArea = area.removeFromTop (area.getHeight() / 2 - 8);
        area.removeFromTop (16);
        inputSection.setBounds (inputArea);
        outputSection.setBounds (area);
    }

private:
    DeviceListSection inputSection;
    DeviceListSection outputSection;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TabPage)
};

} // namespace

MainComponent::MainComponent()
{
    tabs.setTabBarDepth (42);
    tabs.setOutline (0);

    newConfigurationButton.setButtonText (fromUtf8 ("新建配置"));
    newConfigurationButton.onClick = [this]
    {
        createNewConfiguration();
    };

    openConfigurationButton.setButtonText (fromUtf8 ("打开配置"));
    openConfigurationButton.onClick = [this]
    {
        openConfiguration();
    };

    saveConfigurationButton.setButtonText (fromUtf8 ("保存配置"));
    saveConfigurationButton.onClick = [this]
    {
        saveConfiguration();
    };

    refreshConfigurationButton.setButtonText (fromUtf8 ("刷新存档"));
    refreshConfigurationButton.onClick = [this]
    {
        refreshConfigurationList();
    };

    configurationSelector.setTextWhenNothingSelected (fromUtf8 ("选择存档"));
    configurationSelector.setTooltip (fromUtf8 ("选择 adapter_saves 中的存档"));
    configurationSelector.onChange = [this]
    {
        loadSelectedConfiguration();
    };

    settingsButton.setButtonText (fromUtf8 ("全局设置"));
    settingsButton.onClick = [this]
    {
        showAudioFilterSettingsDialog();
    };

    loadGlobalSettings();

    tabs.addTab (fromUtf8 ("系统音频"),
                 wjn::common::theme::rackPanel,
                 new TabPage (fromUtf8 ("输入  |  WASAPI 捕获"),
                              fromUtf8 ("输出  |  WASAPI 渲染"),
                              audioDeviceFilterSettings),
                 true);
    tabs.addTab (fromUtf8 ("系统 MIDI"),
                 wjn::common::theme::rackPanel,
                 new TabPage (fromUtf8 ("输入  |  WinMM / WinRT MIDI"),
                              fromUtf8 ("输出  |  WinMM / WinRT MIDI"),
                              audioDeviceFilterSettings,
                              true),
                 true);

    addAndMakeVisible (newConfigurationButton);
    addAndMakeVisible (openConfigurationButton);
    addAndMakeVisible (saveConfigurationButton);
    addAndMakeVisible (refreshConfigurationButton);
    addAndMakeVisible (configurationSelector);
    addAndMakeVisible (settingsButton);
    addAndMakeVisible (tabs);

    refreshConfigurationList();
    const auto mostRecentConfiguration = configurationFiles.isEmpty()
                                             ? juce::File()
                                             : configurationFiles.getFirst();
    if (mostRecentConfiguration == juce::File()
        || ! loadConfigurationFile (mostRecentConfiguration, false))
    {
        applyConfiguration (wjn::common::AdapterConfig::createDefault());
        currentConfigurationFile = {};
        refreshConfigurationList();
    }
    setSize (960, 640);
    startTimerHz (20);
}

MainComponent::~MainComponent()
{
    stopTimer();
    for (int index = 0; index < tabs.getNumTabs(); ++index)
        if (auto* tab = dynamic_cast<TabPage*> (tabs.getTabContentComponent (index)))
            tab->releaseClients();
    openGlAcceleration.detach();
}

void MainComponent::timerCallback()
{
    openGlAcceleration.update (*this);
    for (int index = 0; index < tabs.getNumTabs(); ++index)
        if (auto* tab = dynamic_cast<TabPage*> (tabs.getTabContentComponent (index)))
            tab->refresh();
}

juce::File MainComponent::getGlobalConfigFile() const
{
    return juce::File::getSpecialLocation (juce::File::currentApplicationFile)
        .getParentDirectory().getChildFile ("config.json");
}

bool MainComponent::loadGlobalSettings()
{
    const auto file = getGlobalConfigFile();
    if (! file.existsAsFile())
        return false;

    const auto parsed = juce::JSON::parse (file);
    const auto* root = parsed.getDynamicObject();
    if (root == nullptr)
        return false;

    const auto format = root->getProperty ("format").toString();
    if (format.isNotEmpty() && format != "WinJACKNexus.AdapterGlobalConfig")
        return false;

    CascadeDeviceSelector::AudioDeviceFilterSettings loaded;
    loaded.virtualDevicePattern = root->hasProperty ("virtualDevicePattern")
        ? root->getProperty ("virtualDevicePattern").toString() : loaded.virtualDevicePattern;
    loaded.inputDevicePattern = root->hasProperty ("inputDevicePattern")
        ? root->getProperty ("inputDevicePattern").toString() : loaded.inputDevicePattern;
    loaded.outputDevicePattern = root->hasProperty ("outputDevicePattern")
        ? root->getProperty ("outputDevicePattern").toString() : loaded.outputDevicePattern;
    if (! CascadeDeviceSelector::areFilterPatternsValid (loaded))
        return false;

    audioDeviceFilterSettings = std::move (loaded);
    const auto openGlEnabled = root->hasProperty ("openGlAccelerationEnabled")
        && static_cast<bool> (root->getProperty ("openGlAccelerationEnabled"));
    openGlAcceleration.setEnabled (openGlEnabled);
    return true;
}

bool MainComponent::saveGlobalSettings() const
{
    auto root = juce::DynamicObject::Ptr (new juce::DynamicObject());
    root->setProperty ("format", "WinJACKNexus.AdapterGlobalConfig");
    root->setProperty ("version", 1);
    root->setProperty ("virtualDevicePattern", audioDeviceFilterSettings.virtualDevicePattern);
    root->setProperty ("inputDevicePattern", audioDeviceFilterSettings.inputDevicePattern);
    root->setProperty ("outputDevicePattern", audioDeviceFilterSettings.outputDevicePattern);
    root->setProperty ("openGlAccelerationEnabled", openGlAcceleration.isRequested());

    const auto file = getGlobalConfigFile();
    file.getParentDirectory().createDirectory();
    return file.replaceWithText (juce::JSON::toString (juce::var (root), false));
}

void MainComponent::createNewConfiguration()
{
    applyConfiguration (wjn::common::AdapterConfig::createDefault());
    currentConfigurationFile = {};
    refreshConfigurationList();
}

void MainComponent::openConfiguration()
{
    chooseConfigurationFile (false);
}

void MainComponent::saveConfiguration()
{
    if (currentConfigurationFile == juce::File())
    {
        chooseConfigurationFile (true);
        return;
    }

    if (! saveConfigurationToFile (currentConfigurationFile))
        showConfigurationError (fromUtf8 ("配置保存失败，请检查文件路径和权限。"));
}

juce::File MainComponent::getAdapterSavesDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::currentApplicationFile)
        .getParentDirectory()
        .getChildFile ("adapter_saves");
}

void MainComponent::refreshConfigurationList()
{
    const auto directory = getAdapterSavesDirectory();
    if (! directory.exists())
        directory.createDirectory();

    juce::Array<juce::File> discoveredFiles;
    directory.findChildFiles (discoveredFiles, juce::File::findFiles, false, "*.adapter");
    RecentFileComparator comparator;
    discoveredFiles.sort (comparator, true);
    configurationFiles = std::move (discoveredFiles);

    const auto selectedFile = currentConfigurationFile;
    updatingConfigurationSelector = true;
    configurationSelector.clear (juce::dontSendNotification);
    for (int index = 0; index < configurationFiles.size(); ++index)
        configurationSelector.addItem (configurationFiles[index].getFileNameWithoutExtension(), index + 1);

    const auto selectedIndex = selectedFile == juce::File()
                                   ? -1
                                   : configurationFiles.indexOf (selectedFile);
    configurationSelector.setSelectedId (selectedIndex >= 0 ? selectedIndex + 1 : 0,
                                         juce::dontSendNotification);
    updatingConfigurationSelector = false;
}

void MainComponent::loadSelectedConfiguration()
{
    if (updatingConfigurationSelector)
        return;

    const auto selectedIndex = configurationSelector.getSelectedId() - 1;
    if (selectedIndex >= 0 && selectedIndex < configurationFiles.size())
        loadConfigurationFile (configurationFiles[selectedIndex], true);
}

bool MainComponent::loadConfigurationFile (const juce::File& file, bool reportError)
{
    const auto configuration = wjn::common::AdapterConfig::loadFromFile (file);
    if (! configuration.isValid())
    {
        if (reportError)
            showConfigurationError (fromUtf8 ("配置文件无效或版本不受支持。"));
        return false;
    }

    applyConfiguration (configuration);
    currentConfigurationFile = file;
    if (file.getParentDirectory() == getAdapterSavesDirectory())
        file.setLastModificationTime (juce::Time::getCurrentTime());
    refreshConfigurationList();
    return true;
}

bool MainComponent::saveConfigurationToFile (const juce::File& file)
{
    if (file == juce::File())
        return false;

    const auto target = getAdapterSavesDirectory().getChildFile (
        file.getFileNameWithoutExtension()).withFileExtension ("adapter");
    const auto configuration = collectConfiguration();
    if (! configuration.saveToFile (target))
        return false;

    currentConfigurationFile = target;
    refreshConfigurationList();
    return true;
}

void MainComponent::chooseConfigurationFile (bool saveAs)
{
    if (configurationFileChooser != nullptr)
        return;

    const auto initialDirectory = currentConfigurationFile != juce::File()
                                      ? currentConfigurationFile
                                      : getAdapterSavesDirectory();
    const auto chooserTitle = saveAs ? fromUtf8 ("保存 Adapter 配置")
                                     : fromUtf8 ("打开 Adapter 配置");
    configurationFileChooser = std::make_unique<juce::FileChooser> (
        chooserTitle, initialDirectory, "*.adapter", true);

     const auto chooserFlags = saveAs ? (juce::FileBrowserComponent::saveMode
                                                     | juce::FileBrowserComponent::canSelectFiles)
                                                 : (juce::FileBrowserComponent::openMode
                                                     | juce::FileBrowserComponent::canSelectFiles);
    juce::Component::SafePointer<MainComponent> safeThis (this);
    configurationFileChooser->launchAsync (
        chooserFlags,
        [safeThis, saveAs] (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;

            const auto selectedFile = chooser.getResult();
            safeThis->configurationFileChooser.reset();
            if (selectedFile == juce::File())
                return;

            if (saveAs)
            {
                if (! safeThis->saveConfigurationToFile (selectedFile))
                {
                    safeThis->showConfigurationError (
                        fromUtf8 ("配置保存失败，请检查文件路径和权限。"));
                    return;
                }
                return;
            }

            safeThis->loadConfigurationFile (selectedFile, true);
        });
}

void MainComponent::applyConfiguration (const wjn::common::AdapterConfig& configuration)
{
    if (auto* audioPage = dynamic_cast<TabPage*> (tabs.getTabContentComponent (0)))
        audioPage->restoreMappings (configuration.clients);
    if (auto* midiPage = dynamic_cast<TabPage*> (tabs.getTabContentComponent (1)))
        midiPage->restoreMappings (configuration.clients);
}

wjn::common::AdapterConfig MainComponent::collectConfiguration()
{
    wjn::common::AdapterConfig configuration;
    configuration.created = juce::Time::getCurrentTime().toISO8601 (true);

    for (int index = 0; index < tabs.getNumTabs(); ++index)
        if (auto* page = dynamic_cast<TabPage*> (tabs.getTabContentComponent (index)))
        {
            auto mappings = page->collectMappings();
            configuration.clients.insert (configuration.clients.end(), mappings.begin(), mappings.end());
        }

    return configuration;
}

void MainComponent::showConfigurationError (const juce::String& message)
{
    juce::AlertWindow::showAsync (
        juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::WarningIcon)
            .withTitle (fromUtf8 ("配置操作失败"))
            .withMessage (message)
            .withButton (fromUtf8 ("确定")),
        nullptr);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (wjn::common::theme::darkCanvas);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    auto toolbar = area.removeFromTop (40).reduced (8, 6);
    configurationSelector.setBounds (toolbar.removeFromLeft (180));
    toolbar.removeFromLeft (8);
    refreshConfigurationButton.setBounds (toolbar.removeFromLeft (76));
    toolbar.removeFromLeft (8);
    settingsButton.setBounds (toolbar.removeFromRight (136));
    toolbar.removeFromRight (8);
    saveConfigurationButton.setBounds (toolbar.removeFromRight (88));
    toolbar.removeFromRight (8);
    openConfigurationButton.setBounds (toolbar.removeFromRight (88));
    toolbar.removeFromRight (8);
    newConfigurationButton.setBounds (toolbar.removeFromRight (88));
    tabs.setBounds (area);
}

void MainComponent::showAudioFilterSettingsDialog()
{
    auto* alert = new juce::AlertWindow (fromUtf8 ("全局设置"),
                                         fromUtf8 ("配置设备筛选规则和界面渲染选项。"),
                                         juce::MessageBoxIconType::NoIcon,
                                         this);
    alert->addTextEditor ("virtualDevicePattern", audioDeviceFilterSettings.virtualDevicePattern,
                          fromUtf8 ("虚拟设备正则"));
    alert->addTextEditor ("inputDevicePattern", audioDeviceFilterSettings.inputDevicePattern,
                          fromUtf8 ("录制设备正则"));
    alert->addTextEditor ("outputDevicePattern", audioDeviceFilterSettings.outputDevicePattern,
                          fromUtf8 ("播放设备正则"));
    auto* openGlAccelerationToggle = new juce::ToggleButton();
    openGlAccelerationToggle->setButtonText (fromUtf8 ("OpenGL 加速"));
    openGlAccelerationToggle->setToggleState (openGlAcceleration.isRequested(),
                                              juce::dontSendNotification);
    openGlAccelerationToggle->setSize (320, 24);
    alert->addCustomComponent (openGlAccelerationToggle);
    alert->addButton (fromUtf8 ("应用"), 1,
                      juce::KeyPress (juce::KeyPress::returnKey, 0, 0));
    alert->addButton (fromUtf8 ("取消"), 0,
                      juce::KeyPress (juce::KeyPress::escapeKey, 0, 0));

    juce::Component::SafePointer<MainComponent> safeThis (this);
    alert->enterModalState (true,
                            juce::ModalCallbackFunction::create ([safeThis, alert, openGlAccelerationToggle] (int result) mutable
                            {
                                const std::unique_ptr<juce::ToggleButton> toggleGuard (openGlAccelerationToggle);
                                if (safeThis == nullptr || result != 1)
                                    return;

                                CascadeDeviceSelector::AudioDeviceFilterSettings updated;
                                updated.virtualDevicePattern = alert->getTextEditorContents ("virtualDevicePattern");
                                updated.inputDevicePattern = alert->getTextEditorContents ("inputDevicePattern");
                                updated.outputDevicePattern = alert->getTextEditorContents ("outputDevicePattern");

                                if (! CascadeDeviceSelector::areFilterPatternsValid (updated))
                                {
                                    juce::AlertWindow::showAsync (
                                        juce::MessageBoxOptions()
                                            .withIconType (juce::MessageBoxIconType::WarningIcon)
                                            .withTitle (fromUtf8 ("设置无效"))
                                            .withMessage (fromUtf8 ("请输入有效的正则表达式。"))
                                            .withButton (fromUtf8 ("确定")),
                                        nullptr);
                                    return;
                                }

                                safeThis->audioDeviceFilterSettings = std::move (updated);
                                safeThis->openGlAcceleration.setEnabled (openGlAccelerationToggle->getToggleState());
                                if (! safeThis->saveGlobalSettings())
                                    safeThis->showConfigurationError (fromUtf8 ("全局设置保存失败，请检查文件路径和权限。"));
                            }),
                            true);
}

} // namespace wjn::adapter
