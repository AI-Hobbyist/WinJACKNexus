#include "MainComponent.h"

#include "CascadeDeviceSelector.h"
#include "DeviceItemCard.h"
#include <WinJACKNexus/Common/UI/Theme.h>

namespace wjn::adapter
{
namespace
{

juce::Font systemFont (float height, int style = juce::Font::plain)
{
    return juce::Font (juce::FontOptions (juce::Font::getSystemUIFontName(), height, style));
}

juce::String fromUtf8 (const char* value)
{
    return juce::String::fromUTF8 (value);
}

class DeviceListSection final : public juce::Component
{
public:
    explicit DeviceListSection (juce::String title)
        : sectionTitle (std::move (title))
    {
        addAndMakeVisible (titleLabel);
        titleLabel.setText (sectionTitle, juce::dontSendNotification);
        titleLabel.setFont (systemFont (15.0f, juce::Font::bold));
        titleLabel.setColour (juce::Label::textColourId, wjn::common::theme::primaryText);

        addAndMakeVisible (addButton);
        addButton.setButtonText (fromUtf8 ("添加设备"));
        addButton.onClick = [this]
        {
            CascadeDeviceSelector::show (*this, [this] (CascadeDeviceSelector::Selection selection)
            {
                addDevice (std::move (selection));
            });
        };

        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&listContent, false);
        viewport.setScrollBarsShown (true, false);
        listContent.setOpaque (false);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (wjn::common::theme::rackPanel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        g.setColour (wjn::common::theme::border);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        auto header = area.removeFromTop (32);
        titleLabel.setBounds (header.removeFromLeft (header.getWidth() - 110));
        addButton.setBounds (header.removeFromRight (104));
        area.removeFromTop (8);
        viewport.setBounds (area);
        layoutCards();
    }

private:
    void addDevice (CascadeDeviceSelector::Selection selection)
    {
        DeviceItemCard::Data data;
        data.clientName = makeClientName (selection.streamType);
        data.driver = selection.driver;
        data.streamType = selection.streamType;
        data.device = selection.device;
        data.channels = selection.channels;

        auto* card = new DeviceItemCard (
            std::move (data),
            [] (DeviceItemCard&, juce::String) {},
            [] (DeviceItemCard&) {},
            [this] (DeviceItemCard& card)
            {
                cards.removeObject (&card, true);
                layoutCards();
            });

        cards.add (card);
        listContent.addAndMakeVisible (card);
        layoutCards();
    }

    juce::String makeClientName (const juce::String& streamType) const
    {
        const auto isInput = streamType == "Record";
        const auto prefix = isInput ? "WDM_AudioIn_" : "WDM_AudioOut_";
        return prefix + juce::String (cards.size() + 1).paddedLeft ('0', 2);
    }

    void layoutCards()
    {
        constexpr int cardHeight = 58;
        auto width = juce::jmax (0, viewport.getWidth() - viewport.getScrollBarThickness());
        for (int index = 0; index < cards.size(); ++index)
            cards[index]->setBounds (0, index * (cardHeight + 8), width, cardHeight);

        listContent.setSize (width, juce::jmax (viewport.getHeight(), cards.size() * (cardHeight + 8)));
        viewport.setViewedComponent (&listContent, false);
    }

    juce::String sectionTitle;
    juce::Label titleLabel;
    juce::TextButton addButton;
    juce::Viewport viewport;
    juce::Component listContent;
    juce::OwnedArray<DeviceItemCard> cards;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeviceListSection)
};

class TabPage final : public juce::Component
{
public:
    TabPage (juce::String inputTitle, juce::String outputTitle)
        : inputSection (std::move (inputTitle)), outputSection (std::move (outputTitle))
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
                              fromUtf8 ("OUT  |  WinMM / WinRT MIDI")),
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
