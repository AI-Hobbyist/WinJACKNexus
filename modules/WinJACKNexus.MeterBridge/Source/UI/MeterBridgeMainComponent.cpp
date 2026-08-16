#include "MeterBridgeMainComponent.h"

#include "HistoryWindow.h"
#include "SettingsDialog.h"

#include <WinJACKNexus/Common/UI/Theme.h>

#include <algorithm>
#include <array>
#include <limits>
#include <numeric>

namespace wjn::meterbridge
{

namespace
{
juce::Colour paletteColour (int index)
{
    static const std::array<juce::Colour, 6> colours {
        juce::Colour (0xff3b82f6), juce::Colour (0xff22c55e), juce::Colour (0xfff59e0b),
        juce::Colour (0xffef4444), juce::Colour (0xff8b5cf6), juce::Colour (0xff06b6d4)
    };
    return juce::isPositiveAndBelow (index, static_cast<int> (colours.size()))
        ? colours[static_cast<size_t> (index)] : colours[0];
}

}

class MeterGroupPanel final : public juce::Component
{
public:
    MeterGroupPanel (juce::String groupIdToUse, juce::String groupNameToUse)
        : groupId (std::move (groupIdToUse)), groupName (std::move (groupNameToUse))
    {
        addAndMakeVisible (titleLabel);
        titleLabel.setText (groupName, juce::dontSendNotification);
        titleLabel.setJustificationType (juce::Justification::centredLeft);
        titleLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        titleLabel.setColour (juce::Label::backgroundColourId, groupColour);
        titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
        titleLabel.setEditable (true, true, false);
        titleLabel.onTextChange = [this]
        {
            const auto requestedName = titleLabel.getText().trim();
            if (requestedName.isNotEmpty() && requestedName != groupName
                && onRenamed != nullptr && onRenamed (groupId, requestedName))
            {
                groupName = requestedName;
                return;
            }
            titleLabel.setText (groupName, juce::dontSendNotification);
        };
    }

    const juce::String& getGroupId() const noexcept { return groupId; }
    const juce::String& getGroupName() const noexcept { return groupName; }

    void setGroupName (juce::String newName)
    {
        groupName = std::move (newName);
        titleLabel.setText (groupName, juce::dontSendNotification);
    }

    void setRenameCallback (std::function<bool (const juce::String&, const juce::String&)> callback)
    {
        onRenamed = std::move (callback);
    }

    void setCards (std::vector<wjn::common::ChannelCard*> cardsToUse)
    {
        cards = std::move (cardsToUse);
        for (auto* card : cards)
            addAndMakeVisible (*card);
        resized();
    }

    bool contains (const wjn::common::ChannelCard* card) const
    {
        return std::find (cards.begin(), cards.end(), card) != cards.end();
    }

    wjn::common::ChannelCard* reorderCard (wjn::common::ChannelCard& draggedCard,
                                           int horizontalPosition, int dragDelta)
    {
        const auto dragged = std::find (cards.begin(), cards.end(), &draggedCard);
        if (dragged == cards.end())
            return nullptr;

        const auto currentIndex = static_cast<int> (std::distance (cards.begin(), dragged));
        if (dragDelta < 0 && currentIndex > 0)
        {
            auto* previous = cards[static_cast<size_t> (currentIndex - 1)];
            if (horizontalPosition < previous->getX() + previous->getWidth() / 2)
            {
                std::iter_swap (dragged, dragged - 1);
                resized();
                draggedCard.setTopLeftPosition (horizontalPosition, draggedCard.getY());
                return previous;
            }
        }
        else if (dragDelta > 0 && currentIndex + 1 < static_cast<int> (cards.size()))
        {
            auto* next = cards[static_cast<size_t> (currentIndex + 1)];
            if (horizontalPosition + draggedCard.getWidth() > next->getX() + next->getWidth() / 2)
            {
                std::iter_swap (dragged, dragged + 1);
                resized();
                draggedCard.setTopLeftPosition (horizontalPosition, draggedCard.getY());
                return next;
            }
        }
        return nullptr;
    }

    int requiredWidth() const noexcept
    {
        constexpr int cardWidth = 190;
        constexpr int cardGap = 10;
        return 16 + static_cast<int> (cards.size()) * cardWidth
             + juce::jmax (0, static_cast<int> (cards.size()) - 1) * cardGap;
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff18252f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 7.0f);
        g.setColour (groupColour);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 7.0f, 1.0f);
    }

    void resized() override
    {
        constexpr int cardWidth = 190;
        constexpr int cardGap = 10;
        titleLabel.setBounds (10, 6, juce::jmax (1, getWidth() - 20), 22);
        auto area = getLocalBounds().withTop (31).reduced (8, 0);
        for (auto* card : cards)
        {
            card->setBounds (area.removeFromLeft (cardWidth)
                                 .withHeight (juce::jmax (1, getHeight() - 39)));
            area.removeFromLeft (cardGap);
        }
    }

private:
    juce::String groupId;
    juce::String groupName;
    juce::Label titleLabel;
    juce::Colour groupColour { 0xff526575 };
    std::vector<wjn::common::ChannelCard*> cards;
    std::function<bool (const juce::String&, const juce::String&)> onRenamed;
};

