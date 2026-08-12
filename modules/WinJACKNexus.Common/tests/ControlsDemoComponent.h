#pragma once

#include <WinJACKNexus/Common/UI/AudioLed.h>
#include <WinJACKNexus/Common/UI/ChannelCard.h>
#include <WinJACKNexus/Common/UI/EqCurveEditor.h>
#include <WinJACKNexus/Common/UI/LcdDisplayControl.h>
#include <WinJACKNexus/Common/UI/MeterComponent.h>
#include <WinJACKNexus/Common/UI/MeterHistoryChart.h>
#include <WinJACKNexus/Common/UI/MidiLed.h>
#include <WinJACKNexus/Common/UI/MixerChannelStripComponent.h>
#include <WinJACKNexus/Common/UI/OnOffSwitch.h>
#include <WinJACKNexus/Common/UI/SettingsSlider.h>
#include <WinJACKNexus/Common/UI/SpatialPannerComponent.h>
#include <WinJACKNexus/Common/UI/RotaryControl.h>
#include <WinJACKNexus/Common/UI/RouteSelectorControl.h>
#include <WinJACKNexus/Common/UI/SegmentedMeterControl.h>
#include <WinJACKNexus/Common/UI/ThemeContext.h>
#include <WinJACKNexus/Common/UI/TransferCurveEditor.h>
#include <WinJACKNexus/Common/UI/ToggleBadgeControl.h>
#include <WinJACKNexus/Common/UI/VerticalFaderControl.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace wjn::common
{

class ControlsDemoComponent final : public juce::Component,
                                    private juce::Button::Listener,
                                    private juce::Timer
{
public:
    ControlsDemoComponent();

    void setTheme(const ThemeContext& newTheme);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void setSectionTitle(juce::Label& label,
                         const juce::String& title,
                         const juce::String& functions);
    void buttonClicked(juce::Button*) override;
    void timerCallback() override;
    void layoutSection(juce::Component& section,
                       juce::Label& title,
                       int x,
                       int y,
                       int width,
                       int height);

    ThemeContext theme;
    juce::Label header;
    juce::Label subtitle;

    juce::GroupComponent ledSection { juce::String::fromUTF8("LED 控件") };
    juce::Label ledTitle;
    AudioLed audioLed;
    MidiLed midiLed;
    juce::TextButton audioDownButton { "AudioLed.setLevel(-)" };
    juce::TextButton audioUpButton { "AudioLed.setLevel(+)" };
    juce::TextButton midiTriggerButton { "MidiLed.trigger()" };

    juce::GroupComponent meterSection { "MeterComponent" };
    juce::Label meterTitle;
    MeterComponent peakMeter { "PEAK", MeterComponent::MeterType::decibels };
    MeterComponent truePeakMeter { "dBTP", MeterComponent::MeterType::truePeak };
    MeterComponent loudnessMeter { "LUFS", MeterComponent::MeterType::loudness, -60.0f, 0.0f };
    MeterComponent rangeMeter { "LRA", MeterComponent::MeterType::range, 0.0f, 72.0f };
    SettingsSlider meterValueSlider;
    LcdDisplayControl stereoLcd;
    LcdDisplayControl surroundLcd;
    LcdDisplayControl midiLcd;

    juce::GroupComponent interactiveSection { juce::String::fromUTF8("设置页控件") };
    juce::Label interactiveTitle;
    OnOffSwitch onOffSwitch;
    SettingsSlider settingsSlider;
    juce::Label switchValue;
    juce::Label sliderValue;

    juce::GroupComponent compositeSection { juce::String::fromUTF8("复合控件") };
    juce::Label compositeTitle;
    SpatialPannerComponent spatialPanner { true };
    MixerChannelStripComponent mixerStrip { "Mixer Channel" };
    ChannelCard channelCard { 1 };

    juce::GroupComponent pureMixerSection { juce::String::fromUTF8("PureMixer 风格控件") };
    juce::Label pureMixerTitle;
    RotaryControl gainControl { "GAIN", " dB" };
    RotaryControl compControl { "COMP", "" };
    RotaryControl gateControl { "GATE", "" };
    RotaryControl auxControl { "AUX 1", "" };
    VerticalFaderControl verticalFader;
    SegmentedMeterControl inputMeter { "INPUT" };
    SegmentedMeterControl reductionMeter { "GR", juce::Colour(0xffe0bf35) };
    SegmentedMeterControl gateMeter { "GATE", juce::Colour(0xff42d96f) };
    SegmentedMeterControl outputMeter { "OUTPUT" };
    SettingsSlider pureMixerValueSlider;
    RouteSelectorControl routeSelector { "IN", "JACK mic_1" };
    ToggleBadgeControl muteButton { "MUTE" };
    ToggleBadgeControl soloButton { "SOLO" };
    ToggleBadgeControl eqButton { "EQ" };
    ToggleBadgeControl dynamicsButton { "DYN" };

    juce::GroupComponent curveSection { juce::String::fromUTF8("PureMixer 曲线控件") };
    juce::Label curveTitle;
    EqCurveEditor eqCurveEditor;
    TransferCurveEditor transferCurveEditor;
    std::unique_ptr<MeterHistoryChart> meterHistoryChart;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlsDemoComponent)
};

} // namespace wjn::common