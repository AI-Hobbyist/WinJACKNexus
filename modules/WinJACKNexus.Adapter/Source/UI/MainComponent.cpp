#include "MainComponent.h"

#include <WinJACKNexus/Common/UI/Theme.h>

namespace wjn::adapter
{
namespace
{

class IoSection final : public juce::Component
{
public:
    explicit IoSection (juce::String title)
        : sectionTitle (std::move (title))
    {
        addAndMakeVisible (titleLabel);
        titleLabel.setText (sectionTitle, juce::dontSendNotification);
        titleLabel.setFont (juce::Font (juce::FontOptions().withPointHeight (15.0f)).boldened());
        titleLabel.setColour (juce::Label::textColourId, wjn::common::theme::primaryText);
        titleLabel.setJustificationType (juce::Justification::centredLeft);
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
        titleLabel.setBounds (16, 12, getWidth() - 32, 26);
    }

private:
    juce::String sectionTitle;
    juce::Label titleLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IoSection)
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
    IoSection inputSection;
    IoSection outputSection;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TabPage)
};

} // namespace

MainComponent::MainComponent()
{
    tabs.setTabBarDepth (42);
    tabs.setOutline (0);

    tabs.addTab ("Physical Audio",
                 wjn::common::theme::rackPanel,
                 new TabPage ("IN  |  WASAPI Capture", "OUT  |  WASAPI Render"),
                 true);
    tabs.addTab ("Virtual / Playback",
                 wjn::common::theme::rackPanel,
                 new TabPage ("IN  |  WASAPI Loopback", "OUT  |  Virtual Injector"),
                 true);
    tabs.addTab ("System MIDI",
                 wjn::common::theme::rackPanel,
                 new TabPage ("IN  |  WinMM / WinRT MIDI", "OUT  |  WinMM / WinRT MIDI"),
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