MeterBridgeMainComponent::MeterBridgeMainComponent (const wjn::common::TextCatalog& localeToUse)
        : locale (localeToUse),
    channelModel (project)
{
    project.logRootDirectory = defaultLogDirectory().getFullPathName();
    loadProject (defaultProjectFile());
    loadGlobalSettings();

    addAndMakeVisible (toolbarPanel);
    addAndMakeVisible (statusLcd);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (saveProjectPresetButton);
    addAndMakeVisible (projectPresetBox);
    addAndMakeVisible (refreshPresetsButton);
    addAndMakeVisible (customPresetButton);
    addAndMakeVisible (addChannelButton);
    addAndMakeVisible (removeChannelButton);
    addAndMakeVisible (addGroupButton);
    addAndMakeVisible (resetAllButton);
    addAndMakeVisible (globalHistoryButton);
    addAndMakeVisible (settingsButton);
    addAndMakeVisible (cardsViewport);
    cardsViewport.setViewedComponent (&cardsContent, false);
    cardsViewport.setScrollBarsShown (false, true, true, false);
    cardsContent.addMouseListener (this, false);

    saveButton.setButtonText (locale.text ("meterbridge.toolbar.save", "保存"));
    loadButton.setButtonText (locale.text ("meterbridge.toolbar.load", "加载"));
    saveProjectPresetButton.setButtonText (
        locale.text ("meterbridge.toolbar.saveProjectPreset", "保存工程预设"));
    refreshPresetsButton.setButtonText (locale.text ("meterbridge.projectPreset.refresh", "刷新预设"));
    customPresetButton.setButtonText (locale.text ("meterbridge.toolbar.newLoudnessPreset", "新建响度预设"));
    addChannelButton.setButtonText (locale.text ("meterbridge.toolbar.addChannel", "添加通道"));
    removeChannelButton.setButtonText (locale.text ("meterbridge.toolbar.removeChannel", "删除通道"));
    addGroupButton.setButtonText (locale.text ("meterbridge.toolbar.addGroup", "添加分组"));
    resetAllButton.setButtonText (locale.text ("meterbridge.toolbar.resetAll", "全局重置"));
    globalHistoryButton.setButtonText (locale.text ("meterbridge.toolbar.globalHistory", "全局历史"));
    settingsButton.setButtonText (locale.text ("meterbridge.toolbar.settings", "设置"));

    saveButton.onClick = [this] { showSaveDialog(); };
    loadButton.onClick = [this] { showLoadDialog(); };
    saveProjectPresetButton.onClick = [this] { showSaveProjectPresetDialog(); };
    projectPresetBox.setTextWhenNothingSelected (locale.text ("meterbridge.projectPreset.placeholder",
                                                              "选择工程预设"));
    projectPresetBox.onChange = [this]
    {
        const auto index = projectPresetBox.getSelectedId() - 1;
        if (! juce::isPositiveAndBelow (index, static_cast<int> (projectPresetFiles.size())))
            return;
        if (loadProject (projectPresetFiles[static_cast<size_t> (index)]))
        {
            openGlAcceleration.setEnabled (project.openGlAccelerationEnabled);
            rebuildCards();
            connectJack();
            saveProject (defaultProjectFile());
            saveGlobalSettings();
        }
    };
    refreshPresetsButton.onClick = [this] { refreshPresetLists(); };
    customPresetButton.onClick = [this] { showCustomPresetDialog(); };
    addChannelButton.onClick = [this]
    {
        addChannel();
    };
    removeChannelButton.onClick = [this]
    {
        removeChannel (channelModel.size() - 1);
    };
    addGroupButton.onClick = [this] { addGroup(); };
    globalHistoryButton.onClick = [this] { showGlobalHistory(); };
    resetAllButton.onClick = [this]
    {
        for (int index = 0; index < channelModel.size(); ++index)
            resetChannel (index);
    };
    settingsButton.onClick = [this] { showSettings(); };

    statusLcd.setAccent (theme.colour ("accent"));
    statusLcd.setPowered (true);
    statusLcd.setContentPainter ([this] (juce::Graphics& graphics, juce::Rectangle<float> area,
                                         const juce::Font& font, juce::Colour colour)
    {
        graphics.setColour (colour);
        graphics.setFont (font);
        graphics.drawFittedText (statusLineOne, area.removeFromTop (area.getHeight() * 0.5f).toNearestInt(),
                                 juce::Justification::centredLeft, 1);
        graphics.drawFittedText (statusLineTwo, area.toNearestInt(), juce::Justification::centredLeft, 1);
    });
    openGlAcceleration.setEnabled (project.openGlAccelerationEnabled);

    refreshProjectPresetList();

    rebuildCards();
    connectJack();
    setSize (1280, 720);
    startTimerHz (30);
}

MeterBridgeMainComponent::~MeterBridgeMainComponent()
{
    prepareForShutdown();
}

void MeterBridgeMainComponent::prepareForShutdown()
{
    if (shutdownPrepared)
        return;

    shutdownPrepared = true;
    stopTimer();
    cardsContent.removeMouseListener (this);
    audioBridge.disconnect();
    openGlAcceleration.detach();
}

void MeterBridgeMainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme.colour ("darkCanvas"));
}

int MeterBridgeMainComponent::indexOfCard (const wjn::common::ChannelCard* card) const noexcept
{
    for (size_t index = 0; index < cards.size(); ++index)
        if (cards[index].get() == card)
            return static_cast<int> (index);
    return -1;
}

void MeterBridgeMainComponent::configureCard (wjn::common::ChannelCard& card, int index)
{
    const auto& state = channelModel.get (index);
    card.setTheme (theme);
    card.setActionLabels (locale.text ("meterbridge.channel.reset", "重置"),
                          locale.text ("meterbridge.channel.record", "记录"));
    card.setMeterThickness (14.0f);
    const auto savedColour = channelColours.find (state.id.toStdString());
    card.setCardColour (savedColour != channelColours.end() ? savedColour->second
                                                            : juce::Colour (0xff3b4c59));
    card.setChannelName (state.name);
    card.setGroupOptions (channelModel.getGroupNames());
    card.setGroupName (groupNameForId (state.groupId));
    card.setPresetOptions (presetNames(), presetLibrary.getPresets().size() > 0
        ? static_cast<int> (std::distance (presetLibrary.getPresets().begin(),
            std::find_if (presetLibrary.getPresets().begin(), presetLibrary.getPresets().end(),
                          [&state] (const auto& item) { return item.id == state.presetId; }))) + 1
        : 1);
    const auto preset = presetLibrary.get (state.presetId);
    card.setPreset (preset.targetLufs, preset.toleranceLu, preset.truePeakMaxDbtp);
    card.setPeakHoldDuration (project.peakHoldDurationSeconds);
    card.setRecordState (state.record);

    card.setOnReset ([this] (wjn::common::ChannelCard& source)
    {
        const auto selected = indexOfCard (&source);
        if (selected >= 0)
            resetChannel (selected);
    });
    card.setOnRecord ([this] (wjn::common::ChannelCard& source)
    {
        const auto selected = indexOfCard (&source);
        if (selected >= 0)
            channelModel.setRecord (selected, source.isRecording());
    });
    card.setOnRename ([this] (wjn::common::ChannelCard& source, const juce::String& name)
    {
        const auto selected = indexOfCard (&source);
        if (selected >= 0)
        {
            channelModel.setChannelName (selected, name);
            connectJack();
        }
    });
    card.setOnPresetChange ([this] (wjn::common::ChannelCard& source, int menuId)
    {
        const auto selected = indexOfCard (&source);
        if (selected >= 0)
        {
            auto presetId = presetIdForMenuId (menuId);
            if (presetId.isEmpty())
                presetId = project.defaultPresetId;
            channelModel.setPresetId (selected, presetId);
            const auto presetValue = presetLibrary.get (project.channels[static_cast<size_t> (selected)].presetId);
            source.setPreset (presetValue.targetLufs, presetValue.toleranceLu, presetValue.truePeakMaxDbtp);
            saveProject (defaultProjectFile());
        }
    });
    card.setOnGroupChange ([this] (wjn::common::ChannelCard& source, const juce::String& name)
    {
        const auto selected = indexOfCard (&source);
        if (selected >= 0)
        {
            channelModel.setGroupId (selected, groupIdForName (name));
            rebuildGroups();
        }
    });
    card.setOnDrag ([this] (wjn::common::ChannelCard& source, juce::Point<int> delta,
                            bool started, bool shiftDown)
    {
        handleDrag (source, delta, started, shiftDown);
    });
    card.setOnHistory ([this] (wjn::common::ChannelCard& source)
    {
        const auto selected = indexOfCard (&source);
        if (selected >= 0)
            showHistory (selected);
    });
    card.setOnContextMenu ([this] (wjn::common::ChannelCard& source, juce::Point<int> position)
    {
        showCardMenu (source, position);
    });
}

