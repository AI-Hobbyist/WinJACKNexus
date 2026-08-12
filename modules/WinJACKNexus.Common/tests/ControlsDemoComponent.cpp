#include "ControlsDemoComponent.h"

#include <cmath>

namespace wjn::common
{

namespace
{
juce::String utf8(const char* text)
{
    return juce::String::fromUTF8(text);
}

juce::Font systemFont(float height)
{
    return juce::Font(juce::FontOptions()
                          .withName(juce::Font::getSystemUIFontName())
                          .withPointHeight(height));
}

void configureLabel(juce::Label& label, const juce::String& text, juce::Colour colour)
{
    label.setText(text, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, colour);
    label.setFont(systemFont(13.0f));
    label.setJustificationType(juce::Justification::centredLeft);
}

void drawLcdMeter(juce::Graphics& graphics,
                  juce::Rectangle<float> area,
                  float level,
                  juce::Colour colour,
                  int segments = 48)
{
    const auto gap = 1.0f;
    const auto segmentWidth = (area.getWidth() - gap * static_cast<float>(segments - 1)) / static_cast<float>(segments);
    const auto lit = juce::roundToInt(juce::jlimit(0.0f, 1.0f, level) * static_cast<float>(segments));
    for (int index = 0; index < segments; ++index)
    {
        const auto bounds = juce::Rectangle<float>(area.getX() + static_cast<float>(index) * (segmentWidth + gap),
                                                    area.getY(), segmentWidth, area.getHeight());
        graphics.setColour(index < lit ? colour : colour.withAlpha(0.11f));
        graphics.fillRect(bounds);
    }
}
}

ControlsDemoComponent::ControlsDemoComponent()
{
    configureLabel(header, utf8("WinJACKNexus.Common 自绘控件演示"), theme.colour("primaryText"));
    header.setFont(systemFont(22.0f));
    addAndMakeVisible(header);

    configureLabel(subtitle,
                   utf8("每个区域标注对应公开函数；可直接点击、拖拽或使用键盘操作。"),
                   theme.colour("secondaryText"));
    addAndMakeVisible(subtitle);

    setSectionTitle(ledTitle,
                    "AudioLed / MidiLed",
                    "AudioLed: setLevel(float, bool), paint()    MidiLed: trigger(), paint()");
    addAndMakeVisible(ledSection);
    addAndMakeVisible(ledTitle);
    addAndMakeVisible(audioLed);
    addAndMakeVisible(midiLed);
    addAndMakeVisible(audioDownButton);
    addAndMakeVisible(audioUpButton);
    addAndMakeVisible(midiTriggerButton);

    setSectionTitle(meterTitle,
                    "MeterComponent",
                    "setValue(), resetValue(), setPeakHoldDuration(), setPreset(), setTheme(), paint()");
    addAndMakeVisible(meterSection);
    addAndMakeVisible(meterTitle);
    addAndMakeVisible(peakMeter);
    addAndMakeVisible(truePeakMeter);
    addAndMakeVisible(loudnessMeter);
    addAndMakeVisible(rangeMeter);
    addAndMakeVisible(meterValueSlider);
    addAndMakeVisible(stereoLcd);
    addAndMakeVisible(surroundLcd);
    addAndMakeVisible(midiLcd);

    stereoLcd.setContentPainter([](juce::Graphics& graphics, juce::Rectangle<float> area, const juce::Font& font, juce::Colour colour)
    {
        graphics.setColour(colour);
        auto headerArea = area.removeFromTop(25.0f);
        auto deviceArea = headerArea.removeFromLeft(area.getWidth() * 0.34f);
        auto formatArea = headerArea.removeFromLeft(area.getWidth() * 0.25f);
        graphics.drawRect(deviceArea, 1.0f);
        graphics.drawRect(formatArea, 1.0f);
        graphics.setFont(font.withHeight(15.0f).boldened());
        graphics.drawText("AIH-DAC PRO", deviceArea.reduced(6.0f, 1.0f), juce::Justification::centredLeft);
        graphics.setFont(font.withHeight(11.0f));
        graphics.drawText("PCM 48.0kHz", formatArea.reduced(6.0f, 1.0f), juce::Justification::centredLeft);
        graphics.drawText("VOL -20.5dB", headerArea, juce::Justification::centredRight);
        area.removeFromTop(4.0f);
        const std::array levels { 0.78f, 0.66f };
        const std::array labels { "L", "R" };
        for (int channel = 0; channel < 2; ++channel)
        {
            auto row = area.removeFromTop(area.getHeight() / static_cast<float>(2 - channel));
            graphics.setFont(font.withHeight(12.0f).boldened());
            graphics.drawText(labels[static_cast<size_t>(channel)], row.removeFromLeft(18.0f), juce::Justification::centredLeft);
            auto meter = row.removeFromTop(13.0f).reduced(1.0f, 1.0f);
            drawLcdMeter(graphics, meter, levels[static_cast<size_t>(channel)], colour, 48);
            graphics.setFont(font.withHeight(7.5f));
            graphics.drawText("-60          -40          -20   -10       -3     0     +3   dB",
                              row, juce::Justification::centred);
        }
    });
    surroundLcd.setContentPainter([](juce::Graphics& graphics, juce::Rectangle<float> area, const juce::Font& font, juce::Colour colour)
    {
        graphics.setColour(colour);
        auto headerArea = area.removeFromTop(24.0f);
        auto deviceArea = headerArea.removeFromLeft(area.getWidth() * 0.35f);
        auto formatArea = headerArea.removeFromLeft(area.getWidth() * 0.27f);
        graphics.drawRect(deviceArea, 1.0f);
        graphics.drawRect(formatArea, 1.0f);
        graphics.setFont(font.withHeight(14.0f).boldened());
        graphics.drawText("SURR-AMP 7.1", deviceArea.reduced(5.0f, 1.0f), juce::Justification::centredLeft);
        graphics.setFont(font.withHeight(10.0f));
        graphics.drawText("PCM 96.0kHz", formatArea.reduced(5.0f, 1.0f), juce::Justification::centredLeft);
        graphics.drawText("VOL -18.0dB", headerArea, juce::Justification::centredRight);
        area.removeFromTop(3.0f);
        auto statusArea = area.removeFromRight(area.getWidth() * 0.25f).reduced(2.0f);
        graphics.drawRect(statusArea, 1.0f);
        graphics.setFont(font.withHeight(8.0f));
        graphics.drawText("> INPUT: HDMI 1\n> MODE : 7.1 CH\n> DRV  : OPTIMAL",
                          statusArea.reduced(5.0f, 2.0f), juce::Justification::centredLeft);
        area.removeFromRight(5.0f);
        const std::array labels { "FL", "FR", "C", "LFE", "SL", "SR", "SBL", "SBR" };
        const std::array levels { 0.82f, 0.76f, 0.68f, 0.45f, 0.62f, 0.58f, 0.52f, 0.49f };
        for (size_t channel = 0; channel < labels.size(); ++channel)
        {
            auto column = area.removeFromLeft(area.getWidth() / static_cast<float>(labels.size() - channel));
            graphics.setFont(font.withHeight(8.0f).boldened());
            graphics.drawText(labels[channel], column.removeFromTop(12.0f), juce::Justification::centred);
            auto meterArea = column.reduced(column.getWidth() * 0.28f, 1.0f);
            const auto litHeight = meterArea.getHeight() * levels[channel];
            graphics.setColour(colour.withAlpha(0.22f));
            graphics.drawRect(meterArea, 1.0f);
            graphics.setColour(colour);
            graphics.fillRect(meterArea.removeFromBottom(litHeight));
        }
    });
    midiLcd.setContentPainter([](juce::Graphics& graphics, juce::Rectangle<float> area, const juce::Font& font, juce::Colour colour)
    {
        graphics.setColour(colour);
        auto headerArea = area.removeFromTop(25.0f);
        auto deviceArea = headerArea.removeFromLeft(area.getWidth() * 0.34f);
        auto modeArea = headerArea.removeFromLeft(area.getWidth() * 0.31f);
        graphics.drawRect(deviceArea, 1.0f);
        graphics.drawRect(modeArea, 1.0f);
        graphics.setFont(font.withHeight(14.0f).boldened());
        graphics.drawText("MIDI-CTRL 16", deviceArea.reduced(5.0f, 1.0f), juce::Justification::centredLeft);
        graphics.setFont(font.withHeight(10.0f));
        graphics.drawText("CH MODE: OMNI", modeArea.reduced(5.0f, 1.0f), juce::Justification::centredLeft);
        graphics.drawText("BANK A", headerArea, juce::Justification::centredRight);
        area.removeFromTop(3.0f);
        const std::array values { 82, 121, 77, 68, 72, 83, 55, 119, 79, 67, 116, 69, 120, 67, 80, 109 };
        for (size_t channel = 0; channel < values.size(); ++channel)
        {
            auto column = area.removeFromLeft(area.getWidth() / static_cast<float>(values.size() - channel));
            graphics.setFont(font.withHeight(7.0f));
            graphics.drawText(juce::String(static_cast<int>(channel + 1)), column.removeFromTop(10.0f), juce::Justification::centred);
            auto meterArea = column.reduced(column.getWidth() * 0.24f, 1.0f);
            graphics.fillRect(meterArea.removeFromBottom(meterArea.getHeight() * static_cast<float>(values[channel]) / 127.0f));
        }
    });

    setSectionTitle(interactiveTitle,
                    "OnOffSwitch / SettingsSlider",
                    "OnOffSwitch: setToggleState(), getToggleState(), setStateChangeCallback()    SettingsSlider: setRange(), setValue(), getValue(), setValueChangeCallback()");
    addAndMakeVisible(interactiveSection);
    addAndMakeVisible(interactiveTitle);
    addAndMakeVisible(onOffSwitch);
    addAndMakeVisible(settingsSlider);
    addAndMakeVisible(switchValue);
    addAndMakeVisible(sliderValue);

    setSectionTitle(compositeTitle,
                    "SpatialPanner / MixerChannelStrip / ChannelCard",
                    "SpatialPanner: setPosition(), getPosition(), setIntensityGraphVisible()    MixerChannelStrip: setGain(), setPan(), setMeter()    ChannelCard: setChannelName(), setPreset(), setOnReset(), setOnRecord()");
    addAndMakeVisible(compositeSection);
    addAndMakeVisible(compositeTitle);
    addAndMakeVisible(spatialPanner);
    addAndMakeVisible(mixerStrip);
    addAndMakeVisible(channelCard);

    setSectionTitle(pureMixerTitle,
                    "RotaryControl / VerticalFaderControl / SegmentedMeterControl",
                    "RotaryControl: setRange(), setValue(), setAccent(), setValueChangeCallback()    VerticalFaderControl: setValue(), setValueChangeCallback()    SegmentedMeterControl: setLevel(), setHold()");
    addAndMakeVisible(pureMixerSection);
    addAndMakeVisible(pureMixerTitle);
    addAndMakeVisible(gainControl);
    addAndMakeVisible(compControl);
    addAndMakeVisible(gateControl);
    addAndMakeVisible(auxControl);
    addAndMakeVisible(verticalFader);
    addAndMakeVisible(inputMeter);
    addAndMakeVisible(reductionMeter);
    addAndMakeVisible(gateMeter);
    addAndMakeVisible(outputMeter);
    addAndMakeVisible(pureMixerValueSlider);
    addAndMakeVisible(routeSelector);
    addAndMakeVisible(muteButton);
    addAndMakeVisible(soloButton);
    addAndMakeVisible(eqButton);
    addAndMakeVisible(dynamicsButton);

    setSectionTitle(curveTitle,
                    "EqCurveEditor / TransferCurveEditor",
                    "EqCurveEditor: setBands(), getBands(), setBandChangeCallback()    TransferCurveEditor: setThreshold(), setRange(), setValueChangeCallback()");
    addAndMakeVisible(curveSection);
    addAndMakeVisible(curveTitle);
    addAndMakeVisible(eqCurveEditor);
    addAndMakeVisible(transferCurveEditor);

    for (auto* button : { &audioDownButton, &audioUpButton, &midiTriggerButton })
    {
        button->setLookAndFeel(nullptr);
        button->addListener(this);
    }

    setTheme(theme);
    meterValueSlider.setRange(0.0, 1.0, 0.01);
    meterValueSlider.setValue(0.65, juce::dontSendNotification);
    meterValueSlider.setValueChangeCallback([this](double value)
    {
        const auto normalised = static_cast<float>(value);
        peakMeter.setValue(-60.0f + normalised * 72.0f);
        truePeakMeter.setValue(-60.0f + normalised * 72.0f);
        loudnessMeter.setValue(-60.0f + normalised * 60.0f);
        rangeMeter.setValue(normalised * 72.0f);
    });

    onOffSwitch.setStateChangeCallback([this](bool isOn)
    {
        switchValue.setText(isOn ? utf8("当前状态：ON") : utf8("当前状态：OFF"),
                    juce::dontSendNotification);
    });
    settingsSlider.setRange(0.0, 100.0, 1.0);
    settingsSlider.setValue(50.0, juce::dontSendNotification);
    settingsSlider.setValueChangeCallback([this](double value)
    {
        sliderValue.setText(utf8("当前值：") + juce::String(static_cast<int>(value)),
                            juce::dontSendNotification);
    });
    switchValue.setText(utf8("当前状态：OFF"), juce::dontSendNotification);
    sliderValue.setText(utf8("当前值：50"), juce::dontSendNotification);

    for (auto* control : { &gainControl, &compControl, &gateControl, &auxControl })
    {
        control->setTheme(theme);
        control->setRange(0.0, 1.0);
    }
    gainControl.setRange(-24.0, 12.0);
    gainControl.setValue(0.0, juce::dontSendNotification);
    compControl.setValue(0.58, juce::dontSendNotification);
    gateControl.setValue(0.32, juce::dontSendNotification);
    auxControl.setValue(0.66, juce::dontSendNotification);
    verticalFader.setTheme(theme);
    verticalFader.setValue(-3.0f, juce::dontSendNotification);
    for (auto* meter : { &inputMeter, &reductionMeter, &gateMeter, &outputMeter })
        meter->setTheme(theme);
    inputMeter.setLevel(0.72f);
    reductionMeter.setLevel(0.42f);
    reductionMeter.setHold(0.48f);
    gateMeter.setLevel(0.28f);
    outputMeter.setLevel(0.64f);
    pureMixerValueSlider.setRange(0.0, 1.0, 0.01);
    pureMixerValueSlider.setValue(0.64, juce::dontSendNotification);
    pureMixerValueSlider.setValueChangeCallback([this](double value)
    {
        inputMeter.setLevel(static_cast<float>(value));
        outputMeter.setLevel(static_cast<float>(value * 0.86));
        reductionMeter.setLevel(static_cast<float>(value * 0.56));
        gateMeter.setLevel(static_cast<float>(value * 0.38));
        verticalFader.setValue(-60.0f + static_cast<float>(value) * 72.0f, juce::dontSendNotification);
    });
    routeSelector.setTheme(theme);
    routeSelector.setOptions({ "JACK mic_1", "JACK mic_2", "JACK system:capture_1" });
    routeSelector.setSelectedIndex(0);
    muteButton.setTheme(theme);
    soloButton.setTheme(theme);
    eqButton.setTheme(theme);
    dynamicsButton.setTheme(theme);
    muteButton.setAccent(juce::Colour(0xffff7b9a));
    soloButton.setAccent(juce::Colour(0xfff0c84b));
    eqButton.setAccent(juce::Colour(0xff8de3ff));
    dynamicsButton.setAccent(juce::Colour(0xff42d96f));
    eqCurveEditor.setTheme(theme);
    transferCurveEditor.setTheme(theme);
    routeSelector.setTheme(theme);
    muteButton.setTheme(theme);
    soloButton.setTheme(theme);
    eqButton.setTheme(theme);
    dynamicsButton.setTheme(theme);
    transferCurveEditor.setValueChangeCallback([this](float threshold, float range)
    {
        eqCurveEditor.setBands({ EqCurveEditor::Band { 80.0f, threshold * 8.0f, 0.7f, juce::Colour(0xff4bb7ff) },
                                 EqCurveEditor::Band { 620.0f, -range * 5.0f, 1.4f, juce::Colour(0xfff0c84b) },
                                 EqCurveEditor::Band { 2400.0f, 1.5f, 1.1f, juce::Colour(0xff9bdf73) },
                                 EqCurveEditor::Band { 10000.0f, 3.0f, 0.8f, juce::Colour(0xffff7b9a) } });
    });

    startTimer(250);
}

void ControlsDemoComponent::timerCallback()
{
    stopTimer();
    meterHistoryChart = std::make_unique<MeterHistoryChart>();
    meterHistoryChart->setTheme(theme);
    meterHistoryChart->setValueRange("+12 dBFS", "-60 dBFS");
    meterHistoryChart->setSecondaryValueRange("72", "0");
    addAndMakeVisible(*meterHistoryChart);
    resized();
}

void ControlsDemoComponent::setTheme(const ThemeContext& newTheme)
{
    theme = newTheme;
    audioLed.setLevel(0.65f, false);
    midiLed.trigger();
    peakMeter.setTheme(theme);
    truePeakMeter.setTheme(theme);
    loudnessMeter.setTheme(theme);
    rangeMeter.setTheme(theme);
    for (auto* lcd : { &stereoLcd, &surroundLcd, &midiLcd })
    {
        lcd->setTheme(theme);
        lcd->setAccent(juce::Colour(0xff8de3ff));
    }
    spatialPanner.setTheme(theme);
    mixerStrip.setTheme(theme);
    channelCard.setTheme(theme);
    onOffSwitch.setTheme(theme);
    settingsSlider.setTheme(theme);
    if (isShowing())
    {
        for (auto* control : { &gainControl, &compControl, &gateControl, &auxControl })
            control->setTheme(theme);
        verticalFader.setTheme(theme);
        for (auto* meter : { &inputMeter, &reductionMeter, &gateMeter, &outputMeter })
            meter->setTheme(theme);
        eqCurveEditor.setTheme(theme);
        transferCurveEditor.setTheme(theme);
        if (meterHistoryChart != nullptr)
            meterHistoryChart->setTheme(theme);
    }
    repaint();
}

void ControlsDemoComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(theme.colour("darkCanvas"));
}

