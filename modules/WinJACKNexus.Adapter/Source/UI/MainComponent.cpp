#include "MainComponent.h"

#include "CascadeDeviceSelector.h"
#include "DeviceItemCard.h"
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
    DeviceListSection (juce::String title, bool midi = false)
        : sectionTitle (std::move (title)), isMidi (midi)
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
                CascadeDeviceSelector::showMidi (*this, sectionTitle.startsWith ("IN"), std::move (addSelection));
            else
                CascadeDeviceSelector::show (*this, std::move (addSelection));
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
        DeviceItemCard::Data data;
        data.clientName = makeClientName (selection.streamType, selection.midi);
        data.driver = selection.driver;
        data.streamType = selection.streamType;
        data.device = selection.device;
        data.channels = selection.channels;
        data.midi = selection.midi;

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

                    section->cards.removeObject (cardToRemove, true);
                    section->layoutCards();
                });
            });

        cards.add (card);
        listContent.addAndMakeVisible (card);
        layoutCards();
    }

    juce::String makeClientName (const juce::String& streamType, bool midi) const
    {
        if (midi)
        {
            const auto prefix = streamType == "Input" ? "WDM_MidiIn_" : "WDM_MidiOut_";
            return prefix + juce::String (cards.size() + 1).paddedLeft ('0', 2);
        }

        const auto isInput = streamType == "Record";
        const auto prefix = isInput ? "WDM_AudioIn_" : "WDM_AudioOut_";
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
    bool isMidi = false;
    wjn::common::NexusLabel titleLabel;
    wjn::common::NexusButton addButton;
    wjn::common::NexusButton refreshButton;
    wjn::common::NexusViewport viewport;
    juce::Component listContent;
    juce::OwnedArray<DeviceItemCard> cards;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeviceListSection)
};

class TabPage final : public juce::Component
{
public:
    TabPage (juce::String inputTitle, juce::String outputTitle, bool midi = false)
        : inputSection (std::move (inputTitle), midi), outputSection (std::move (outputTitle), midi)
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

    tabs.addTab ("Physical Audio",
                 wjn::common::theme::rackPanel,
                 new TabPage (fromUtf8 ("IN  |  WASAPI Capture"),
                              fromUtf8 ("OUT  |  WASAPI Render")),
                 true);
    tabs.addTab ("Virtual / Playback",
                 wjn::common::theme::rackPanel,
                 new TabPage (fromUtf8 ("IN  |  WASAPI Loopback"),
                              fromUtf8 ("OUT  |  Virtual Injector")),
                 true);
    tabs.addTab ("System MIDI",
                 wjn::common::theme::rackPanel,
                 new TabPage (fromUtf8 ("IN  |  WinMM / WinRT MIDI"),
                              fromUtf8 ("OUT  |  WinMM / WinRT MIDI"),
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