void MeterBridgeMainComponent::rebuildCards()
{
    project.ensureDefaults();
    groupPanels.clear();
    for (auto* card : cardLayoutOrder)
        if (card->getParentComponent() != nullptr)
            card->getParentComponent()->removeChildComponent (card);
    cardLayoutOrder.clear();
    cards.clear();
    cards.reserve (project.channels.size());
    for (int index = 0; index < channelModel.size(); ++index)
    {
        auto card = std::make_unique<wjn::common::ChannelCard> (index);
        configureCard (*card, index);
        cards.push_back (std::move (card));
    }

    meterEngines.resize (cards.size());
    silenceDetectors.resize (cards.size());
    latestValues.resize (cards.size());
    histories.resize (cards.size());
    lastHistoryTimesMs.resize (cards.size());
    lastLogTimesMs.resize (cards.size());
    currentLogFiles.resize (cards.size());
    for (size_t index = 0; index < cards.size(); ++index)
    {
        meterEngines[index].reset();
        silenceDetectors[index].setThresholdDb (project.silenceThresholdDb);
        silenceDetectors[index].setDurationSeconds (project.silenceDurationSeconds);
        latestValues[index] = meterEngines[index].getValues();
        histories[index].clear();
        lastHistoryTimesMs[index] = 0;
        lastLogTimesMs[index] = 0;
        currentLogFiles[index] = {};
        cards[index]->resetMeterValues (latestValues[index]);
    }
    for (auto& card : cards)
        cardLayoutOrder.push_back (card.get());
    rebuildGroups();
}

void MeterBridgeMainComponent::rebuildGroups()
{
    groupPanels.clear();
    for (auto& card : cards)
    {
        const auto index = indexOfCard (card.get());
        if (index < 0)
            continue;
        card->setGroupOptions (channelModel.getGroupNames());
        card->setGroupName (groupNameForId (channelModel.get (index).groupId));
    }

    for (auto* card : cardLayoutOrder)
        if (card->getParentComponent() != nullptr)
            card->getParentComponent()->removeChildComponent (card);

    for (const auto& group : project.groups)
    {
        if (group.id == "ungrouped")
            continue;

        std::vector<wjn::common::ChannelCard*> groupedCards;
        for (auto* card : cardLayoutOrder)
        {
            const auto index = indexOfCard (card);
            if (index >= 0 && channelModel.get (index).groupId == group.id)
                groupedCards.push_back (card);
        }
        if (groupedCards.empty())
            continue;

        auto panel = std::make_unique<MeterGroupPanel> (group.id, group.name);
        panel->setCards (std::move (groupedCards));
        panel->setRenameCallback ([this] (const juce::String& groupId, const juce::String& name)
        {
            return renameGroup (groupId, name);
        });
        cardsContent.addAndMakeVisible (*panel);
        groupPanels.push_back (std::move (panel));
    }

    for (auto* card : cardLayoutOrder)
        if (card->getParentComponent() == nullptr)
            cardsContent.addAndMakeVisible (*card);

    layoutCards();
}

void MeterBridgeMainComponent::layoutCards()
{
    constexpr int cardWidth = 190;
    constexpr int itemGap = 14;
    const auto height = juce::jmax (1, cardsViewport.getHeight());
    auto area = juce::Rectangle<int> (0, 0, juce::jmax (1, cardsContent.getWidth()), height)
                    .reduced (10, 8);
    auto contentWidth = 10;

    for (auto& panel : groupPanels)
    {
        const auto width = panel->requiredWidth();
        panel->setBounds (area.removeFromLeft (width).withHeight (juce::jmax (1, height - 16)));
        area.removeFromLeft (itemGap);
        contentWidth += width + itemGap;
    }

    for (auto* card : cardLayoutOrder)
    {
        if (card->getParentComponent() != &cardsContent)
            continue;
        card->setBounds (area.removeFromLeft (cardWidth).withHeight (height));
        area.removeFromLeft (itemGap);
        contentWidth += cardWidth + itemGap;
    }

    contentWidth += 10;
    cardsContent.setSize (juce::jmax (1, contentWidth), height);
}

void MeterBridgeMainComponent::connectJack()
{
    lastJackAttemptMs = juce::Time::currentTimeMillis();
    juce::StringArray names;
    for (const auto& channel : project.channels)
        names.add (channel.name);
    audioBridge.connect (names);
    updateStatus();
}

void MeterBridgeMainComponent::addChannel()
{
    if (channelModel.addChannel())
    {
        rebuildCards();
        connectJack();
    }
}

void MeterBridgeMainComponent::removeChannel (int index)
{
    if (channelModel.removeChannel (index))
    {
        rebuildCards();
        connectJack();
    }
}

void MeterBridgeMainComponent::removeGroup (const juce::String& groupId)
{
    if (channelModel.removeGroup (groupId))
        rebuildCards();
}

bool MeterBridgeMainComponent::renameGroup (const juce::String& groupId, const juce::String& name)
{
    if (name.trim().isEmpty() || groupId == "ungrouped")
        return false;

    for (const auto& group : project.groups)
        if (group.name == name.trim())
            return false;

    if (! channelModel.renameGroup (groupId, name))
        return false;

    const auto groupNames = channelModel.getGroupNames();
    for (size_t index = 0; index < cards.size(); ++index)
    {
        cards[index]->setGroupOptions (groupNames);
        cards[index]->setGroupName (groupNameForId (project.channels[index].groupId));
    }
    for (auto& panel : groupPanels)
        if (panel->getGroupId() == groupId)
            panel->setGroupName (name.trim());
    return true;
}

void MeterBridgeMainComponent::swapChannelState (int first, int second)
{
    if (! juce::isPositiveAndBelow (first, channelModel.size())
        || ! juce::isPositiveAndBelow (second, channelModel.size()))
        return;

    std::swap (project.channels[static_cast<size_t> (first)],
               project.channels[static_cast<size_t> (second)]);
    std::swap (cards[static_cast<size_t> (first)],
               cards[static_cast<size_t> (second)]);
    std::swap (meterEngines[static_cast<size_t> (first)],
               meterEngines[static_cast<size_t> (second)]);
    std::swap (silenceDetectors[static_cast<size_t> (first)],
               silenceDetectors[static_cast<size_t> (second)]);
    std::swap (latestValues[static_cast<size_t> (first)],
               latestValues[static_cast<size_t> (second)]);
    std::swap (histories[static_cast<size_t> (first)],
               histories[static_cast<size_t> (second)]);
    std::swap (lastHistoryTimesMs[static_cast<size_t> (first)],
               lastHistoryTimesMs[static_cast<size_t> (second)]);
    std::swap (lastLogTimesMs[static_cast<size_t> (first)],
               lastLogTimesMs[static_cast<size_t> (second)]);
    std::swap (currentLogFiles[static_cast<size_t> (first)],
               currentLogFiles[static_cast<size_t> (second)]);
}

void MeterBridgeMainComponent::handleDrag (wjn::common::ChannelCard& card,
                                            juce::Point<int> delta,
                                            bool started,
                                            bool shiftDown)
{
    if (started)
    {
        dragTarget = &card;
        dragOrigin = card.getPosition();

        if (! shiftDown)
            for (auto& panel : groupPanels)
                if (panel->contains (&card))
                {
                    dragTarget = panel.get();
                    dragOrigin = panel->getPosition();
                    break;
                }
        return;
    }

    if (dragTarget == nullptr)
        return;

    constexpr int gridSize = 10;
    const auto snappedDelta = juce::roundToInt (static_cast<float> (delta.x) / gridSize) * gridSize;
    const auto horizontalPosition = dragOrigin.x + snappedDelta;
    dragTarget->setTopLeftPosition (horizontalPosition, dragOrigin.y);
    reorderDraggedItem (horizontalPosition, snappedDelta);
}

