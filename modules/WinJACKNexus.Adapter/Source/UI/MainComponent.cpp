#include "MainComponent.h"

#include "CascadeDeviceSelector.h"
#include "DeviceItemCard.h"
#include "../DebugTrace.h"
#include <WinJACKNexus/Common/UI/CommonControls.h>
#include <WinJACKNexus/Common/UI/Theme.h>

namespace wjn::adapter
{
namespace
{

juce::String fromUtf8 (const char* value)
{
    return juce::String::fromUTF8 (value);
}

class DeviceListSection final : public wjn::common::NexusPanel
{
public:
    DeviceListSection (juce::String title, bool input, juce::StringArray& deviceIdentifiers,
                       bool midi = false, bool virtualAudio = false)
        : sectionTitle (std::move (title)), isInput (input), isMidi (midi), isVirtual (virtualAudio),
          addedDeviceIdentifiers (deviceIdentifiers)
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
                CascadeDeviceSelector::show (*this, addedDeviceIdentifiers, std::move (addSelection));
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
        addedDeviceIdentifiers.add (deviceIdentifier);
        debug::trace ("addDevice data ready client=" + data.clientName);

        debug::trace ("addDevice before card new");
        auto* card = new DeviceItemCard (
            std::move (data),
            [] (DeviceItemCard&, juce::String) {},
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
        debug::trace ("addDevice after card new card=" + debug::pointerText (card));

        cards.add (card);
        debug::trace ("addDevice after cards.add count=" + juce::String (cards.size()));
        listContent.addAndMakeVisible (card);
        debug::trace ("addDevice after listContent.addAndMakeVisible");
        layoutCards();
        debug::trace ("addDevice complete");
    }

    juce::String makeClientName (const juce::String& streamType, bool midi) const
    {
        if (midi)
        {
            const auto prefix = streamType == "Input" ? "WDM_MidiIn_" : "WDM_MidiOut_";
            return prefix + juce::String (cards.size() + 1).paddedLeft ('0', 2);
        }

        const auto isVirtualInput = streamType == "Loopback";
        const auto isPhysicalInput = streamType == "Record";
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
    bool isMidi = false;
    bool isVirtual = false;
    wjn::common::NexusLabel titleLabel;
    wjn::common::NexusButton addButton;
    wjn::common::NexusButton refreshButton;
    wjn::common::NexusViewport viewport;
    juce::Component listContent;
    juce::OwnedArray<DeviceItemCard> cards;
    juce::StringArray& addedDeviceIdentifiers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeviceListSection)
};

class TabPage final : public juce::Component
{
public:
    TabPage (juce::String inputTitle, juce::String outputTitle,
             juce::StringArray& deviceIdentifiers, bool midi = false, bool virtualAudio = false)
                : inputSection (std::move (inputTitle), true, deviceIdentifiers, midi, virtualAudio),
                    outputSection (std::move (outputTitle), false, deviceIdentifiers, midi, virtualAudio)
    {
        addAndMakeVisible (inputSection);
        addAndMakeVisible (outputSection);
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

    tabs.addTab (fromUtf8 ("系统音频"),
                 wjn::common::theme::rackPanel,
                 new TabPage (fromUtf8 ("输入  |  WASAPI 捕获"),
                              fromUtf8 ("输出  |  WASAPI 渲染"), addedDeviceIdentifiers),
                 true);
    tabs.addTab (fromUtf8 ("系统 MIDI"),
                 wjn::common::theme::rackPanel,
                 new TabPage (fromUtf8 ("输入  |  WinMM / WinRT MIDI"),
                              fromUtf8 ("输出  |  WinMM / WinRT MIDI"),
                              addedDeviceIdentifiers,
                              true),
                 true);

    addAndMakeVisible (tabs);
    setSize (960, 640);
}

MainComponent::~MainComponent() = default;

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (wjn::common::theme::darkCanvas);
}

void MainComponent::resized()
{
    tabs.setBounds (getLocalBounds());
}

} // namespace wjn::adapter