void ControlsDemoComponent::resized()
{
    auto area = getLocalBounds().reduced(18);
    header.setBounds(area.removeFromTop(32));
    subtitle.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);

    const auto columnWidth = (area.getWidth() - 24) / 3;
    auto left = area.removeFromLeft(columnWidth);
    area.removeFromLeft(12);
    auto middle = area.removeFromLeft(columnWidth);
    area.removeFromLeft(12);
    auto right = area;
    layoutSection(ledSection, ledTitle, left.getX(), left.getY(), left.getWidth(), 150);
    layoutSection(meterSection, meterTitle, middle.getX(), middle.getY(), middle.getWidth(), 490);

    const auto nextY = left.getY() + 232;
    layoutSection(interactiveSection, interactiveTitle, left.getX(), nextY, left.getWidth(), 170);
    layoutSection(compositeSection, compositeTitle, middle.getX(), middle.getY() + 502, middle.getWidth(), 282);

    audioLed.setBounds(left.getX() + 24, left.getY() + 42, 48, 48);
    midiLed.setBounds(left.getX() + 94, left.getY() + 42, 48, 48);
    audioDownButton.setBounds(left.getX() + 160, left.getY() + 38, 130, 28);
    audioUpButton.setBounds(left.getX() + 160, left.getY() + 72, 130, 28);
    midiTriggerButton.setBounds(left.getX() + 24, left.getY() + 100, 180, 28);

    const auto meterX = middle.getX() + 22;
    peakMeter.setBounds(meterX, middle.getY() + 48, 48, 145);
    truePeakMeter.setBounds(meterX + 58, middle.getY() + 48, 48, 145);
    loudnessMeter.setBounds(meterX + 116, middle.getY() + 48, 48, 145);
    rangeMeter.setBounds(meterX + 174, middle.getY() + 48, 48, 145);
    meterValueSlider.setBounds(middle.getX() + 24, middle.getY() + 194, middle.getWidth() - 48, 22);
    const auto lcdX = middle.getX() + 20;
    const auto lcdWidth = middle.getWidth() - 40;
    stereoLcd.setBounds(lcdX, middle.getY() + 222, lcdWidth, 82);
    surroundLcd.setBounds(lcdX, middle.getY() + 310, lcdWidth, 82);
    midiLcd.setBounds(lcdX, middle.getY() + 398, lcdWidth, 82);

    onOffSwitch.setBounds(left.getX() + 24, nextY + 52, 56, 28);
    switchValue.setBounds(left.getX() + 94, nextY + 52, 140, 28);
    settingsSlider.setBounds(left.getX() + 24, nextY + 98, left.getWidth() - 48, 24);
    sliderValue.setBounds(left.getX() + 24, nextY + 126, 160, 24);

    spatialPanner.setBounds(middle.getX() + 20, middle.getY() + 554, 180, 180);
    mixerStrip.setBounds(middle.getX() + 220, middle.getY() + 554, 110, 180);
    channelCard.setBounds(middle.getX() + 340, middle.getY() + 554, middle.getWidth() - 360, 180);

    const auto pureMixerY = nextY + 190;
    layoutSection(pureMixerSection, pureMixerTitle, left.getX(), pureMixerY, left.getWidth(), 370);
    gainControl.setBounds(left.getX() + 18, pureMixerY + 48, 64, 84);
    compControl.setBounds(left.getX() + 86, pureMixerY + 48, 64, 84);
    gateControl.setBounds(left.getX() + 154, pureMixerY + 48, 64, 84);
    auxControl.setBounds(left.getX() + 222, pureMixerY + 48, 64, 84);
    verticalFader.setBounds(left.getX() + 24, pureMixerY + 138, 58, 136);
    inputMeter.setBounds(left.getX() + 112, pureMixerY + 142, 40, 130);
    reductionMeter.setBounds(left.getX() + 158, pureMixerY + 142, 40, 130);
    gateMeter.setBounds(left.getX() + 204, pureMixerY + 142, 40, 130);
    outputMeter.setBounds(left.getX() + 250, pureMixerY + 142, 40, 130);
    pureMixerValueSlider.setBounds(left.getX() + 18, pureMixerY + 270, left.getWidth() - 36, 22);
    routeSelector.setBounds(left.getX() + 18, pureMixerY + 300, left.getWidth() - 36, 24);
    muteButton.setBounds(left.getX() + 18, pureMixerY + 332, 72, 24);
    soloButton.setBounds(left.getX() + 96, pureMixerY + 332, 72, 24);
    eqButton.setBounds(left.getX() + 174, pureMixerY + 332, 72, 24);
    dynamicsButton.setBounds(left.getX() + 252, pureMixerY + 332, 72, 24);

    const auto curveY = right.getY();
    layoutSection(curveSection, curveTitle, right.getX(), curveY, right.getWidth(), 790);
    eqCurveEditor.setBounds(right.getX() + 16, curveY + 52, right.getWidth() - 32, 260);
    transferCurveEditor.setBounds(right.getX() + 16, curveY + 322, right.getWidth() - 32, 160);
    if (meterHistoryChart != nullptr)
        meterHistoryChart->setBounds(right.getX() + 16, curveY + 492, right.getWidth() - 32, 280);
}

void ControlsDemoComponent::setSectionTitle(juce::Label& label,
                                             const juce::String& title,
                                             const juce::String& functions)
{
    label.setText(title + "\n" + utf8("函数：") + functions, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, theme.colour("primaryText"));
    label.setFont(systemFont(12.0f));
    label.setJustificationType(juce::Justification::centredLeft);
}

void ControlsDemoComponent::buttonClicked(juce::Button* button)
{
    if (button == &audioDownButton)
        audioLed.setLevel(0.2f, false);
    else if (button == &audioUpButton)
        audioLed.setLevel(0.95f, true);
    else if (button == &midiTriggerButton)
        midiLed.trigger();
}

void ControlsDemoComponent::layoutSection(juce::Component& section,
                                          juce::Label& title,
                                          int x,
                                          int y,
                                          int width,
                                          int height)
{
    section.setBounds(x, y, width, height);
    title.setBounds(x + 12, y + 8, width - 24, 38);
}

} // namespace wjn::common