void MeterBridgeMainComponent::reorderDraggedItem (int horizontalPosition, int dragDelta)
{
    if (auto* draggedPanel = dynamic_cast<MeterGroupPanel*> (dragTarget))
    {
        const auto dragged = std::find_if (groupPanels.begin(), groupPanels.end(),
                                           [draggedPanel] (const auto& panel)
                                           { return panel.get() == draggedPanel; });
        if (dragged == groupPanels.end())
            return;

        const auto currentIndex = static_cast<int> (std::distance (groupPanels.begin(), dragged));
        MeterGroupPanel* neighbour = nullptr;
        auto neighbourIterator = groupPanels.end();
        if (dragDelta < 0 && currentIndex > 0)
            neighbourIterator = dragged - 1;
        else if (dragDelta > 0 && currentIndex + 1 < static_cast<int> (groupPanels.size()))
            neighbourIterator = dragged + 1;

        if (neighbourIterator != groupPanels.end())
        {
            neighbour = neighbourIterator->get();
            const auto draggedCentre = horizontalPosition + draggedPanel->getWidth() / 2;
            const auto neighbourCentre = neighbour->getX() + neighbour->getWidth() / 2;
            const auto crossed = dragDelta < 0 ? draggedCentre < neighbourCentre
                                               : draggedCentre > neighbourCentre;
            if (crossed)
            {
                const auto draggedGroup = std::find_if (project.groups.begin(), project.groups.end(),
                    [draggedPanel] (const auto& group) { return group.id == draggedPanel->getGroupId(); });
                const auto neighbourGroup = std::find_if (project.groups.begin(), project.groups.end(),
                    [neighbour] (const auto& group) { return group.id == neighbour->getGroupId(); });
                const auto draggedGroupIndex = draggedGroup != project.groups.end()
                    ? static_cast<int> (std::distance (project.groups.begin(), draggedGroup)) : -1;
                const auto neighbourGroupIndex = neighbourGroup != project.groups.end()
                    ? static_cast<int> (std::distance (project.groups.begin(), neighbourGroup)) : -1;
                std::iter_swap (dragged, neighbourIterator);
                if (juce::isPositiveAndBelow (draggedGroupIndex, static_cast<int> (project.groups.size()))
                    && juce::isPositiveAndBelow (neighbourGroupIndex, static_cast<int> (project.groups.size())))
                    std::iter_swap (project.groups.begin() + draggedGroupIndex,
                                    project.groups.begin() + neighbourGroupIndex);
                layoutCards();
                draggedPanel->setTopLeftPosition (horizontalPosition, draggedPanel->getY());
                dragOrigin.x = horizontalPosition - dragDelta;
            }
        }
        return;
    }

    auto* draggedCard = dynamic_cast<wjn::common::ChannelCard*> (dragTarget);
    if (draggedCard == nullptr)
        return;

    for (auto& panel : groupPanels)
        if (panel->contains (draggedCard))
        {
            auto* neighbourCard = panel->reorderCard (*draggedCard, horizontalPosition, dragDelta);
            if (neighbourCard != nullptr)
            {
                const auto first = indexOfCard (draggedCard);
                const auto second = indexOfCard (neighbourCard);
                if (first >= 0 && second >= 0)
                {
                    auto draggedOrder = std::find (cardLayoutOrder.begin(), cardLayoutOrder.end(), draggedCard);
                    auto neighbourOrder = std::find (cardLayoutOrder.begin(), cardLayoutOrder.end(), neighbourCard);
                    if (draggedOrder != cardLayoutOrder.end() && neighbourOrder != cardLayoutOrder.end())
                        std::iter_swap (draggedOrder, neighbourOrder);
                    swapChannelState (first, second);
                    connectJack();
                }
            }
            return;
        }

    if (draggedCard->getParentComponent() != &cardsContent)
        return;

    std::vector<wjn::common::ChannelCard*> directCards;
    for (auto* card : cardLayoutOrder)
        if (card->getParentComponent() == &cardsContent)
            directCards.push_back (card);

    const auto dragged = std::find (directCards.begin(), directCards.end(), draggedCard);
    if (dragged == directCards.end())
        return;

    const auto currentIndex = static_cast<int> (std::distance (directCards.begin(), dragged));
    auto neighbour = directCards.end();
    if (dragDelta < 0 && currentIndex > 0)
        neighbour = dragged - 1;
    else if (dragDelta > 0 && currentIndex + 1 < static_cast<int> (directCards.size()))
        neighbour = dragged + 1;

    if (neighbour == directCards.end())
        return;

    auto* neighbourCard = *neighbour;
    const auto crossed = dragDelta < 0
        ? horizontalPosition < neighbourCard->getX() + neighbourCard->getWidth() / 2
        : horizontalPosition + draggedCard->getWidth() > neighbourCard->getX() + neighbourCard->getWidth() / 2;
    if (! crossed)
        return;

    const auto first = indexOfCard (draggedCard);
    const auto second = indexOfCard (neighbourCard);
    if (first < 0 || second < 0)
        return;

    std::iter_swap (std::find (cardLayoutOrder.begin(), cardLayoutOrder.end(), draggedCard),
                    std::find (cardLayoutOrder.begin(), cardLayoutOrder.end(), neighbourCard));
    swapChannelState (first, second);
    layoutCards();
    dragOrigin.x = horizontalPosition - dragDelta;
    connectJack();
}

void MeterBridgeMainComponent::showEmptyAreaMenu (juce::Point<int> screenPosition)
{
    juce::PopupMenu menu;
    menu.addItem (1, locale.text ("meterbridge.menu.addChannel", "添加通道"));
    menu.addItem (2, locale.text ("meterbridge.menu.addGroup", "添加分组"));
    menu.addItem (3, locale.text ("meterbridge.menu.globalHistory", "全局历史图表"), ! cards.empty());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
                        [this] (int result)
    {
        if (result == 1)
            addChannel();
        else if (result == 2)
            addGroup();
        else if (result == 3)
            showGlobalHistory();
    });
}

void MeterBridgeMainComponent::showCardMenu (wjn::common::ChannelCard& card,
                                              juce::Point<int> screenPosition)
{
    juce::PopupMenu menu;
    menu.addItem (1, locale.text ("meterbridge.menu.addChannel", "添加通道"));
    menu.addItem (2, locale.text ("meterbridge.menu.deleteChannel", "删除通道"), cards.size() > 1);
    menu.addSeparator();
    menu.addItem (3, locale.text ("meterbridge.menu.addGroup", "添加分组"));

    const auto groupId = groupIdForName (card.getGroupName());
    const auto canDeleteGroup = groupId != "ungrouped";
    menu.addItem (4, locale.text ("meterbridge.menu.deleteGroup", "删除分组"), canDeleteGroup);
    menu.addItem (5, locale.text ("meterbridge.menu.historyAnalysis", "历史分析"));

    juce::PopupMenu cardColours;
    const std::array<juce::String, 6> colourNames {
        locale.text ("meterbridge.color.blue", "蓝"),
        locale.text ("meterbridge.color.green", "绿"),
        locale.text ("meterbridge.color.amber", "琥珀"),
        locale.text ("meterbridge.color.red", "红"),
        locale.text ("meterbridge.color.violet", "紫"),
        locale.text ("meterbridge.color.cyan", "青")
    };
    for (int index = 0; index < static_cast<int> (colourNames.size()); ++index)
        cardColours.addItem (100 + index, colourNames[static_cast<size_t> (index)], true,
                             card.getCardColour() == paletteColour (index));
    menu.addSubMenu (locale.text ("meterbridge.menu.channelColor", "通道颜色"), cardColours);

    juce::Component::SafePointer<wjn::common::ChannelCard> safeCard (&card);
    const auto cardIndex = indexOfCard (&card);
    const auto channelId = juce::isPositiveAndBelow (cardIndex, channelModel.size())
        ? project.channels[static_cast<size_t> (cardIndex)].id : juce::String();
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
                        [this, safeCard, groupId, channelId] (int result)
    {
        if (result == 1)
        {
            addChannel();
            return;
        }
        if (result == 2)
        {
            if (safeCard != nullptr)
                removeChannel (indexOfCard (safeCard));
            return;
        }
        if (result == 3)
        {
            addGroup();
            return;
        }
        if (result == 4)
        {
            removeGroup (groupId);
            return;
        }
        if (result == 5 && safeCard != nullptr)
        {
            showHistory (indexOfCard (safeCard));
            return;
        }
        if (result >= 100 && result < 106 && safeCard != nullptr)
        {
            const auto colour = paletteColour (result - 100);
            safeCard->setCardColour (colour);
            channelColours[channelId.toStdString()] = colour;
        }
    });
}

void MeterBridgeMainComponent::mouseDown (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
        showEmptyAreaMenu (event.getScreenPosition());
}

void MeterBridgeMainComponent::consumeAudio()
{
    const auto status = audioBridge.getStatus();
    if (! status.connected || status.sampleRate <= 0)
    {
        updateStatus();
        return;
    }

    for (size_t index = 0; index < cards.size(); ++index)
    {
        auto& engine = meterEngines[index];
        engine.setSampleRate (status.sampleRate);
        MeterAudioBridge::AudioBlock block;
           bool received = false;
           constexpr int maximumBlocksPerRefresh = 32;
           for (int blocksProcessed = 0;
               blocksProcessed < maximumBlocksPerRefresh
               && audioBridge.pop (static_cast<int> (index), block);
               ++blocksProcessed)
        {
            engine.process (block.samples.data(), block.frameCount);
            if (silenceDetectors[index].processBlock (engine.getValues().peakDbfs,
                                                       block.frameCount, status.sampleRate))
            {
                resetChannel (static_cast<int> (index), false);
                continue;
            }
            received = true;
        }

        if (! received)
            continue;

        latestValues[index] = engine.getValues();
        cards[index]->setMeterValues (latestValues[index]);
        const auto nowMs = juce::Time::currentTimeMillis();
        if (nowMs - lastHistoryTimesMs[index] >= 100)
        {
            histories[index].push_back ({ juce::Time::getCurrentTime(), {
                latestValues[index].peakDbfs, latestValues[index].rmsDbfs,
                latestValues[index].truePeakDbtp, latestValues[index].momentaryLufs,
                latestValues[index].shortTermLufs, latestValues[index].integratedLufs,
                latestValues[index].lraLu } });
            lastHistoryTimesMs[index] = nowMs;
            const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime (project.historyWindowSeconds);
            while (! histories[index].empty() && histories[index].front().timestamp < cutoff)
                histories[index].pop_front();
        }
        writeCsvRecord (static_cast<int> (index), latestValues[index]);
    }
    updateStatus();
}

void MeterBridgeMainComponent::updateStatus()
{
    const auto status = audioBridge.getStatus();
    const auto dropped = csvLogWriter.getDroppedWriteCount();
    const auto failed = csvLogWriter.getFailedWriteCount();
    juce::String firstLine;
    juce::String secondLine;
    if (! status.connected || ! status.running || status.sampleRate <= 0)
    {
        firstLine = locale.text ("meterbridge.status.offline", "JACK 离线");
        secondLine = audioBridge.getLastError();
    }
    else
    {
        firstLine = juce::String (status.sampleRate) + " Hz  "
                  + juce::String (status.blockSize) + " FRM";
        secondLine = openGlAcceleration.isEnabled()
            ? locale.text ("meterbridge.status.opengl", "OpenGL 加速")
            : locale.text ("meterbridge.status.software", "软件渲染");
        secondLine << "  " << (status.running
            ? locale.text ("meterbridge.status.running", "运行")
            : locale.text ("meterbridge.status.stopped", "已停止"));
        if (status.xruns > 0)
            secondLine << "  " << locale.text ("meterbridge.status.xruns", "XRUN")
                       << " " << juce::String (status.xruns);
    }
    if (dropped > 0)
        secondLine << "  " << locale.text ("meterbridge.status.logDrop", "日志丢弃") << " " << dropped;
    if (failed > 0)
        secondLine << "  " << locale.text ("meterbridge.status.logError", "日志错误") << " " << failed;

    if (statusLineOne == firstLine && statusLineTwo == secondLine)
        return;
    statusLineOne = std::move (firstLine);
    statusLineTwo = std::move (secondLine);
    statusLcd.setPowered (true);
    statusLcd.repaint();
}

void MeterBridgeMainComponent::resetChannel (int index, bool resetSilenceDetector)
{
    if (! juce::isPositiveAndBelow (index, static_cast<int> (cards.size())))
        return;
    meterEngines[static_cast<size_t> (index)].reset();
    if (resetSilenceDetector)
        silenceDetectors[static_cast<size_t> (index)].reset();
    latestValues[static_cast<size_t> (index)] = meterEngines[static_cast<size_t> (index)].getValues();
    histories[static_cast<size_t> (index)].clear();
    currentLogFiles[static_cast<size_t> (index)] = {};
    lastLogTimesMs[static_cast<size_t> (index)] = 0;
    lastHistoryTimesMs[static_cast<size_t> (index)] = 0;
    cards[static_cast<size_t> (index)]->resetMeterValues (latestValues[static_cast<size_t> (index)]);
}

void MeterBridgeMainComponent::writeCsvRecord (int index, const wjn::common::MeterValues& values)
{
    if (! juce::isPositiveAndBelow (index, static_cast<int> (cards.size()))
        || ! cards[static_cast<size_t> (index)]->isRecording())
        return;

    const auto now = juce::Time::currentTimeMillis();
    if (now - lastLogTimesMs[static_cast<size_t> (index)]
        < static_cast<juce::int64> (project.logSaveIntervalSeconds * 1000.0f))
        return;

    auto folderName = project.channels[static_cast<size_t> (index)].name
        .replaceCharacters ("\\/:*?\"<>|", "_________").trim();
    if (folderName.isEmpty())
        folderName = "Channel " + juce::String (index + 1);
    const auto directory = juce::File (project.logRootDirectory).getChildFile (folderName);
    if (! directory.createDirectory())
        return;

    auto& file = currentLogFiles[static_cast<size_t> (index)];
    if (file == juce::File())
    {
        const auto timestamp = juce::Time::getCurrentTime();
        file = directory.getChildFile (timestamp.formatted ("%Y-%m-%d_%H-%M-%S") + "-"
                                       + juce::String (timestamp.getMilliseconds()).paddedLeft ('0', 3) + ".csv");
    }

    const auto line = juce::Time::getCurrentTime().toISO8601 (true) + ","
                    + juce::String (values.peakDbfs, 3) + ","
                    + juce::String (values.rmsDbfs, 3) + ","
                    + juce::String (values.truePeakDbtp, 3) + ","
                    + juce::String (values.momentaryLufs, 3) + ","
                    + juce::String (values.shortTermLufs, 3) + ","
                    + juce::String (values.integratedLufs, 3) + ","
                    + juce::String (values.lraLu, 3) + "\r\n";
    if (csvLogWriter.enqueue (file, line))
        lastLogTimesMs[static_cast<size_t> (index)] = now;
}

void MeterBridgeMainComponent::showHistory (int index)
{
    if (! juce::isPositiveAndBelow (index, static_cast<int> (cards.size())))
        return;
    juce::Component::SafePointer<MeterBridgeMainComponent> safeThis (this);
    const auto presetDefinitions = presetLibrary.getPresets();
    auto* window = new HistoryWindow (project.channels[static_cast<size_t> (index)].name + " "
                                          + locale.text ("meterbridge.history.title", "历史"),
        locale,
        [safeThis, index]
        {
            return safeThis != nullptr && juce::isPositiveAndBelow (index, static_cast<int> (safeThis->histories.size()))
                ? std::vector<wjn::common::HistorySample> (
                    safeThis->histories[static_cast<size_t> (index)].begin(),
                    safeThis->histories[static_cast<size_t> (index)].end())
                : std::vector<wjn::common::HistorySample>();
        },
        [safeThis, index]
        {
            return safeThis != nullptr && juce::isPositiveAndBelow (index, safeThis->channelModel.size())
                ? safeThis->presetLibrary.get (safeThis->project.channels[static_cast<size_t> (index)].presetId)
                : wjn::common::LoudnessPreset();
        },
        [safeThis, index]
        {
            if (safeThis == nullptr || ! juce::isPositiveAndBelow (index, safeThis->channelModel.size()))
                return 1;
            const auto& presetId = safeThis->project.channels[static_cast<size_t> (index)].presetId;
            for (size_t presetIndex = 0; presetIndex < safeThis->presetLibrary.getPresets().size(); ++presetIndex)
                if (safeThis->presetLibrary.getPresets()[presetIndex].id == presetId)
                    return static_cast<int> (presetIndex) + 1;
            return static_cast<int> (safeThis->presetLibrary.getPresets().size()) + 1;
        },
        [safeThis, index] (int menuId)
        {
            if (safeThis == nullptr || ! juce::isPositiveAndBelow (index, safeThis->channelModel.size()))
                return;
            auto presetId = safeThis->presetIdForMenuId (menuId);
            if (presetId.isEmpty())
                presetId = safeThis->project.defaultPresetId;
            safeThis->channelModel.setPresetId (index, presetId);
            const auto preset = safeThis->presetLibrary.get (presetId);
            safeThis->cards[static_cast<size_t> (index)]->setPreset (preset.targetLufs,
                                                                       preset.toleranceLu,
                                                                       preset.truePeakMaxDbtp);
            safeThis->cards[static_cast<size_t> (index)]->setPresetOptions (
                safeThis->presetNames(), menuId);
            safeThis->saveProject (safeThis->defaultProjectFile());
        },
        project.historyWindowSeconds, project.visibleMetrics, presetDefinitions);
    window->setVisible (true);
}

std::vector<int> MeterBridgeMainComponent::globalHistoryChannelIndices (int targetIndex) const
{
    std::vector<int> channelIndices;
    const auto channelCount = static_cast<int> (project.channels.size());
    if (juce::isPositiveAndBelow (targetIndex, channelCount))
    {
        channelIndices.push_back (targetIndex);
    }
    else
    {
        std::vector<juce::String> groupIds;
        for (const auto& group : project.groups)
            if (group.id != "ungrouped"
                && std::any_of (project.channels.begin(), project.channels.end(), [&group] (const auto& channel)
                                { return channel.groupId == group.id; }))
                groupIds.push_back (group.id);

        if (targetIndex >= channelCount
            && targetIndex < channelCount + static_cast<int> (groupIds.size()))
        {
            const auto& groupId = groupIds[static_cast<size_t> (targetIndex - channelCount)];
            for (int index = 0; index < channelCount; ++index)
                if (project.channels[static_cast<size_t> (index)].groupId == groupId)
                    channelIndices.push_back (index);
        }
        else if (targetIndex == channelCount + static_cast<int> (groupIds.size()))
        {
            for (int index = 0; index < channelCount; ++index)
                channelIndices.push_back (index);
        }
    }
    return channelIndices;
}

std::vector<wjn::common::HistorySample> MeterBridgeMainComponent::globalHistoryForTarget (int targetIndex) const
{
    const auto channelIndices = globalHistoryChannelIndices (targetIndex);

    if (channelIndices.empty())
        return {};
    if (channelIndices.size() == 1)
    {
        const auto& history = histories[static_cast<size_t> (channelIndices.front())];
        return { history.begin(), history.end() };
    }

    size_t sampleCount = std::numeric_limits<size_t>::max();
    for (const auto index : channelIndices)
        sampleCount = juce::jmin (sampleCount, histories[static_cast<size_t> (index)].size());
    if (sampleCount == 0 || sampleCount == std::numeric_limits<size_t>::max())
        return {};

    std::vector<wjn::common::HistorySample> result;
    result.reserve (sampleCount);
    for (size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        auto sample = histories[static_cast<size_t> (channelIndices.front())][sampleIndex];
        for (size_t metric = 0; metric < sample.values.size(); ++metric)
        {
            std::vector<float> values;
            values.reserve (channelIndices.size());
            for (const auto index : channelIndices)
                values.push_back (histories[static_cast<size_t> (index)][sampleIndex].values[metric]);
            const auto mean = std::accumulate (values.begin(), values.end(), 0.0f)
                             / static_cast<float> (values.size());
            std::sort (values.begin(), values.end());
            const auto middle = values.size() / 2;
            const auto median = values.size() % 2 == 0
                ? (values[middle - 1] + values[middle]) * 0.5f : values[middle];
            sample.values[metric] = (mean + median) * 0.5f;
        }
        result.push_back (std::move (sample));
    }
    return result;
}

juce::StringArray MeterBridgeMainComponent::globalHistoryTargets() const
{
    juce::StringArray targets;
    for (const auto& channel : project.channels)
        targets.add (locale.text ("meterbridge.history.track", "轨道") + ": " + channel.name);
    for (const auto& group : project.groups)
    {
        if (group.id != "ungrouped"
            && std::any_of (project.channels.begin(), project.channels.end(), [&group] (const auto& channel)
                            { return channel.groupId == group.id; }))
            targets.add (locale.text ("meterbridge.history.group", "分组") + ": " + group.name);
    }
    targets.add (locale.text ("meterbridge.history.all", "全部"));
    return targets;
}

wjn::common::LoudnessPreset MeterBridgeMainComponent::globalPresetForTarget (int targetIndex) const
{
    const auto channelIndices = globalHistoryChannelIndices (targetIndex);
    return ! channelIndices.empty()
        ? presetLibrary.get (project.channels[static_cast<size_t> (channelIndices.front())].presetId)
        : wjn::common::LoudnessPreset();
}

int MeterBridgeMainComponent::globalPresetMenuIdForTarget (int targetIndex) const
{
    const auto channelIndices = globalHistoryChannelIndices (targetIndex);
    const auto selectedPresetId = channelIndices.empty()
        ? juce::String() : project.channels[static_cast<size_t> (channelIndices.front())].presetId;
    for (size_t index = 0; index < presetLibrary.getPresets().size(); ++index)
        if (presetLibrary.getPresets()[index].id == selectedPresetId)
            return static_cast<int> (index) + 1;
    return static_cast<int> (presetLibrary.getPresets().size()) + 1;
}

void MeterBridgeMainComponent::setGlobalPresetForTarget (int targetIndex, int menuId)
{
    const auto presetId = presetIdForMenuId (menuId).isNotEmpty()
        ? presetIdForMenuId (menuId) : project.defaultPresetId;
    const auto channelIndices = globalHistoryChannelIndices (targetIndex);

    for (const auto index : channelIndices)
    {
        channelModel.setPresetId (index, presetId);
        const auto preset = presetLibrary.get (presetId);
        cards[static_cast<size_t> (index)]->setPreset (preset.targetLufs, preset.toleranceLu,
                                                        preset.truePeakMaxDbtp);
        cards[static_cast<size_t> (index)]->setPresetOptions (presetNames(), menuId);
    }
    if (! channelIndices.empty())
        saveProject (defaultProjectFile());
}

void MeterBridgeMainComponent::showGlobalHistory()
{
    auto selection = std::make_shared<int> (0);
    juce::Component::SafePointer<MeterBridgeMainComponent> safeThis (this);
    auto* window = new HistoryWindow (locale.text ("meterbridge.toolbar.globalHistory", "全局历史"),
        locale,
        [safeThis, selection]
        {
            return safeThis != nullptr ? safeThis->globalHistoryForTarget (*selection)
                                       : std::vector<wjn::common::HistorySample>();
        },
        [safeThis, selection]
        {
            return safeThis != nullptr ? safeThis->globalPresetForTarget (*selection)
                                       : wjn::common::LoudnessPreset();
        },
        [safeThis, selection]
        {
            return safeThis != nullptr ? safeThis->globalPresetMenuIdForTarget (*selection) : 1;
        },
        [safeThis, selection] (int menuId)
        {
            if (safeThis != nullptr)
                safeThis->setGlobalPresetForTarget (*selection, menuId);
        },
        project.historyWindowSeconds, project.visibleMetrics, presetLibrary.getPresets(),
        globalHistoryTargets(),
        [selection] (int targetIndex) { *selection = targetIndex; });
    window->setVisible (true);
}

void MeterBridgeMainComponent::showSettings()
{
    SettingsDialogLauncher::show (*this, project, locale, presetLibrary.getPresets(),
                                  [this] (MeterProject updated) { applyProjectSettings (std::move (updated)); });
}

void MeterBridgeMainComponent::showSaveDialog()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        locale.text ("meterbridge.dialog.saveProject", "保存 MeterBridge 配置"),
                                                       defaultProjectFile(), "*.meter");
    juce::Component::SafePointer<MeterBridgeMainComponent> safeThis (this);
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [safeThis] (const juce::FileChooser& chooser)
    {
        if (safeThis != nullptr)
            safeThis->saveProject (chooser.getResult());
    });
}

void MeterBridgeMainComponent::showLoadDialog()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        locale.text ("meterbridge.dialog.loadProject", "加载 MeterBridge 配置"),
                                                       defaultProjectFile(), "*.meter");
    juce::Component::SafePointer<MeterBridgeMainComponent> safeThis (this);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [safeThis] (const juce::FileChooser& chooser)
    {
        if (safeThis != nullptr && safeThis->loadProject (chooser.getResult()))
        {
            safeThis->openGlAcceleration.setEnabled (safeThis->project.openGlAccelerationEnabled);
            safeThis->rebuildCards();
            safeThis->connectJack();
        }
    });
}

void MeterBridgeMainComponent::showSaveProjectPresetDialog()
{
    auto* alert = new juce::AlertWindow (locale.text ("meterbridge.dialog.savePreset", "保存工程预设"),
                                         locale.text ("meterbridge.dialog.savePresetMessage",
                                                      "将当前完整工程保存到 meter_saves 目录"),
                                         juce::AlertWindow::NoIcon);
    alert->addTextEditor ("name", locale.text ("meterbridge.dialog.defaultProjectPresetName",
                                                "My meter preset"),
                          locale.text ("meterbridge.dialog.name", "名称"));
    alert->addButton (locale.text ("meterbridge.dialog.save", "保存"), 1);
    alert->addButton (locale.text ("meterbridge.dialog.cancel", "取消"), 0);
    alert->enterModalState (true, juce::ModalCallbackFunction::create ([this, alert] (int result)
    {
        if (result != 1)
            return;

        const auto name = alert->getTextEditorContents ("name").trim();
        const auto file = projectPresetFileForName (name);
        if (file == juce::File())
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::AlertWindow::WarningIcon,
                locale.text ("meterbridge.title", "MeterBridge"),
                locale.text ("meterbridge.dialog.emptyPresetName", "工程预设名称不能为空"),
                locale.text ("meterbridge.dialog.confirm", "确定"));
            return;
        }
        saveProject (file);
    }), true);
}

void MeterBridgeMainComponent::showCustomPresetDialog()
{
    auto* alert = new juce::AlertWindow (
        locale.text ("meterbridge.dialog.createLoudnessPreset", "新建响度预设"),
        locale.text ("meterbridge.dialog.createLoudnessPresetMessage", "保存为独立 .loudness 文件"),
                                         juce::AlertWindow::NoIcon);
    alert->addTextEditor ("name", locale.text ("meterbridge.dialog.defaultLoudnessPresetName", "My preset"),
                          locale.text ("meterbridge.dialog.name", "名称"));
    alert->addTextEditor ("target", "-14", locale.text ("meterbridge.dialog.targetLufs", "目标 LUFS"));
    alert->addTextEditor ("peak", "-1", locale.text ("meterbridge.dialog.truePeak", "真峰值 dBTP"));
    alert->addTextEditor ("tolerance", "1", locale.text ("meterbridge.dialog.tolerance", "容差 LU"));
    alert->addButton (locale.text ("meterbridge.dialog.create", "创建"), 1);
    alert->addButton (locale.text ("meterbridge.dialog.cancel", "取消"), 0);
    alert->enterModalState (true, juce::ModalCallbackFunction::create ([this, alert] (int result)
    {
        if (result == 1)
        {
            presetLibrary.saveCustom (alert->getTextEditorContents ("name"),
                                      { static_cast<float> (alert->getTextEditorContents ("target").getDoubleValue()),
                                        static_cast<float> (alert->getTextEditorContents ("peak").getDoubleValue()),
                                        static_cast<float> (alert->getTextEditorContents ("tolerance").getDoubleValue()) });
            refreshPresetLists();
        }
    }), true);
}

void MeterBridgeMainComponent::addGroup()
{
    channelModel.addGroup ("Group " + juce::String (project.groups.size()));
    rebuildCards();
}

void MeterBridgeMainComponent::refreshPresetLists()
{
    presetLibrary.refresh();
    for (size_t index = 0; index < cards.size(); ++index)
        configureCard (*cards[index], static_cast<int> (index));
    refreshProjectPresetList();
}

void MeterBridgeMainComponent::refreshProjectPresetList()
{
    const auto directory = projectPresetDirectory();
    if (! directory.exists() && ! directory.createDirectory())
    {
        projectPresetBox.clear (juce::dontSendNotification);
        projectPresetBox.setTextWhenNothingSelected (
            locale.text ("meterbridge.dialog.projectPresetDirectoryError", "工程预设目录不可用"));
        return;
    }

    const auto previousId = projectPresetBox.getSelectedId();
    projectPresetFiles.clear();
    for (const auto& file : directory.findChildFiles (juce::File::findFiles, false, "*.meter"))
        projectPresetFiles.push_back (file);
    std::sort (projectPresetFiles.begin(), projectPresetFiles.end(), [] (const auto& first, const auto& second)
    {
        return first.getFileNameWithoutExtension().compareIgnoreCase (second.getFileNameWithoutExtension()) < 0;
    });

    projectPresetBox.clear (juce::dontSendNotification);
    if (projectPresetFiles.empty())
    {
        projectPresetBox.setTextWhenNothingSelected (locale.text ("meterbridge.projectPreset.empty",
                                                                    "没有工程预设"));
        return;
    }

    for (size_t index = 0; index < projectPresetFiles.size(); ++index)
        projectPresetBox.addItem (projectPresetFiles[index].getFileNameWithoutExtension(),
                                  static_cast<int> (index) + 1);
    projectPresetBox.setSelectedId (juce::isPositiveAndBelow (previousId - 1,
                                                                static_cast<int> (projectPresetFiles.size()))
                                        ? previousId : 0,
                                    juce::dontSendNotification);
}

void MeterBridgeMainComponent::saveProject (const juce::File& file)
{
    juce::String error;
    if (! project.saveToFile (file, error) && error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon, locale.text ("meterbridge.title", "MeterBridge"),
            error, locale.text ("meterbridge.dialog.confirm", "确定"));
}

bool MeterBridgeMainComponent::loadProject (const juce::File& file)
{
    const auto globalOpenGlAccelerationEnabled = project.openGlAccelerationEnabled;
    if (! file.existsAsFile())
    {
        project.ensureDefaults();
        return false;
    }
    MeterProject loaded;
    juce::String error;
    if (! MeterProject::loadFromFile (file, loaded, error))
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon, locale.text ("meterbridge.title", "MeterBridge"),
            error, locale.text ("meterbridge.dialog.confirm", "确定"));
        project.ensureDefaults();
        return false;
    }
    if (loaded.logRootDirectory.isEmpty())
        loaded.logRootDirectory = defaultLogDirectory().getFullPathName();
    loaded.openGlAccelerationEnabled = globalOpenGlAccelerationEnabled;
    project = std::move (loaded);
    project.ensureDefaults();
    return true;
}

juce::File MeterBridgeMainComponent::defaultProjectFile() const
{
    return juce::File::getSpecialLocation (juce::File::currentApplicationFile)
        .getParentDirectory().getChildFile ("WinJACKNexus.MeterBridge.meter");
}

juce::File MeterBridgeMainComponent::projectPresetDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::currentApplicationFile)
        .getParentDirectory().getChildFile ("meter_saves");
}

juce::File MeterBridgeMainComponent::projectPresetFileForName (const juce::String& name) const
{
    if (name.isEmpty())
        return {};

    const auto fileName = name.replaceCharacters (" \\/:*?\"<>|", "__________").substring (0, 64);
    return fileName.isEmpty() ? juce::File() : projectPresetDirectory().getChildFile (fileName + ".meter");
}

juce::File MeterBridgeMainComponent::defaultGlobalConfigFile() const
{
    return juce::File::getSpecialLocation (juce::File::currentApplicationFile)
        .getParentDirectory().getChildFile ("config.json");
}

juce::File MeterBridgeMainComponent::defaultLogDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::currentApplicationFile)
        .getParentDirectory().getChildFile ("logs");
}

juce::String MeterBridgeMainComponent::presetIdForMenuId (int menuId) const
{
    if (juce::isPositiveAndBelow (menuId - 1, static_cast<int> (presetLibrary.getPresets().size())))
        return presetLibrary.getPresets()[static_cast<size_t> (menuId - 1)].id;
    return {};
}

juce::StringArray MeterBridgeMainComponent::presetNames() const
{
    juce::StringArray names;
    for (const auto& preset : presetLibrary.getPresets())
        names.add (preset.name);
    names.add (locale.text ("meterbridge.history.noPreset", "不使用预设"));
    return names;
}

juce::String MeterBridgeMainComponent::groupIdForName (const juce::String& name) const
{
    for (const auto& group : project.groups)
        if (group.name == name)
            return group.id;
    return "ungrouped";
}

juce::String MeterBridgeMainComponent::groupNameForId (const juce::String& id) const
{
    for (const auto& group : project.groups)
        if (group.id == id)
            return group.name;
    return project.groups.empty()
        ? locale.text ("meterbridge.default.ungrouped", "未分组") : project.groups.front().name;
}

void MeterBridgeMainComponent::applyProjectSettings (MeterProject newProject)
{
    project = std::move (newProject);
    project.ensureDefaults();
    openGlAcceleration.setEnabled (project.openGlAccelerationEnabled);
    saveGlobalSettings();
    rebuildCards();
    connectJack();
}

void MeterBridgeMainComponent::saveGlobalSettings()
{
    juce::String error;
    if (! project.saveGlobalToFile (defaultGlobalConfigFile(), error) && error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon, locale.text ("meterbridge.title", "MeterBridge"),
            error, locale.text ("meterbridge.dialog.confirm", "确定"));
}

bool MeterBridgeMainComponent::loadGlobalSettings()
{
    const auto file = defaultGlobalConfigFile();
    if (! file.existsAsFile())
        return false;

    juce::String error;
    if (! MeterProject::loadGlobalFromFile (file, project, error))
    {
        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (
                juce::AlertWindow::WarningIcon, locale.text ("meterbridge.title", "MeterBridge"),
                error, locale.text ("meterbridge.dialog.confirm", "确定"));
        return false;
    }
    return true;
}

void MeterBridgeMainComponent::timerCallback()
{
    openGlAcceleration.update (*this);
    const auto status = audioBridge.getStatus();
    const auto now = juce::Time::currentTimeMillis();
    if (! status.connected && now - lastJackAttemptMs >= 2000)
        connectJack();
    consumeAudio();
}

void MeterBridgeMainComponent::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto toolbar = area.removeFromTop (60);
    toolbarPanel.setBounds (toolbar);
    auto controls = toolbar.reduced (8, 6);
    settingsButton.setBounds (controls.removeFromRight (62));
    controls.removeFromRight (6);
    statusLcd.setBounds (controls.removeFromRight (232));
    controls.removeFromRight (6);
    saveButton.setBounds (controls.removeFromLeft (62));
    controls.removeFromLeft (4);
    loadButton.setBounds (controls.removeFromLeft (62));
    controls.removeFromLeft (4);
    saveProjectPresetButton.setBounds (controls.removeFromLeft (94));
    controls.removeFromLeft (4);
    projectPresetBox.setBounds (controls.removeFromLeft (170).reduced (0, 2));
    controls.removeFromLeft (4);
    refreshPresetsButton.setBounds (controls.removeFromLeft (86));
    controls.removeFromLeft (4);
    customPresetButton.setBounds (controls.removeFromLeft (82));
    controls.removeFromLeft (4);
    addChannelButton.setBounds (controls.removeFromLeft (82));
    controls.removeFromLeft (4);
    removeChannelButton.setBounds (controls.removeFromLeft (82));
    controls.removeFromLeft (4);
    addGroupButton.setBounds (controls.removeFromLeft (82));
    controls.removeFromLeft (4);
    resetAllButton.setBounds (controls.removeFromLeft (82));
    controls.removeFromLeft (4);
    globalHistoryButton.setBounds (controls.removeFromLeft (82));
    area.removeFromTop (10);
    cardsViewport.setBounds (area);
    layoutCards();
}

} // namespace wjn::meterbridge
