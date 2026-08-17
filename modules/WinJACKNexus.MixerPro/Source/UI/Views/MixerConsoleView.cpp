#include "UI/Views/MixerConsoleView.h"

#include "Audio/CommonJackMixerRuntime.h"

#include <WinJACKNexus/Common/Localization/LocaleManager.h>
#include <WinJACKNexus/Common/UI/HorizontalSliderControl.h>

#include <cmath>

namespace mixerpro
{

namespace
{
float meterValueToDb(float value) noexcept
{
    return value > 0.000001f ? juce::jmax(-60.0f, 20.0f * std::log10(value)) : -60.0f;
}

juce::String formatStereoPan(double value)
{
    const auto clamped = juce::jlimit(-1.0, 1.0, value);
    if (std::abs(clamped) < 0.005)
        return "C";

    const auto percentage = juce::roundToInt(std::abs(clamped) * 100.0);
    return (clamped < 0.0 ? "L" : "R") + juce::String(percentage) + "%";
}

std::array<float, 8> meterValuesToDb(const std::array<float, wjn::common::MeterFrame::maxChannels>& values,
                                     int channelCount) noexcept
{
    std::array<float, 8> result;
    const auto count = juce::jlimit(0, static_cast<int>(result.size()), channelCount);
    for (int channel = 0; channel < count; ++channel)
        result[static_cast<size_t>(channel)] = meterValueToDb(values[static_cast<size_t>(channel)]);
    return result;
}

void paintSharedSpatialPannerPreview(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef layout)
{
    if (bounds.getWidth() < 48 || bounds.getHeight() < 24)
        return;

    g.setColour(juce::Colour(0xff101318));
    g.fillRoundedRectangle(bounds.toFloat(), 5.0f);
    g.setColour(juce::Colour(0xff4c5664));
    g.drawRoundedRectangle(bounds.toFloat(), 5.0f, 1.0f);

    auto pannerColumn = bounds.removeFromLeft(46).reduced(6, 5);
    const auto fieldSize = juce::jmin(pannerColumn.getWidth(), pannerColumn.getHeight());
    auto field = pannerColumn.withSizeKeepingCentre(fieldSize, fieldSize);
    g.setColour(juce::Colour(0xff26303a));
    g.drawEllipse(field.toFloat(), 1.0f);
    g.drawLine(static_cast<float>(field.getCentreX()), static_cast<float>(field.getY()),
               static_cast<float>(field.getCentreX()), static_cast<float>(field.getBottom()), 1.0f);
    g.drawLine(static_cast<float>(field.getX()), static_cast<float>(field.getCentreY()),
               static_cast<float>(field.getRight()), static_cast<float>(field.getCentreY()), 1.0f);
    g.setColour(juce::Colour(0xff8de3ff));
    g.fillEllipse(static_cast<float>(field.getX()) + field.getWidth() * 0.62f - 4.0f,
                  static_cast<float>(field.getY()) + field.getHeight() * 0.34f - 4.0f,
                  8.0f,
                  8.0f);

    auto text = bounds.reduced(4, 4);
    g.setColour(juce::Colour(0xfff1f4f7));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText(layout + " Spatial", text.removeFromTop(13), juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff8de3ff));
    g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
    g.drawText("X +0.24  Y -0.32", text.removeFromTop(11), juce::Justification::centredLeft);

    auto meters = text.removeFromTop(10).reduced(0, 1);
    const std::array<float, 8> levels { 0.74f, 0.58f, 0.82f, 0.39f, 0.52f, 0.46f, 0.63f, 0.48f };
    const auto meterCount = layout == juce::StringRef("7.1") ? 8 : 6;
    const auto meterWidth = meters.getWidth() / meterCount;
    for (int meterIndex = 0; meterIndex < meterCount; ++meterIndex)
    {
        auto bar = meters.removeFromLeft(meterWidth).reduced(1, 1);
        const auto level = levels[static_cast<size_t>(meterIndex)];
        g.setColour(juce::Colour(0xff26303a));
        g.fillRect(bar);
        g.setColour(level > 0.72f ? juce::Colour(0xffe0bf35) : juce::Colour(0xff42d96f));
        g.fillRect(bar.withTrimmedTop(juce::roundToInt(static_cast<float>(bar.getHeight()) * (1.0f - level))));
    }
}

void paintSectionDivider(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
        return;

    const auto y = bounds.getCentreY();
    g.setColour(juce::Colour(0xff15181d));
    g.drawHorizontalLine(y, static_cast<float>(bounds.getX()), static_cast<float>(bounds.getRight()));
    g.setColour(juce::Colour(0xff343b45));
    g.drawHorizontalLine(y + 1, static_cast<float>(bounds.getX()), static_cast<float>(bounds.getRight()));
}

void addSectionGap(juce::Graphics& g, juce::Rectangle<int>& bounds, int height = 6)
{
    if (bounds.getHeight() <= 0)
        return;

    auto gap = bounds.removeFromTop(juce::jmin(height, bounds.getHeight()));
    paintSectionDivider(g, gap);
}
} // namespace

class MixerConsoleView::SpatialPannerSettingsComponent final : public juce::Component
{
public:
    SpatialPannerSettingsComponent(bool isSevenOneIn, const wjn::common::ThemeContext& themeIn)
        : isSevenOne(isSevenOneIn), theme(themeIn)
    {
        spatialControl.setCompactPreview(false);
        spatialControl.setTheme(theme);
        spatialControl.setPosition(0.62f, 0.34f);
        spatialControl.setIntensityGraphVisible(true);
        spatialControl.setPositionChangedCallback([this](juce::Point<float> newPosition)
        {
            sourcePosition = newPosition;
            repaint();
        });
        addAndMakeVisible(spatialControl);

        outputMeter.setChannelCount(isSevenOne ? 8 : 6);
        outputMeter.setTheme(theme);
        outputMeter.setAccent(juce::Colour(0xff8de3ff));
        outputMeter.setShowsOutput(true, juce::dontSendNotification);
        outputMeter.setPeakDb({ -12.0f, -19.0f, -16.0f, -29.0f, -26.0f, -24.0f, -31.0f, -28.0f });
        outputMeter.setHoldDb({ -8.0f, -16.0f, -12.0f, -24.0f, -21.0f, -20.0f, -27.0f, -24.0f });
        addAndMakeVisible(outputMeter);

        intensityControl.setText("");
        intensityControl.setToggleState(true, juce::dontSendNotification);
        intensityControl.setSwitchStyle(true);
        intensityControl.setAccent(juce::Colour(0xff8de3ff));
        intensityControl.setTheme(theme);
        intensityControl.setStateChangeCallback([this](bool active)
        {
            showIntensityGraph = active;
            spatialControl.setIntensityGraphVisible(active);
            repaint();
        });
        addAndMakeVisible(intensityControl);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(18);
        bounds.removeFromTop(30);
        bounds.removeFromTop(22);

        auto pannerRow = bounds.removeFromTop(300);
        auto channelMeters = pannerRow.removeFromRight(182).reduced(6, 0);
        auto padArea = pannerRow.reduced(16, 10);
        spatialControl.setBounds(padArea.expanded(8));

        auto meterContent = channelMeters.reduced(7, 8);
        meterContent.removeFromTop(16);
        outputMeter.setBounds(meterContent);

        auto toggleRow = bounds.removeFromTop(34).reduced(4, 3);
        auto toggleArea = toggleRow.removeFromLeft(210);
        toggleArea.removeFromLeft(130);
        intensityControl.setBounds(toggleArea.removeFromLeft(52).reduced(0, 4));

        auto controls = bounds.reduced(4, 8);
        for (auto& readout : readoutBounds)
            readout = controls.removeFromTop(28);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff17191c));

        auto bounds = getLocalBounds().reduced(18);
        g.setColour(juce::Colour(0xfff1f4f7));
        g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        const auto layoutName = isSevenOne ? "7.1" : "5.1";
        g.drawText(juce::String(layoutName) + " Spatial Panner", bounds.removeFromTop(30), juce::Justification::centredLeft);

        g.setColour(juce::Colour(0xff8de3ff));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(juce::String(layoutName) + " coordinate editor prototype", bounds.removeFromTop(22), juce::Justification::centredLeft);

        auto pannerRow = bounds.removeFromTop(300);
        auto channelMeters = pannerRow.removeFromRight(182).reduced(6, 0);
        g.setColour(juce::Colour(0xff242a32));
        g.fillRoundedRectangle(channelMeters.toFloat(), 6.0f);
        g.setColour(juce::Colour(0xff3a414c));
        g.drawRoundedRectangle(channelMeters.toFloat(), 6.0f, 1.0f);
        auto meterTitle = channelMeters.reduced(7, 8).removeFromTop(16);
        g.setColour(juce::Colour(0xfff1f4f7));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText("OUTPUT", meterTitle, juce::Justification::centred);

        auto toggleRow = bounds.removeFromTop(34).reduced(4, 3);
        auto toggleArea = toggleRow.removeFromLeft(210);
        g.setColour(juce::Colour(0xffc9d1da));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText("Intensity Curve", toggleArea.removeFromLeft(130), juce::Justification::centredLeft);

        auto controls = bounds.reduced(4, 8);
        drawReadout(g, controls.removeFromTop(28), "X", juce::String(sourcePosition.x * 2.0f - 1.0f, 2));
        drawReadout(g, controls.removeFromTop(28), "Y", juce::String(sourcePosition.y * 2.0f - 1.0f, 2));
        drawReadout(g, controls.removeFromTop(28), "Z", "+0.18");
        drawReadout(g, controls.removeFromTop(28), "Divergence", "42%");
    }

private:
    bool isSevenOne = false;
    bool showIntensityGraph = true;
    wjn::common::ThemeContext theme;
    juce::Point<float> sourcePosition { 0.62f, 0.34f };
    std::array<juce::Rectangle<int>, 4> readoutBounds {};
    wjn::common::SpatialPannerComponent spatialControl { false };
    wjn::common::MultiChannelMeterControl outputMeter;
    wjn::common::ToggleBadgeControl intensityControl;

    static void drawReadout(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef label, juce::StringRef value)
    {
        auto name = bounds.removeFromLeft(110);
        auto box = bounds.removeFromLeft(120).reduced(0, 3);

        g.setColour(juce::Colour(0xffc9d1da));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(label, name, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff101318));
        g.fillRoundedRectangle(box.toFloat(), 4.0f);
        g.setColour(juce::Colour(0xff8de3ff));
        g.drawText(value, box, juce::Justification::centred);
    }

    static void drawChannelMeters(juce::Graphics& g, juce::Rectangle<int> bounds, bool isSevenOne)
    {
        g.setColour(juce::Colour(0xff242a32));
        g.fillRoundedRectangle(bounds.toFloat(), 6.0f);
        g.setColour(juce::Colour(0xff3a414c));
        g.drawRoundedRectangle(bounds.toFloat(), 6.0f, 1.0f);

        auto content = bounds.reduced(7, 8);
        g.setColour(juce::Colour(0xfff1f4f7));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText("OUTPUT", content.removeFromTop(16), juce::Justification::centred);

        const std::array<const char*, 8> names { "L", "C", "R", "LFE", "Ls", "Rs", "Lrs", "Rrs" };
        const std::array<float, 8> values { 0.74f, 0.58f, 0.82f, 0.39f, 0.52f, 0.25f, 0.31f, 0.46f };
        const std::array<float, 8> peaks { 0.91f, 0.68f, 0.79f, 0.46f, 0.57f, 0.30f, 0.40f, 0.55f };
        const std::array<const char*, 8> readouts { "-12", "-19", "-16", "-29", "-26", "-24", "-31", "-28" };

        const auto channelCount = isSevenOne ? 8 : 6;
        auto meterArea = content.removeFromBottom(content.getHeight() - 14);
        const auto cellWidth = meterArea.getWidth() / channelCount;
        for (int channelIndex = 0; channelIndex < channelCount; ++channelIndex)
        {
            const auto i = static_cast<size_t>(channelIndex);
            auto cell = meterArea.removeFromLeft(cellWidth).reduced(2, 0);
            g.setColour(juce::Colour(0xffc9d1da));
            g.setFont(juce::FontOptions(7.0f, juce::Font::bold));
            g.drawText(names[i], cell.removeFromTop(14), juce::Justification::centred);

            auto readout = cell.removeFromBottom(13);
            auto rail = cell.reduced(2, 2);
            g.setColour(juce::Colour(0xff101318));
            g.fillRoundedRectangle(rail.toFloat(), 2.0f);
            g.setColour(juce::Colour(0xff151b22));
            g.drawRoundedRectangle(rail.toFloat(), 2.0f, 1.0f);

            const auto value = values[i];
            const auto fillHeight = juce::roundToInt(static_cast<float>(rail.getHeight()) * value);
            const auto active = rail.withTop(rail.getBottom() - fillHeight);
            const auto paintSegment = [&g, &rail, &active](float start, float end, juce::Colour colour)
            {
                auto segment = juce::Rectangle<int>(rail.getX(),
                                                    rail.getBottom() - juce::roundToInt(static_cast<float>(rail.getHeight()) * end),
                                                    rail.getWidth(),
                                                    juce::roundToInt(static_cast<float>(rail.getHeight()) * (end - start)));
                g.setColour(colour);
                g.fillRect(segment.getIntersection(active));
            };
            paintSegment(0.0f, 0.70f, juce::Colour(0xff42d96f));
            paintSegment(0.70f, 0.90f, juce::Colour(0xffe0bf35));
            paintSegment(0.90f, 1.00f, juce::Colour(0xffe34b4b));

            const auto peakY = rail.getBottom() - juce::roundToInt(static_cast<float>(rail.getHeight()) * peaks[i]);
            g.setColour(juce::Colour(0xfff6f8fb));
            g.fillRect(rail.getX() + 1, peakY, juce::jmax(1, rail.getWidth() - 2), 2);
            g.setColour(juce::Colour(0xff8de3ff));
            g.setFont(juce::FontOptions(7.0f, juce::Font::bold));
            g.drawText(readouts[i], readout, juce::Justification::centred);
        }
    }
};

class MixerConsoleView::SpatialPannerSettingsWindow final : public juce::DocumentWindow
{
public:
    SpatialPannerSettingsWindow(bool isSevenOne, const wjn::common::ThemeContext& theme)
        : DocumentWindow(juce::String("MixerPro ") + (isSevenOne ? "7.1" : "5.1") + " Spatial Panner",
                         juce::Colour(0xff17191c),
                         juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        setContentOwned(new SpatialPannerSettingsComponent(isSevenOne, theme), true);
        centreWithSize(520, 430);
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpatialPannerSettingsWindow)
};

class MixerConsoleView::AuxSendSettingsComponent final : public juce::Component
{
public:
    using LevelChangeCallback = std::function<void(int, double)>;

    explicit AuxSendSettingsComponent(const wjn::common::ThemeContext& themeIn,
                                      const std::array<double, 3>& initialLevels,
                                      LevelChangeCallback levelChangeCallbackIn)
        : theme(themeIn), levelChangeCallback(std::move(levelChangeCallbackIn))
    {
        const auto accent = juce::Colour(0xfff0c84b);
        const std::array<juce::String, 3> targets { "Stereo Aux Bus", "5.1 Aux Bus", "7.1 Aux Bus" };
        const std::array<int, 3> channelCounts { 2, 6, 8 };
        const std::array<bool, 3> preStates { false, true, false };
        const std::array<float, 3> peakSeeds { -12.0f, -9.0f, -14.0f };

        for (size_t index = 0; index < rowCount; ++index)
        {
            configureToggle(enabledControls[index], "ON", true, accent,
                            [this, index](bool active)
            {
                enabledControls[index].setText(active ? "ON" : "OFF");
                repaint();
            });
            configureToggle(preControls[index], preStates[index] ? "PRE" : "POST", preStates[index], accent,
                            [this, index](bool active)
            {
                preControls[index].setText(active ? "PRE" : "POST");
                repaint();
            });

            targetControls[index].setLabel("Target");
            targetControls[index].setOptions({ targets[index], "Master", "JACK Aux " + juce::String(static_cast<int>(index + 1)) });
            targetControls[index].setSelectedIndex(0);
            targetControls[index].setAccent(accent);
            targetControls[index].setTheme(theme);
            targetControls[index].setSelectionChangeCallback([this](int, const juce::String&) { repaint(); });

            levelControls[index].setRange(-60.0, 12.0);
            levelControls[index].setValue(initialLevels[index], juce::dontSendNotification);
            levelControls[index].setLabel("LEVEL");
            levelControls[index].setSuffix(" dB");
            levelControls[index].setAccent(accent);
            levelControls[index].setTheme(theme);
            levelControls[index].setValueChangeCallback([this, index](double value)
            {
                if (levelChangeCallback != nullptr)
                    levelChangeCallback(static_cast<int>(index), value);
                repaint();
            });

            addAndMakeVisible(enabledControls[index]);
            addAndMakeVisible(preControls[index]);
            addAndMakeVisible(targetControls[index]);
            addAndMakeVisible(levelControls[index]);

            addAndMakeVisible(meterSourceControls[index]);
            meterSourceControls[index].setText(outputMeters[index] ? "OUT" : "IN");
            meterSourceControls[index].setToggleState(outputMeters[index], juce::dontSendNotification);
            meterSourceControls[index].setAccent(accent);
            meterSourceControls[index].setTheme(theme);
            meterSourceControls[index].setStateChangeCallback([this, index](bool active)
            {
                outputMeters[index] = active;
                meterSourceControls[index].setText(active ? "OUT" : "IN");
                meters[index].setShowsOutput(active, juce::dontSendNotification);
                repaint();
            });

            addAndMakeVisible(meters[index]);
            meters[index].setChannelCount(channelCounts[index]);
            meters[index].setAccent(accent);
            meters[index].setTheme(theme);
            meters[index].setShowsOutput(outputMeters[index], juce::dontSendNotification);
            auto peak = std::array<float, 8> { peakSeeds[index], peakSeeds[index] - 2.0f, peakSeeds[index] - 4.0f,
                                                peakSeeds[index] - 8.0f, peakSeeds[index] - 6.0f, peakSeeds[index] - 9.0f,
                                                peakSeeds[index] - 7.0f, peakSeeds[index] - 5.0f };
            auto hold = peak;
            for (auto& value : hold)
                value += 3.0f;
            meters[index].setPeakDb(peak);
            meters[index].setHoldDb(hold);
        }

        addAndMakeVisible(panControl);
        panControl.setRange(-1.0, 1.0);
        panControl.setValue(0.0, juce::dontSendNotification);
        panControl.setLabel("PAN");
        panControl.setAccent(accent);
        panControl.setTheme(theme);
        panControl.setValueTextFormatter(formatStereoPan);
        panControl.setValueChangeCallback([this](double) { repaint(); });

        for (size_t index = 0; index < spatialControls.size(); ++index)
        {
            addAndMakeVisible(spatialControls[index]);
            spatialControls[index].setCompactPreview(true);
            spatialControls[index].setTheme(theme);
            spatialControls[index].setPosition(0.62f, 0.34f);
            spatialControls[index].setPositionChangedCallback([this](juce::Point<float>) { repaint(); });
            spatialControls[index].setDoubleClickCallback([this, index]
            {
                const auto isSevenOne = index == 1;
                if (spatialPannerWindow == nullptr || spatialPannerWindowIsSevenOne != isSevenOne)
                {
                    spatialPannerWindow.reset();
                    spatialPannerWindow = std::make_unique<SpatialPannerSettingsWindow>(isSevenOne, theme);
                    spatialPannerWindowIsSevenOne = isSevenOne;
                }
                spatialPannerWindow->setVisible(true);
                spatialPannerWindow->toFront(true);
            });
        }
    }

    void setLevels(const std::array<double, 3>& levels)
    {
        for (size_t index = 0; index < levelControls.size(); ++index)
            levelControls[index].setValue(levels[index], juce::dontSendNotification);
        repaint();
    }

    void setLevel(int index, double level)
    {
        if (index >= 0 && index < static_cast<int>(levelControls.size()))
            levelControls[static_cast<size_t>(index)].setValue(level, juce::dontSendNotification);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(18);
        bounds.removeFromTop(30);
        bounds.removeFromTop(24);
        bounds.removeFromTop(8);

        layoutStereoRow(bounds.removeFromTop(158), 0);
        bounds.removeFromTop(14);
        layoutSurroundRow(bounds.removeFromTop(158), 1);
        bounds.removeFromTop(14);
        layoutSurroundRow(bounds.removeFromTop(158), 2);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff17191c));

        auto bounds = getLocalBounds().reduced(18);
        g.setColour(juce::Colour(0xfff1f4f7));
        g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        g.drawText("Aux Send Settings - Input 5.1", bounds.removeFromTop(30), juce::Justification::centredLeft);

        g.setColour(juce::Colour(0xffc9d1da));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText("Detailed send matrix lives here; channel strip shows only the first three send knobs.",
                   bounds.removeFromTop(24),
                   juce::Justification::centredLeft);

        bounds.removeFromTop(8);
        drawStereoAux(g, bounds.removeFromTop(158), "AUX 1 - Stereo Reverb", -12, 0.42f, true, false, 0);
        bounds.removeFromTop(14);
        drawSurroundAux(g, bounds.removeFromTop(158), "AUX 2 - 5.1 Foldout", -9, true, true, "5.1 Aux Bus", "5.1", 6, 1);
        bounds.removeFromTop(14);
        drawSurroundAux(g, bounds.removeFromTop(158), "AUX 3 - 7.1 Surround FX", -14, true, false, "7.1 Aux Bus", "7.1", 8, 2);
    }

private:
    static constexpr size_t rowCount = 3;

    void configureToggle(wjn::common::ToggleBadgeControl& control,
                         juce::String text,
                         bool active,
                         juce::Colour accent,
                         std::function<void(bool)> callback)
    {
        control.setText(std::move(text));
        control.setToggleState(active, juce::dontSendNotification);
        control.setAccent(accent);
        control.setTheme(theme);
        control.setStateChangeCallback(std::move(callback));
    }

    void layoutStereoRow(juce::Rectangle<int> bounds, size_t row)
    {
        auto content = bounds.reduced(12).withTrimmedTop(30);
        const auto centreY = content.getCentreY() - 10;
        auto controlsBlock = juce::Rectangle<int>(content.getX(), centreY - 42, 230, 84);
        auto valueBlock = juce::Rectangle<int>(content.getX() + 270, centreY - 38, 100, 76);
        auto pannerBlock = juce::Rectangle<int>(content.getX() + 440, centreY - 38, 120, 76);
        auto outputBlock = juce::Rectangle<int>(bounds.getRight() - 88, bounds.getY() + 16, 64, bounds.getHeight() - 32);

        controlsBlock.removeFromTop(16);
        auto status = controlsBlock.removeFromTop(24);
        enabledControls[row].setBounds(status.removeFromLeft(60).reduced(2));
        preControls[row].setBounds(status.removeFromLeft(72).reduced(2));
        controlsBlock.removeFromTop(6);
        targetControls[row].setBounds(controlsBlock.removeFromTop(30));

        valueBlock.removeFromTop(16);
        levelControls[row].setBounds(valueBlock.withHeight(76));
        pannerBlock.removeFromTop(16);
        panControl.setBounds(pannerBlock.withHeight(76));

        meterSourceControls[row].setBounds(outputBlock.removeFromTop(18).reduced(2, 2));
        meters[row].setBounds(outputBlock.reduced(8, 10));
    }

    void layoutSurroundRow(juce::Rectangle<int> bounds, size_t row)
    {
        auto content = bounds.reduced(12).withTrimmedTop(30);
        const auto centreY = content.getCentreY() - 10;
        auto controlsBlock = juce::Rectangle<int>(content.getX(), centreY - 44, 230, 88);
        auto valueBlock = juce::Rectangle<int>(content.getX() + 270, centreY - 38, 100, 76);
        auto pannerBlock = juce::Rectangle<int>(content.getX() + 420, centreY - 35, 170, 70);
        auto outputBlock = juce::Rectangle<int>(bounds.getRight() - 88, bounds.getY() + 16, 64, bounds.getHeight() - 32);

        controlsBlock.removeFromTop(16);
        auto status = controlsBlock.removeFromTop(24);
        enabledControls[row].setBounds(status.removeFromLeft(60).reduced(2));
        preControls[row].setBounds(status.removeFromLeft(72).reduced(2));
        controlsBlock.removeFromTop(6);
        targetControls[row].setBounds(controlsBlock.removeFromTop(30));

        valueBlock.removeFromTop(16);
        levelControls[row].setBounds(valueBlock.withHeight(76));
        pannerBlock.removeFromTop(16);
        spatialControls[row - 1].setBounds(pannerBlock.withSizeKeepingCentre(112, 44));

        meterSourceControls[row].setBounds(outputBlock.removeFromTop(18).reduced(2, 2));
        meters[row].setBounds(outputBlock.reduced(8, 10));
    }

    static void drawMeterShell(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        g.setColour(juce::Colour(0xff242a32));
        g.fillRoundedRectangle(bounds.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff3a414c));
        g.drawRoundedRectangle(bounds.toFloat(), 5.0f, 1.0f);
    }
    static void drawShell(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef title)
    {
        g.setColour(juce::Colour(0xff242a32));
        g.fillRoundedRectangle(bounds.toFloat(), 6.0f);
        g.setColour(juce::Colour(0xff3a414c));
        g.drawRoundedRectangle(bounds.toFloat(), 6.0f, 1.0f);
        g.setColour(juce::Colour(0xfff0c84b));
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(title, bounds.reduced(12).removeFromTop(22), juce::Justification::centredLeft);
    }

    static void drawBadge(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef text, bool active)
    {
        g.setColour(active ? juce::Colour(0xff1f4936) : juce::Colour(0xff111418));
        g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
        g.setColour(active ? juce::Colour(0xff42d96f) : juce::Colour(0xff6c7480));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(text, bounds, juce::Justification::centred);
    }

    static void drawValue(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef label, juce::StringRef value)
    {
        g.setColour(juce::Colour(0xffc9d1da));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(label, bounds.removeFromLeft(86), juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff101318));
        g.fillRoundedRectangle(bounds.reduced(0, 4).toFloat(), 4.0f);
        g.setColour(juce::Colour(0xff8de3ff));
        g.drawText(value, bounds.reduced(8, 4), juce::Justification::centredLeft);
    }

    static void drawAuxKnob(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef label, juce::StringRef valueText, float normalisedValue)
    {
        auto value = juce::jlimit(0.0f, 1.0f, normalisedValue);
        const auto original = bounds;
        const auto centreX = original.getCentreX();
        auto knobArea = juce::Rectangle<int>(centreX - 21, original.getY() + 10, 42, 42).toFloat();
        const auto centre = knobArea.getCentre();
        const auto radius = knobArea.getWidth() * 0.5f;
        const auto angle = juce::jmap(value, -2.35f, 2.35f);

        g.setColour(juce::Colour(0xff101318));
        g.fillEllipse(knobArea);
        g.setColour(juce::Colour(0xff4c5664));
        g.drawEllipse(knobArea, 1.2f);
        g.setColour(juce::Colour(0xfff0c84b));
        g.drawLine(centre.x, centre.y,
                   centre.x + std::sin(angle) * radius * 0.72f,
                   centre.y - std::cos(angle) * radius * 0.72f,
                   2.0f);

        g.setColour(juce::Colour(0xffd6dde6));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(label, juce::Rectangle<int>(centreX - 34, original.getY() + 53, 68, 12), juce::Justification::centred);
        g.setColour(juce::Colour(0xff8de3ff));
        g.drawText(valueText, juce::Rectangle<int>(centreX - 34, original.getY() + 64, 68, 12), juce::Justification::centred);
    }

    void drawStereoAux(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef title, int levelDb, float pan, bool on, bool pre, int meterIndex)
    {
        juce::ignoreUnused(levelDb, pan, on, pre, meterIndex);
        drawShell(g, bounds, title);
        auto content = bounds.reduced(12).withTrimmedTop(30);
        const auto centreY = content.getCentreY() - 10;
        auto controlsBlock = juce::Rectangle<int>(content.getX(), centreY - 42, 230, 84);
        auto valueBlock = juce::Rectangle<int>(content.getX() + 270, centreY - 38, 100, 76);
        auto pannerBlock = juce::Rectangle<int>(content.getX() + 440, centreY - 38, 120, 76);
        auto outputBlock = juce::Rectangle<int>(bounds.getRight() - 88, bounds.getY() + 16, 64, bounds.getHeight() - 32);

        drawSectionLabel(g, controlsBlock.removeFromTop(16), "AUX", juce::Justification::centredLeft);
        auto status = controlsBlock.removeFromTop(24);
        status.removeFromLeft(60);
        status.removeFromLeft(72);
        controlsBlock.removeFromTop(6);
        controlsBlock.removeFromTop(30);

        drawSectionLabel(g, valueBlock.removeFromTop(16), "AUX VALUE", juce::Justification::centred);
        valueBlock.removeFromTop(76);

        drawSectionLabel(g, pannerBlock.removeFromTop(16), "PANNER", juce::Justification::centred);
        pannerBlock.removeFromTop(76);

        drawMeterShell(g, outputBlock);
        outputBlock.removeFromTop(18);
        outputBlock.removeFromTop(outputBlock.getHeight());
    }

    void drawSurroundAux(juce::Graphics& g,
                         juce::Rectangle<int> bounds,
                         juce::StringRef title,
                         int levelDb,
                         bool on,
                         bool pre,
                         juce::StringRef target,
                         juce::StringRef layout,
                         int channelCount,
                         int meterIndex)
    {
        juce::ignoreUnused(levelDb, on, pre, target, layout, channelCount, meterIndex);
        drawShell(g, bounds, title);
        auto content = bounds.reduced(12).withTrimmedTop(30);
        const auto centreY = content.getCentreY() - 10;
        auto controlsBlock = juce::Rectangle<int>(content.getX(), centreY - 44, 230, 88);
        auto valueBlock = juce::Rectangle<int>(content.getX() + 270, centreY - 38, 100, 76);
        auto pannerBlock = juce::Rectangle<int>(content.getX() + 420, centreY - 35, 170, 70);
        auto outputBlock = juce::Rectangle<int>(bounds.getRight() - 88, bounds.getY() + 16, 64, bounds.getHeight() - 32);

        drawSectionLabel(g, controlsBlock.removeFromTop(16), "AUX", juce::Justification::centredLeft);
        auto row = controlsBlock.removeFromTop(24);
        row.removeFromLeft(60);
        row.removeFromLeft(72);
        controlsBlock.removeFromTop(6);
        controlsBlock.removeFromTop(30);

        drawSectionLabel(g, valueBlock.removeFromTop(16), "AUX VALUE", juce::Justification::centred);
        valueBlock.removeFromTop(76);

        drawSectionLabel(g, pannerBlock.removeFromTop(16), "PANNER", juce::Justification::centred);
        pannerBlock.removeFromTop(70);

        drawMeterShell(g, outputBlock);
        outputBlock.removeFromTop(18);
        outputBlock.removeFromTop(outputBlock.getHeight());
    }

    static void drawPerChannelOutputMeter(juce::Graphics& g, juce::Rectangle<int> bounds, int channelCount, bool showsOutput)
    {
        g.setColour(juce::Colour(0xff242a32));
        g.fillRoundedRectangle(bounds.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff3a414c));
        g.drawRoundedRectangle(bounds.toFloat(), 5.0f, 1.0f);

        auto inner = bounds.reduced(8, 10);
        g.setColour(juce::Colour(0xff101318));
        g.fillRect(inner);

        const std::array<float, 8> levels { 0.68f, 0.54f, 0.72f, 0.38f, 0.44f, 0.31f, 0.36f, 0.48f };
        const std::array<float, 8> holds { 0.78f, 0.64f, 0.81f, 0.47f, 0.54f, 0.42f, 0.46f, 0.59f };
        channelCount = juce::jlimit(1, 8, channelCount);
        const auto cellWidth = inner.getWidth() / channelCount;

        const auto paintFill = [&](juce::Rectangle<int> rail, float value)
        {
            const auto fillHeight = juce::roundToInt(static_cast<float>(rail.getHeight()) * value);
            const auto fillTop = rail.getBottom() - fillHeight;
            const auto greenEnd = rail.getBottom() - juce::roundToInt(static_cast<float>(rail.getHeight()) * 0.68f);
            const auto yellowEnd = rail.getBottom() - juce::roundToInt(static_cast<float>(rail.getHeight()) * 0.89f);
            if (fillTop < yellowEnd)
            {
                g.setColour(juce::Colour(0xffe34b4b));
                g.fillRect(juce::Rectangle<int>(rail.getX(), fillTop, rail.getWidth(), juce::jmax(0, juce::jmin(yellowEnd, rail.getBottom()) - fillTop)));
            }
            if (fillTop < greenEnd)
            {
                const auto yellowTop = juce::jmax(fillTop, yellowEnd);
                g.setColour(juce::Colour(0xffe0bf35));
                g.fillRect(juce::Rectangle<int>(rail.getX(), yellowTop, rail.getWidth(), juce::jmax(0, greenEnd - yellowTop)));
            }
            g.setColour(juce::Colour(0xff42d96f));
            g.fillRect(juce::Rectangle<int>(rail.getX(), juce::jmax(fillTop, greenEnd), rail.getWidth(), rail.getBottom() - juce::jmax(fillTop, greenEnd)));
        };

        for (int channel = 0; channel < channelCount; ++channel)
        {
            auto cell = inner.removeFromLeft(channel + 1 == channelCount ? inner.getWidth() : cellWidth);
            auto rail = channelCount > 2 ? cell : cell.withSizeKeepingCentre(12, cell.getHeight());
            const auto level = juce::jmax(0.0f, levels[static_cast<size_t>(channel)] + (showsOutput ? 0.0f : -0.15f));
            paintFill(rail, level);
            const auto holdY = rail.getBottom() - juce::roundToInt(static_cast<float>(rail.getHeight()) * juce::jmax(0.0f, holds[static_cast<size_t>(channel)] + (showsOutput ? 0.0f : -0.15f)));
            g.setColour(juce::Colour(0xfff6f8fb));
            g.fillRect(juce::Rectangle<int>(rail.getX(), holdY, rail.getWidth(), 1));
        }
    }


    static void drawSurroundAuxWindowExample(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        g.setColour(juce::Colour(0xff20242a));
        g.fillRoundedRectangle(bounds.toFloat(), 6.0f);
        g.setColour(juce::Colour(0xff3a414c));
        g.drawRoundedRectangle(bounds.toFloat(), 6.0f, 1.0f);

        auto content = bounds.reduced(12);
        g.setColour(juce::Colour(0xfff0c84b));
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText("SURROUND AUX CHANNEL WINDOW EXAMPLE", content.removeFromTop(22), juce::Justification::centredLeft);

        auto controls = content.removeFromLeft(190);
        drawSectionLabel(g, controls.removeFromTop(16), "AUX CHANNEL", juce::Justification::centredLeft);
        auto badges = controls.removeFromTop(24);
        drawBadge(g, badges.removeFromLeft(64).reduced(2), "ON", true);
        drawBadge(g, badges.removeFromLeft(76).reduced(2), "5.1", true);
        controls.removeFromTop(8);
        drawTargetBox(g, controls.removeFromTop(30), "Input", "AUX 2 Send Bus");
        drawTargetBox(g, controls.removeFromTop(30), "Output", "Master 5.1");

        auto value = content.removeFromLeft(100);
        drawSectionLabel(g, value.removeFromTop(16), "RETURN", juce::Justification::centred);
        drawAuxKnob(g, value.removeFromTop(76), "LEVEL", "-6 dB", 0.82f);

        auto spatial = content.removeFromLeft(210);
        drawSectionLabel(g, spatial.removeFromTop(16), "SPATIAL", juce::Justification::centred);
        auto button = spatial.removeFromTop(76).withSizeKeepingCentre(178, 54);
        g.setColour(juce::Colour(0xff101318));
        g.fillRoundedRectangle(button.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff4c5664));
        g.drawRoundedRectangle(button.toFloat(), 5.0f, 1.0f);
        auto icon = button.removeFromLeft(56).reduced(10, 8);
        g.setColour(juce::Colour(0xff26303a));
        g.drawEllipse(icon.toFloat(), 1.0f);
        g.setColour(juce::Colour(0xff8de3ff));
        g.fillEllipse(static_cast<float>(icon.getCentreX()) + 1.0f, static_cast<float>(icon.getCentreY()) - 5.0f, 10.0f, 10.0f);
        g.setColour(juce::Colour(0xfff1f4f7));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText("5.1 Spatial Panner", button.removeFromTop(24), juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff8de3ff));
        g.drawText("OPEN PANNER", button, juce::Justification::centredLeft);

        auto meters = content;
        drawSectionLabel(g, meters.removeFromTop(16), "PER-CHANNEL OUTPUT", juce::Justification::centredLeft);
        const std::array<const char*, 6> names { "L", "C", "R", "Ls", "Rs", "LFE" };
        const std::array<float, 6> levels { 0.68f, 0.54f, 0.72f, 0.38f, 0.44f, 0.31f };
        for (size_t i = 0; i < names.size(); ++i)
        {
            auto row = meters.removeFromTop(18);
            g.setColour(juce::Colour(0xffc9d1da));
            g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
            g.drawText(names[i], row.removeFromLeft(28), juce::Justification::centredLeft);
            auto rail = row.reduced(0, 5);
            g.setColour(juce::Colour(0xff101318));
            g.fillRoundedRectangle(rail.toFloat(), 3.0f);
            g.setColour(levels[i] > 0.65f ? juce::Colour(0xffe0bf35) : juce::Colour(0xff42d96f));
            g.fillRoundedRectangle(rail.withWidth(juce::roundToInt(static_cast<float>(rail.getWidth()) * levels[i])).toFloat(), 3.0f);
        }
    }

    static void drawSectionLabel(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef text, juce::Justification justification)
    {
        g.setColour(juce::Colour(0xff8a94a3));
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText(text, bounds, justification);
    }

    static void drawTargetBox(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef label, juce::StringRef value)
    {
        g.setColour(juce::Colour(0xffc9d1da));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(label, bounds.removeFromLeft(52), juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff101318));
        g.fillRoundedRectangle(bounds.reduced(0, 5).toFloat(), 4.0f);
        g.setColour(juce::Colour(0xff8de3ff));
        g.drawText(value, bounds.reduced(8, 5), juce::Justification::centredLeft);
    }

    wjn::common::ThemeContext theme;
    std::array<wjn::common::ToggleBadgeControl, rowCount> enabledControls;
    std::array<wjn::common::ToggleBadgeControl, rowCount> preControls;
    std::array<wjn::common::RouteSelectorControl, rowCount> targetControls;
    std::array<wjn::common::RotaryControl, rowCount> levelControls;
    std::array<wjn::common::ToggleBadgeControl, rowCount> meterSourceControls;
    std::array<wjn::common::MultiChannelMeterControl, rowCount> meters;
    std::array<wjn::common::SpatialPannerComponent, 2> spatialControls {
        wjn::common::SpatialPannerComponent(false),
        wjn::common::SpatialPannerComponent(true)
    };
    wjn::common::RotaryControl panControl;
    std::array<bool, rowCount> outputMeters { true, false, true };
    bool spatialPannerWindowIsSevenOne = false;
    std::unique_ptr<SpatialPannerSettingsWindow> spatialPannerWindow;
    LevelChangeCallback levelChangeCallback;
};

class MixerConsoleView::AuxSendSettingsWindow final : public juce::DocumentWindow
{
public:
    explicit AuxSendSettingsWindow(const wjn::common::ThemeContext& theme,
                                   const std::array<double, 3>& initialLevels,
                                   AuxSendSettingsComponent::LevelChangeCallback levelChangeCallback)
        : DocumentWindow("MixerPro Aux Send Settings", juce::Colour(0xff17191c), juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        auto* content = new AuxSendSettingsComponent(theme, initialLevels, std::move(levelChangeCallback));
        setContentOwned(content, true);
        settingsComponent = content;
        centreWithSize(760, 650);
        setVisible(true);
    }

    void closeButtonPressed() override { setVisible(false); }

    void setLevels(const std::array<double, 3>& levels)
    {
        if (settingsComponent != nullptr)
            settingsComponent->setLevels(levels);
    }

    void setLevel(int index, double level)
    {
        if (settingsComponent != nullptr)
            settingsComponent->setLevel(index, level);
    }

private:
    AuxSendSettingsComponent* settingsComponent = nullptr;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AuxSendSettingsWindow)
};

class MixerConsoleView::ParametricEqSettingsComponent final : public juce::Component
{
public:
    using BandValues = std::array<double, 3>;
    using ChannelBandValues = std::array<BandValues, 4>;
    using AllChannelBandValues = std::array<ChannelBandValues, 8>;
    using GainChangeCallback = std::function<void(int, int, double)>;

    explicit ParametricEqSettingsComponent(int requestedChannelCount)
        : channelCount(juce::jlimit(1, 8, requestedChannelCount)), perChannelMode(channelCount > 1)
    {
        const std::array<float, 4> frequencies { 80.0f, 620.0f, 2400.0f, 10000.0f };
        const std::array<float, 4> gains { 2.5f, -3.0f, 1.5f, 3.0f };
        const std::array<float, 4> qValues { 0.70f, 1.40f, 1.10f, 0.80f };

        for (int i = 0; i < static_cast<int> (bands.size()); ++i)
        {
            auto& band = bands[static_cast<size_t> (i)];
            band.name = "Band " + juce::String (i + 1);
            band.type = i == 0 ? "Low Shelf" : (i == 3 ? "High Shelf" : "Bell");
            const std::array<juce::Colour, 4> bandColours {
                juce::Colour(0xff4bb7ff), juce::Colour(0xfff0c84b),
                juce::Colour(0xff9bdf73), juce::Colour(0xffff7b9a)
            };
            band.colour = bandColours[static_cast<size_t> (i)];
            configureKnob (band.frequency, 20.0, 20000.0, frequencies[static_cast<size_t> (i)], " Hz", 0.35);
            configureKnob (band.gain, -12.0, 12.0, gains[static_cast<size_t> (i)], " dB", 1.0);
            configureKnob (band.q, 0.20, 10.0, qValues[static_cast<size_t> (i)], " Q", 0.35);

            for (auto* slider : { &band.frequency, &band.gain, &band.q })
            {
                addAndMakeVisible (*slider);
                slider->onValueChange = [this, i] { storeBandValues(i); repaint(); };
            }
        }

        for (int channel = 0; channel < 8; ++channel)
            for (int bandIndex = 0; bandIndex < static_cast<int>(bands.size()); ++bandIndex)
            {
                const auto& band = bands[static_cast<size_t>(bandIndex)];
                channelBandValues[static_cast<size_t>(channel)][static_cast<size_t>(bandIndex)] =
                    { band.frequency.getValue(), band.gain.getValue() + (channel - 2) * 0.35, band.q.getValue() };
            }
    }

    ~ParametricEqSettingsComponent() override
    {
        for (auto& band : bands)
            for (auto* slider : { &band.frequency, &band.gain, &band.q })
                slider->onValueChange = nullptr;
    }

    void setChannelBandValues(const AllChannelBandValues& newValues)
    {
        channelBandValues = newValues;
        loadSelectedChannelValues();
        repaint();
    }

    void setGainChangeCallback(GainChangeCallback callback)
    {
        gainChangeCallback = std::move(callback);
    }

    void setChannelCount(int requestedChannelCount)
    {
        channelCount = juce::jlimit(1, 8, requestedChannelCount);
        perChannelMode = channelCount > 1;
        selectedChannel = -1;
        loadSelectedChannelValues();
        resized();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff17191c));
        auto bounds = getLocalBounds().reduced(18);

        g.setColour(juce::Colour(0xfff1f4f7));
        g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        g.drawText("Parametric EQ - " + layoutName(), bounds.removeFromTop(30), juce::Justification::centredLeft);

        if (channelCount > 1)
            drawChannelSelector(g, bounds.removeFromTop(28));

        auto graphPanel = bounds.removeFromTop(250).reduced(0, 8);
        auto inputMeter = graphPanel.removeFromLeft(48);
        graphPanel.removeFromLeft(6);
        auto outputMeter = graphPanel.removeFromRight(48);
        graphPanel.removeFromRight(6);
        graphBounds = graphPanel;
        drawEqSideMeter(g, inputMeter, "IN", 0.74f, 0.82f);
        drawEqSideMeter(g, outputMeter, "OUT", 0.68f, 0.76f);
        drawEqGraph(g, graphBounds);

        bounds.removeFromTop(4);
        for (const auto& band : bands)
            drawBand(g, bounds.removeFromTop(76), band);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(18);
        bounds.removeFromTop(30 + (channelCount > 1 ? 28 : 0) + 250 + 4);

        for (auto& band : bands)
        {
            auto row = bounds.removeFromTop(76).reduced(10, 4);
            row.removeFromLeft(70);
            row.removeFromLeft(106);
            band.frequency.setBounds(row.removeFromLeft(118).reduced(4, 0));
            band.gain.setBounds(row.removeFromLeft(118).reduced(4, 0));
            band.q.setBounds(row.removeFromLeft(104).reduced(4, 0));
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        const auto position = event.getPosition();
        if (perChannelToggleBounds.contains(position))
        {
            perChannelMode = ! perChannelMode;
            if (! perChannelMode)
                selectedChannel = -1;
            repaint();
            return;
        }
        const auto tabCount = perChannelMode ? channelCount + 1 : 1;
        for (int i = 0; i < tabCount; ++i)
            if (channelTabBounds[static_cast<size_t>(i)].contains(position))
            {
                selectedChannel = i - 1;
                loadSelectedChannelValues();
                repaint();
                return;
            }
        beginPointDrag(event);
    }
    void mouseDrag(const juce::MouseEvent& event) override { updatePointDrag(event); }
    void mouseUp(const juce::MouseEvent&) override { draggedBand = -1; }

private:
    struct Band
    {
        juce::String name, type;
        juce::Colour colour;
        juce::Slider frequency, gain, q;
    };

    std::array<Band, 4> bands;
    AllChannelBandValues channelBandValues {};
    juce::Rectangle<int> graphBounds;
    std::array<juce::Rectangle<int>, 9> channelTabBounds {};
    juce::Rectangle<int> perChannelToggleBounds;
    int draggedBand = -1;
    int channelCount = 1;
    int selectedChannel = -1;
    bool perChannelMode = true;
    bool loadingChannelValues = false;
    GainChangeCallback gainChangeCallback;

    static juce::String channelName(int index)
    {
        const std::array<const char*, 8> names { "L", "R", "C", "LFE", "Ls", "Rs", "Lrs", "Rrs" };
        return names[static_cast<size_t>(juce::jlimit(0, 7, index))];
    }

    juce::String layoutName() const
    {
        switch (channelCount)
        {
            case 1: return "Input Mono";
            case 2: return "Input Stereo";
            case 6: return "Input 5.1";
            case 8: return "Input 7.1";
            default: return juce::String(channelCount) + " Channel";
        }
    }

    static void drawChannelTab(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef text, bool active)
    {
        const auto accent = juce::Colour(0xff8de3ff);
        g.setColour(active ? accent.withAlpha(0.25f) : juce::Colour(0xff101318));
        g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
        g.setColour(active ? accent : juce::Colour(0xff6c7480));
        g.drawRoundedRectangle(bounds.toFloat(), 4.0f, 1.0f);
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText(text, bounds, juce::Justification::centred);
    }

    void drawChannelSelector(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        g.setColour(juce::Colour(0xff8a94a3));
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText("CHANNEL CONTROL", bounds.removeFromLeft(114), juce::Justification::centredLeft);
        perChannelToggleBounds = bounds.removeFromLeft(64).reduced(0, 3);
        drawChannelTab(g, perChannelToggleBounds, "PER-CH", perChannelMode);
        bounds.removeFromLeft(8);
        const auto tabCount = perChannelMode ? channelCount + 1 : 1;
        for (int i = 0; i < tabCount; ++i)
        {
            channelTabBounds[static_cast<size_t>(i)] = bounds.removeFromLeft(i == tabCount - 1 ? juce::jmin(52, bounds.getWidth()) : 48).reduced(2, 3);
            drawChannelTab(g, channelTabBounds[static_cast<size_t>(i)], i == 0 ? "ALL" : channelName(i - 1), selectedChannel == i - 1);
        }
    }

    void loadSelectedChannelValues()
    {
        const auto channel = selectedChannel < 0 ? 0 : selectedChannel;
        loadingChannelValues = true;
        for (int bandIndex = 0; bandIndex < static_cast<int>(bands.size()); ++bandIndex)
        {
            auto& band = bands[static_cast<size_t>(bandIndex)];
            const auto& values = channelBandValues[static_cast<size_t>(channel)][static_cast<size_t>(bandIndex)];
            band.frequency.setValue(values[0], juce::dontSendNotification);
            band.gain.setValue(values[1], juce::dontSendNotification);
            band.q.setValue(values[2], juce::dontSendNotification);
            if (gainChangeCallback != nullptr)
                gainChangeCallback(channel, bandIndex, values[1]);
        }
        loadingChannelValues = false;
    }

    void storeBandValues(int bandIndex)
    {
        if (loadingChannelValues)
            return;
        const auto& band = bands[static_cast<size_t>(bandIndex)];
        const BandValues values { band.frequency.getValue(), band.gain.getValue(), band.q.getValue() };
        const auto firstChannel = selectedChannel < 0 ? 0 : selectedChannel;
        const auto lastChannel = selectedChannel < 0 ? channelCount - 1 : selectedChannel;
        for (int channel = firstChannel; channel <= lastChannel; ++channel)
        {
            channelBandValues[static_cast<size_t>(channel)][static_cast<size_t>(bandIndex)] = values;
            if (gainChangeCallback != nullptr)
                gainChangeCallback(channel, bandIndex, values[1]);
        }
    }

    static void configureKnob(juce::Slider& slider, double minimum, double maximum, double value,
                              const juce::String& suffix, double skew)
    {
        slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 82, 18);
        slider.setRange(minimum, maximum, minimum < 20.0 ? 0.01 : 1.0);
        slider.setSkewFactor(skew);
        slider.setValue(value);
        slider.setTextValueSuffix(suffix);
        slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff8de3ff));
        slider.setColour(juce::Slider::thumbColourId, juce::Colour(0xfff1f4f7));
        slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffe5eaf0));
        slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff101318));
        slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }

    float frequencyToX(float frequency) const
    {
        auto graph = graphBounds.reduced(28, 20);
        const auto normalised = (std::log10(frequency) - std::log10(20.0f))
                              / (std::log10(20000.0f) - std::log10(20.0f));
        return static_cast<float>(graph.getX()) + normalised * static_cast<float>(graph.getWidth());
    }

    float gainToY(float gain) const
    {
        auto graph = graphBounds.reduced(28, 20);
        return static_cast<float>(graph.getCentreY()) - gain / 12.0f * static_cast<float>(graph.getHeight() / 2);
    }

    void beginPointDrag(const juce::MouseEvent& event)
    {
        if (!graphBounds.contains(event.getPosition()))
            return;

        for (int i = 0; i < static_cast<int> (bands.size()); ++i)
        {
            const auto& band = bands[static_cast<size_t> (i)];
            if (event.position.getDistanceFrom({ frequencyToX(static_cast<float>(band.frequency.getValue())),
                                                 gainToY(static_cast<float>(band.gain.getValue())) }) < 14.0f)
            {
                draggedBand = i;
                return;
            }
        }
    }

    void updatePointDrag(const juce::MouseEvent& event)
    {
        if (draggedBand < 0)
            return;

        auto graph = graphBounds.reduced(28, 20).toFloat();
        const auto x = juce::jlimit(graph.getX(), graph.getRight(), event.position.x);
        const auto y = juce::jlimit(graph.getY(), graph.getBottom(), event.position.y);
        const auto frequency = std::pow(10.0f, std::log10(20.0f)
            + (x - graph.getX()) / graph.getWidth() * (std::log10(20000.0f) - std::log10(20.0f)));
        const auto gain = juce::jlimit(-12.0f, 12.0f,
            (static_cast<float>(graph.getCentreY()) - y) / (graph.getHeight() / 2.0f) * 12.0f);
        auto& band = bands[static_cast<size_t> (draggedBand)];
        band.frequency.setValue(frequency, juce::sendNotificationSync);
        band.gain.setValue(gain, juce::sendNotificationSync);
    }

    void drawEqGraph(juce::Graphics& g, juce::Rectangle<int> bounds) const
    {
        g.setColour(juce::Colour(0xff101318));
        g.fillRoundedRectangle(bounds.toFloat(), 6.0f);
        g.setColour(juce::Colour(0xff3a414c));
        g.drawRoundedRectangle(bounds.toFloat(), 6.0f, 1.0f);

        auto graph = bounds.reduced(28, 20);
        g.setColour(juce::Colour(0xff26303a));
        for (int i = 0; i <= 8; ++i)
        {
            const auto x = graph.getX() + graph.getWidth() * i / 8;
            g.drawVerticalLine(x, static_cast<float>(graph.getY()), static_cast<float>(graph.getBottom()));
        }
        for (int i = 0; i <= 6; ++i)
        {
            const auto y = graph.getY() + graph.getHeight() * i / 6;
            g.drawHorizontalLine(y, static_cast<float>(graph.getX()), static_cast<float>(graph.getRight()));
        }

        juce::Path spectrum;
        spectrum.startNewSubPath(static_cast<float>(graph.getX()), static_cast<float>(graph.getBottom() - graph.getHeight() * 0.30f));
        for (int i = 1; i <= graph.getWidth(); i += 10)
        {
            const auto t = static_cast<float>(i) / static_cast<float>(graph.getWidth());
            const auto y = static_cast<float>(graph.getBottom()) - (0.22f + 0.22f * std::sin(t * 9.0f) + 0.32f * (1.0f - t)) * static_cast<float>(graph.getHeight());
            spectrum.lineTo(static_cast<float>(graph.getX() + i), y);
        }
        g.setColour(juce::Colour(0x664bb7ff));
        g.strokePath(spectrum, juce::PathStrokeType(1.0f));

        juce::Path eq;
        for (int pixel = 0; pixel < graph.getWidth(); ++pixel)
        {
            const auto frequency = std::pow(10.0f, std::log10(20.0f) + static_cast<float>(pixel) / graph.getWidth()
                * (std::log10(20000.0f) - std::log10(20.0f)));
            float totalGain = 0.0f;
            for (const auto& band : bands)
            {
                const auto distance = std::log2(frequency / static_cast<float>(band.frequency.getValue()));
                const auto width = juce::jmax(0.20f, 1.8f / static_cast<float>(band.q.getValue()));
                totalGain += static_cast<float>(band.gain.getValue()) * std::exp(-0.5f * (distance / width) * (distance / width));
            }
            const auto point = juce::Point<float>(static_cast<float>(graph.getX() + pixel),
                                                  static_cast<float>(graph.getCentreY()) - totalGain / 12.0f * graph.getHeight() / 2.0f);
            pixel == 0 ? eq.startNewSubPath(point) : eq.lineTo(point);
        }
        g.setColour(juce::Colour(0xff8de3ff));
        g.strokePath(eq, juce::PathStrokeType(2.5f));

        for (const auto& band : bands)
        {
            const auto centre = juce::Point<float>(frequencyToX(static_cast<float>(band.frequency.getValue())),
                                                   gainToY(static_cast<float>(band.gain.getValue())));
            g.setColour(juce::Colour(0xff101318));
            g.fillEllipse(centre.x - 7.0f, centre.y - 7.0f, 14.0f, 14.0f);
            g.setColour(band.colour);
            g.drawEllipse(centre.x - 6.0f, centre.y - 6.0f, 12.0f, 12.0f, 2.0f);
        }

        g.setColour(juce::Colour(0xffc9d1da));
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText("20 Hz", graph.withWidth(60).withY(graph.getBottom() - 14), juce::Justification::centredLeft);
        g.drawText("20 kHz", graph.withLeft(graph.getRight() - 60).withY(graph.getBottom() - 14), juce::Justification::centredRight);
        g.drawText("+12 dB", bounds.reduced(4).removeFromTop(18), juce::Justification::centredRight);
        g.drawText("-12 dB", bounds.reduced(4).removeFromBottom(18), juce::Justification::centredRight);
    }

    static void drawEqSideMeter(juce::Graphics& g,
                                juce::Rectangle<int> bounds,
                                juce::StringRef label,
                                float level,
                                float hold)
    {
        g.setColour(juce::Colour(0xff242a32));
        g.fillRoundedRectangle(bounds.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff3a414c));
        g.drawRoundedRectangle(bounds.toFloat(), 5.0f, 1.0f);
        auto title = bounds.removeFromTop(18);
        g.setColour(juce::Colour(0xffc9d1da));
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText(label, title, juce::Justification::centred);

        auto rail = bounds.withSizeKeepingCentre(16, bounds.getHeight() - 16).reduced(0, 8);
        g.setColour(juce::Colour(0xff101318));
        g.fillRect(rail);
        auto fill = rail.reduced(3, 3);
        const auto fillHeight = juce::roundToInt(static_cast<float>(fill.getHeight()) * juce::jlimit(0.0f, 1.0f, level));
        const auto fillTop = fill.getBottom() - fillHeight;
        const auto yellowTop = fill.getBottom() - juce::roundToInt(static_cast<float>(fill.getHeight()) * 0.72f);
        const auto redTop = fill.getBottom() - juce::roundToInt(static_cast<float>(fill.getHeight()) * 0.90f);
        g.setColour(juce::Colour(0xff42d96f));
        g.fillRect(juce::Rectangle<int>(fill.getX(), juce::jmax(fillTop, yellowTop), fill.getWidth(), fill.getBottom() - juce::jmax(fillTop, yellowTop)));
        if (fillTop < yellowTop)
        {
            const auto top = juce::jmax(fillTop, redTop);
            g.setColour(juce::Colour(0xffe0bf35));
            g.fillRect(juce::Rectangle<int>(fill.getX(), top, fill.getWidth(), yellowTop - top));
        }
        if (fillTop < redTop)
        {
            g.setColour(juce::Colour(0xffe34b4b));
            g.fillRect(juce::Rectangle<int>(fill.getX(), fillTop, fill.getWidth(), redTop - fillTop));
        }
        const auto holdY = rail.getBottom() - juce::roundToInt(static_cast<float>(rail.getHeight()) * juce::jlimit(0.0f, 1.0f, hold));
        g.setColour(juce::Colour(0xfff6f8fb));
        g.fillRect(juce::Rectangle<int>(rail.getX() + 2, holdY, rail.getWidth() - 4, 2));
    }

    static void drawBand(juce::Graphics& g, juce::Rectangle<int> bounds, const Band& band)
    {
        g.setColour(juce::Colour(0xff242a32));
        g.fillRoundedRectangle(bounds.reduced(0, 4).toFloat(), 5.0f);
        g.setColour(band.colour);
        g.fillRoundedRectangle(bounds.removeFromLeft(5).reduced(0, 6).toFloat(), 3.0f);

        auto content = bounds.reduced(10, 6);
        g.setColour(juce::Colour(0xfff1f4f7));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(band.name, content.removeFromLeft(70), juce::Justification::centredLeft);
        drawCell(g, content.removeFromLeft(106), band.type);
    }

    static void drawCell(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef text)
    {
        g.setColour(juce::Colour(0xff101318));
        g.fillRoundedRectangle(bounds.reduced(4, 5).toFloat(), 4.0f);
        g.setColour(juce::Colour(0xffc9d1da));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(text, bounds.reduced(8, 5), juce::Justification::centred);
    }
};

class MixerConsoleView::ParametricEqSettingsWindow final : public juce::DocumentWindow
{
public:
    explicit ParametricEqSettingsWindow(int channelCount,
                                        const ParametricEqSettingsComponent::AllChannelBandValues& initialValues,
                                        ParametricEqSettingsComponent::GainChangeCallback gainChangeCallback)
        : DocumentWindow("MixerPro Parametric EQ", juce::Colour(0xff17191c), juce::DocumentWindow::closeButton), layoutChannelCount(channelCount)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        auto* content = new ParametricEqSettingsComponent(channelCount);
        content->setChannelBandValues(initialValues);
        content->setGainChangeCallback(std::move(gainChangeCallback));
        setContentOwned(content, true);
        settingsComponent = content;
        centreWithSize(760, 660);
        setVisible(true);
    }

    void closeButtonPressed() override { setVisible(false); }

    int getChannelCount() const noexcept { return layoutChannelCount; }

    void setChannelBandValues(const ParametricEqSettingsComponent::AllChannelBandValues& values)
    {
        if (settingsComponent != nullptr)
            settingsComponent->setChannelBandValues(values);
    }

private:
    int layoutChannelCount = 1;
    ParametricEqSettingsComponent* settingsComponent = nullptr;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParametricEqSettingsWindow)
};

class MixerConsoleView::DynamicsSettingsComponent final : public juce::Component
{
public:
    using ThresholdValues = std::array<double, 2>;
    using ThresholdChangeCallback = std::function<void(int, double)>;

    explicit DynamicsSettingsComponent(int requestedChannelCount, const wjn::common::ThemeContext& themeIn)
        : channelCount(juce::jlimit(1, 8, requestedChannelCount)),
          perChannelMode(channelCount > 1),
          theme(themeIn)
    {
        addAndMakeVisible(perChannelControl);
        addAndMakeVisible(dynOnControl);
        addAndMakeVisible(listenControl);
        addAndMakeVisible(sidechainControl);
        addAndMakeVisible(meterSourceControl);
        addAndMakeVisible(compressorEnabledControl);
        addAndMakeVisible(gateEnabledControl);
        addAndMakeVisible(thresholdSlider);
        addAndMakeVisible(rangeSlider);

        for (auto& control : compressorControls)
            addAndMakeVisible(control);
        for (auto& control : gateControls)
            addAndMakeVisible(control);

        configureToggle(perChannelControl, "PER-CH", perChannelMode, juce::Colour(0xff42d96f),
                        [this](bool active)
        {
            perChannelMode = active;
            if (! perChannelMode)
                selectedChannel = -1;
            updateChannelTabs();
            resized();
            repaint();
        });
        configureToggle(dynOnControl, "DYN ON", true, juce::Colour(0xff42d96f),
                        [this](bool) { repaint(); });
        configureToggle(listenControl, "LISTEN", false, juce::Colour(0xff42d96f),
                        [this](bool) { repaint(); });
        configureToggle(sidechainControl, "SIDECHAIN", false, juce::Colour(0xff42d96f),
                        [this](bool) { repaint(); });
        configureToggle(meterSourceControl, "OUT", meterShowsOutput, juce::Colour(0xff42d96f),
                        [this](bool active)
        {
            meterShowsOutput = active;
            repaint();
        });
        configureToggle(compressorEnabledControl, "ON", true, juce::Colour(0xff8de3ff),
                        [this](bool) { repaint(); });
        configureToggle(gateEnabledControl, "ON", true, juce::Colour(0xff42d96f),
                        [this](bool) { repaint(); });

        const auto compressorAccent = juce::Colour(0xff8de3ff);
        configureKnob(compressorControls[0], "THRESH", -60.0f, 0.0f, -18.0f, " dB", compressorAccent);
        configureKnob(compressorControls[1], "RATIO", 1.0f, 20.0f, 4.0f, ":1", compressorAccent);
        configureKnob(compressorControls[2], "MAKEUP", -12.0f, 12.0f, 2.5f, " dB", compressorAccent);
        configureKnob(compressorControls[3], "ATTACK", 0.0f, 1000.0f, 18.0f, " ms", compressorAccent);
        configureKnob(compressorControls[4], "RELEASE", 0.0f, 1000.0f, 120.0f, " ms", compressorAccent);
        configureKnob(compressorControls[5], "KNEE", 0.0f, 24.0f, 6.0f, " dB", compressorAccent);

        const auto gateAccent = juce::Colour(0xff42d96f);
        configureKnob(gateControls[0], "THRESH", -60.0f, 0.0f, -42.0f, " dB", gateAccent);
        configureKnob(gateControls[1], "RANGE", -60.0f, 0.0f, -28.0f, " dB", gateAccent);
        configureKnob(gateControls[2], "HOLD", 0.0f, 500.0f, 80.0f, " ms", gateAccent);
        configureKnob(gateControls[3], "ATTACK", 0.0f, 1000.0f, 4.0f, " ms", gateAccent);
        configureKnob(gateControls[4], "RELEASE", 0.0f, 1000.0f, 180.0f, " ms", gateAccent);
        configureKnob(gateControls[5], "MODE", 0.0f, 1.0f, 0.52f, {}, gateAccent);

        thresholdSlider.setRange(0.0f, 1.0f);
        thresholdSlider.setValue(0.46f, juce::dontSendNotification);
        thresholdSlider.setAccent(compressorAccent);
        thresholdSlider.setTheme(theme);
        thresholdSlider.setValueChangeCallback([this](float) { repaint(); });
        rangeSlider.setRange(0.0f, 1.0f);
        rangeSlider.setValue(0.68f, juce::dontSendNotification);
        rangeSlider.setAccent(juce::Colour(0xffd8df39));
        rangeSlider.setTheme(theme);
        rangeSlider.setValueChangeCallback([this](float) { repaint(); });

        const std::array<juce::String, 4> meterLabels { "INPUT", "GR", "GATE", "OUTPUT" };
        const std::array<juce::Colour, 4> meterAccents {
            juce::Colour(0xff8de3ff), juce::Colour(0xffe0bf35),
            juce::Colour(0xff42d96f), juce::Colour(0xff8de3ff)
        };
        const std::array<float, 4> meterLevels { 0.84f, 0.41f, 0.28f, 0.72f };
        const std::array<float, 4> meterHolds { 0.88f, 0.48f, 0.34f, 0.78f };
        for (size_t index = 0; index < signalMeters.size(); ++index)
        {
            addAndMakeVisible(signalMeters[index]);
            signalMeters[index].setLabel(meterLabels[index]);
            signalMeters[index].setAccent(meterAccents[index]);
            signalMeters[index].setTheme(theme);
            signalMeters[index].setLevel(meterLevels[index]);
            signalMeters[index].setHold(meterHolds[index]);
        }

        for (size_t index = 0; index < channelTabs.size(); ++index)
        {
            addAndMakeVisible(channelTabs[index]);
            channelTabs[index].setText(index == 0 ? "ALL" : channelName(static_cast<int>(index - 1)));
            channelTabs[index].setAccent(juce::Colour(0xff42d96f));
            channelTabs[index].setTheme(theme);
            channelTabs[index].setStateChangeCallback([this, index](bool active)
            {
                if (! active)
                    return;
                selectedChannel = index == 0 ? -1 : static_cast<int>(index - 1);
                updateChannelTabs();
                updateProcessorValues();
                repaint();
            });
        }

        updateChannelTabs();
        updateProcessorValues();
    }

    void setThresholdValues(const ThresholdValues& values)
    {
        compressorControls[0].setValue(values[0], juce::dontSendNotification);
        gateControls[0].setValue(values[1], juce::dontSendNotification);
        repaint();
    }

    void setThresholdChangeCallback(ThresholdChangeCallback callback)
    {
        thresholdChangeCallback = std::move(callback);
        compressorControls[0].setValueChangeCallback([this](double value)
        {
            if (thresholdChangeCallback != nullptr)
                thresholdChangeCallback(0, value);
            repaint();
        });
        gateControls[0].setValueChangeCallback([this](double value)
        {
            if (thresholdChangeCallback != nullptr)
                thresholdChangeCallback(1, value);
            repaint();
        });
    }

    void setChannelCount(int requestedChannelCount)
    {
        channelCount = juce::jlimit(1, 8, requestedChannelCount);
        perChannelMode = channelCount > 1;
        selectedChannel = -1;
        updateChannelTabs();
        resized();
        repaint();
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(18);
        bounds.removeFromTop(32);
        bounds.removeFromTop(22);

        if (channelCount > 1)
        {
            auto selector = bounds.removeFromTop(28);
            selector.removeFromLeft(114);
            perChannelControl.setBounds(selector.removeFromLeft(64).reduced(0, 3));
            selector.removeFromLeft(8);

            const auto tabCount = perChannelMode ? channelCount + 1 : 1;
            for (int index = 0; index < tabCount; ++index)
            {
                const auto width = index == tabCount - 1 ? juce::jmin(52, selector.getWidth()) : 48;
                channelTabs[static_cast<size_t>(index)].setBounds(selector.removeFromLeft(width).reduced(2, 3));
            }
        }
        else
        {
            perChannelControl.setVisible(false);
            for (auto& tab : channelTabs)
                tab.setVisible(false);
        }

        auto utilityRow = bounds.removeFromTop(28);
        dynOnControl.setBounds(utilityRow.removeFromLeft(76).reduced(0, 3));
        utilityRow.removeFromLeft(8);
        listenControl.setBounds(utilityRow.removeFromLeft(82).reduced(0, 3));
        utilityRow.removeFromLeft(8);
        sidechainControl.setBounds(utilityRow.removeFromLeft(94).reduced(0, 3));
        utilityRow.removeFromLeft(8);
        meterSourceControl.setBounds(utilityRow.removeFromLeft(54).reduced(0, 3));

        bounds.removeFromTop(10);
        auto controls = bounds.removeFromRight(348);
        bounds.removeFromRight(12);
        auto graph = bounds.removeFromTop(260);
        layoutTransferControls(graph, thresholdSlider, rangeSlider);
        bounds.removeFromTop(12);
        layoutMeterControls(bounds, signalMeters);

        auto compressor = controls.removeFromTop(238);
        layoutProcessorControls(compressor, compressorControls, compressorEnabledControl);
        controls.removeFromTop(12);
        layoutProcessorControls(controls, gateControls, gateEnabledControl);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff17191c));
        auto bounds = getLocalBounds().reduced(18);

        g.setColour(juce::Colour(0xfff1f4f7));
        g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
        g.drawText("Compressor / Gate", bounds.removeFromTop(32), juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff8de3ff));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText("Full dynamics processor · " + layoutName(), bounds.removeFromTop(22), juce::Justification::centredLeft);

        if (channelCount > 1)
            drawChannelSelector(g, bounds.removeFromTop(28));

        auto utilityRow = bounds.removeFromTop(28);
        utilityRow.removeFromLeft(76);
        utilityRow.removeFromLeft(8);
        utilityRow.removeFromLeft(82);
        utilityRow.removeFromLeft(8);
        utilityRow.removeFromLeft(94);
        utilityRow.removeFromLeft(8);
        utilityRow.removeFromLeft(54);
        g.setColour(juce::Colour(0xff8a94a3));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText("DUAL STAGE DYNAMICS", utilityRow, juce::Justification::centredRight);
        bounds.removeFromTop(10);

        auto controls = bounds.removeFromRight(348);
        bounds.removeFromRight(12);
        auto graph = bounds.removeFromTop(260);
        drawTransferGraph(g, graph);
        bounds.removeFromTop(12);
        drawMeterPanel(g, bounds, meterShowsOutput);

        auto compressor = controls.removeFromTop(238);
        const auto channelOffset = selectedChannel < 0 ? 0.0f : static_cast<float>(selectedChannel - 2) * 0.035f;
        drawProcessorPanel(g,
                           compressor,
                           "COMPRESSOR",
                           juce::Colour(0xff8de3ff),
                           { { "THRESH", "RATIO", "MAKEUP", "ATTACK", "RELEASE", "KNEE" } },
                           { { "-18 dB", "4:1", "+2.5 dB", "18 ms", "120 ms", "6 dB" } },
                           { { 0.62f + channelOffset, 0.47f - channelOffset, 0.58f + channelOffset, 0.35f, 0.63f - channelOffset, 0.44f + channelOffset } });
        controls.removeFromTop(12);
        drawProcessorPanel(g,
                           controls,
                           "GATE / EXPANDER",
                           juce::Colour(0xff42d96f),
                           { { "THRESH", "RANGE", "HOLD", "ATTACK", "RELEASE", "MODE" } },
                           { { "-42 dB", "-28 dB", "80 ms", "4 ms", "180 ms", "EXP" } },
                           { { 0.31f - channelOffset, 0.54f + channelOffset, 0.40f, 0.25f + channelOffset, 0.59f - channelOffset, 0.52f } });
    }

private:
    void configureKnob(wjn::common::RotaryControl& control,
                       juce::String label,
                       float minimum,
                       float maximum,
                       float value,
                       juce::String suffix,
                       juce::Colour accent)
    {
        control.setRange(minimum, maximum);
        control.setValue(value, juce::dontSendNotification);
        control.setLabel(std::move(label));
        control.setSuffix(std::move(suffix));
        control.setAccent(accent);
        control.setTheme(theme);
        control.setValueChangeCallback([this](double) { repaint(); });
    }

    void configureToggle(wjn::common::ToggleBadgeControl& control,
                         juce::String text,
                         bool active,
                         juce::Colour accent,
                         std::function<void(bool)> callback)
    {
        control.setText(std::move(text));
        control.setToggleState(active, juce::dontSendNotification);
        control.setAccent(accent);
        control.setTheme(theme);
        control.setStateChangeCallback(std::move(callback));
    }

    void updateChannelTabs()
    {
        perChannelControl.setVisible(channelCount > 1);
        perChannelControl.setToggleState(perChannelMode, juce::dontSendNotification);
        for (size_t index = 0; index < channelTabs.size(); ++index)
        {
            const auto visible = index == 0 || (perChannelMode && static_cast<int>(index) <= channelCount);
            channelTabs[index].setVisible(visible);
            channelTabs[index].setToggleState(
                visible && (index == 0 ? selectedChannel < 0 : selectedChannel == static_cast<int>(index - 1)),
                juce::dontSendNotification);
        }
    }

    void updateProcessorValues()
    {
        const auto channelOffset = selectedChannel < 0 ? 0.0 : static_cast<double>(selectedChannel - 2) * 0.035;
        const std::array<double, 6> compressorValues {
            -18.0 + channelOffset * 36.0,
            4.0 - channelOffset * 8.0,
            2.5 + channelOffset * 12.0,
            18.0,
            120.0 - channelOffset * 180.0,
            6.0 + channelOffset * 12.0
        };
        const std::array<double, 6> gateValues {
            -42.0 - channelOffset * 36.0,
            -28.0 + channelOffset * 36.0,
            80.0,
            4.0 + channelOffset * 12.0,
            180.0 - channelOffset * 120.0,
            0.52
        };
        for (size_t index = 0; index < compressorControls.size(); ++index)
        {
            compressorControls[index].setValue(compressorValues[index], juce::dontSendNotification);
            gateControls[index].setValue(gateValues[index], juce::dontSendNotification);
        }
        if (thresholdChangeCallback != nullptr)
        {
            thresholdChangeCallback(0, compressorValues[0]);
            thresholdChangeCallback(1, gateValues[0]);
        }
    }

    static void layoutProcessorControls(juce::Rectangle<int> bounds,
                                        std::array<wjn::common::RotaryControl, 6>& controls,
                                        wjn::common::ToggleBadgeControl& enabled)
    {
        auto content = bounds.reduced(12, 10);
        content.removeFromTop(18);
        enabled.setBounds(content.removeFromTop(22).withLeft(content.getRight() - 46).reduced(0, 2));
        content.removeFromTop(4);

        for (int row = 0; row < 2; ++row)
        {
            auto knobRow = content.removeFromTop(76);
            for (int column = 0; column < 3; ++column)
            {
                const auto index = static_cast<size_t>(row * 3 + column);
                const auto width = column == 2 ? knobRow.getWidth() : knobRow.getWidth() / (3 - column);
                controls[index].setBounds(knobRow.removeFromLeft(width));
            }
        }
    }

    static void layoutTransferControls(juce::Rectangle<int> bounds,
                                       wjn::common::HorizontalSliderControl& threshold,
                                       wjn::common::HorizontalSliderControl& range)
    {
        auto inner = bounds.reduced(12);
        auto sliderArea = inner.removeFromBottom(32);
        threshold.setBounds(sliderArea.removeFromTop(14).reduced(36, 0));
        range.setBounds(sliderArea.reduced(36, 0));
    }

    static void layoutMeterControls(juce::Rectangle<int> bounds,
                                    std::array<wjn::common::SegmentedMeterControl, 4>& meters)
    {
        auto content = bounds.reduced(14, 10);
        content.removeFromTop(16);
        auto meterArea = content.reduced(8, 4);
        const auto cellWidth = meterArea.getWidth() / static_cast<int>(meters.size());
        for (size_t index = 0; index < meters.size(); ++index)
        {
            const auto width = index + 1 == meters.size() ? meterArea.getWidth() : cellWidth;
            meters[index].setBounds(meterArea.removeFromLeft(width));
        }
    }

    static void drawPanel(juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour accent)
    {
        g.setColour(juce::Colour(0xff242a32));
        g.fillRoundedRectangle(bounds.toFloat(), 6.0f);
        g.setColour(accent.withAlpha(0.45f));
        g.drawRoundedRectangle(bounds.toFloat(), 6.0f, 1.0f);
    }

    static void drawToggle(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef text, bool active)
    {
        g.setColour(active ? juce::Colour(0xff1f4936) : juce::Colour(0xff101318));
        g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
        g.setColour(active ? juce::Colour(0xff42d96f) : juce::Colour(0xff6c7480));
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText(text, bounds, juce::Justification::centred);
    }

    static juce::String channelName(int index)
    {
        const std::array<const char*, 8> names { "L", "R", "C", "LFE", "Ls", "Rs", "Lrs", "Rrs" };
        return names[static_cast<size_t>(juce::jlimit(0, 7, index))];
    }

    juce::String layoutName() const
    {
        switch (channelCount)
        {
            case 1: return "Input Mono";
            case 2: return "Input Stereo";
            case 6: return "Input 5.1";
            case 8: return "Input 7.1";
            default: return juce::String(channelCount) + " Channel";
        }
    }

    void drawChannelSelector(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        g.setColour(juce::Colour(0xff8a94a3));
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText("CHANNEL CONTROL", bounds.removeFromLeft(114), juce::Justification::centredLeft);
    }

    static void drawKnob(juce::Graphics& g,
                         juce::Rectangle<int> bounds,
                         juce::StringRef label,
                         juce::StringRef value,
                         float normalisedValue,
                         juce::Colour accent)
    {
        const auto centreX = bounds.getCentreX();
        auto dial = juce::Rectangle<int>(centreX - 22, bounds.getY() + 2, 44, 44).toFloat();
        const auto centre = dial.getCentre();
        const auto angle = juce::jmap(juce::jlimit(0.0f, 1.0f, normalisedValue), -2.35f, 2.35f);
        g.setColour(juce::Colour(0xff101318));
        g.fillEllipse(dial);
        g.setColour(juce::Colour(0xff4c5664));
        g.drawEllipse(dial, 1.2f);
        g.setColour(accent);
        g.drawLine(centre.x, centre.y,
                   centre.x + std::sin(angle) * 15.0f,
                   centre.y - std::cos(angle) * 15.0f,
                   2.0f);
        g.setColour(juce::Colour(0xffc9d1da));
        g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        g.drawText(label, juce::Rectangle<int>(bounds.getX(), bounds.getY() + 47, bounds.getWidth(), 12), juce::Justification::centred);
        g.setColour(accent);
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText(value, juce::Rectangle<int>(bounds.getX(), bounds.getY() + 59, bounds.getWidth(), 14), juce::Justification::centred);
    }

    static void drawProcessorPanel(juce::Graphics& g,
                                   juce::Rectangle<int> bounds,
                                   juce::StringRef title,
                                   juce::Colour accent,
                                   std::array<const char*, 6> labels,
                                   std::array<const char*, 6> values,
                                   std::array<float, 6> positions)
    {
        juce::ignoreUnused(labels, values, positions);
        drawPanel(g, bounds, accent);
        auto content = bounds.reduced(12, 10);
        g.setColour(accent);
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(title, content.removeFromTop(18), juce::Justification::centredLeft);
    }

    static void drawTransferGraph(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        drawPanel(g, bounds, juce::Colour(0xff8de3ff));
        auto inner = bounds.reduced(12);
        auto sliderArea = inner.removeFromBottom(32);
        auto plot = inner.reduced(22, 8);
        g.setColour(juce::Colour(0xff101318));
        g.fillRect(plot);
        g.setColour(juce::Colour(0xff26303a));
        for (int i = 0; i <= 6; ++i)
        {
            const auto x = plot.getX() + i * plot.getWidth() / 6;
            const auto y = plot.getY() + i * plot.getHeight() / 6;
            g.drawVerticalLine(x, static_cast<float>(plot.getY()), static_cast<float>(plot.getBottom()));
            g.drawHorizontalLine(y, static_cast<float>(plot.getX()), static_cast<float>(plot.getRight()));
        }

        g.setColour(juce::Colour(0xff49718a));
        g.drawLine(static_cast<float>(plot.getX()), static_cast<float>(plot.getBottom()),
                   static_cast<float>(plot.getRight()), static_cast<float>(plot.getY()), 1.0f);

        const auto thresholdX = plot.getX() + plot.getWidth() * 46 / 100;
        const auto thresholdY = plot.getBottom() - plot.getHeight() * 46 / 100;
        juce::Path compression;
        compression.startNewSubPath(static_cast<float>(plot.getX()), static_cast<float>(plot.getBottom()));
        compression.lineTo(static_cast<float>(thresholdX - 16), static_cast<float>(thresholdY + 16));
        compression.cubicTo(static_cast<float>(thresholdX - 2), static_cast<float>(thresholdY + 2),
                            static_cast<float>(thresholdX + 14), static_cast<float>(thresholdY - 4),
                            static_cast<float>(plot.getRight()), static_cast<float>(plot.getY() + plot.getHeight() * 28 / 100));
        g.setColour(juce::Colour(0xff8de3ff));
        g.strokePath(compression, juce::PathStrokeType(2.2f));

        juce::Path gate;
        gate.startNewSubPath(static_cast<float>(plot.getX()), static_cast<float>(plot.getBottom()));
        gate.lineTo(static_cast<float>(plot.getX() + plot.getWidth() * 24 / 100), static_cast<float>(plot.getBottom()));
        gate.cubicTo(static_cast<float>(plot.getX() + plot.getWidth() * 31 / 100), static_cast<float>(plot.getBottom() - 6),
                     static_cast<float>(plot.getX() + plot.getWidth() * 38 / 100), static_cast<float>(plot.getBottom() - plot.getHeight() * 31 / 100),
                     static_cast<float>(thresholdX), static_cast<float>(thresholdY + 10));
        g.setColour(juce::Colour(0xffd8df39));
        g.strokePath(gate, juce::PathStrokeType(1.7f));

        g.setColour(juce::Colour(0xfff1f4f7));
        g.fillRect(juce::Rectangle<int>(thresholdX - 2, thresholdY - 2, 5, 5));
        g.setColour(juce::Colour(0xffd8df39));
        g.fillRect(juce::Rectangle<int>(plot.getRight() - 4, plot.getY() + plot.getHeight() * 28 / 100 - 2, 5, 5));

        auto thresholdSlider = sliderArea.removeFromTop(14);
        auto rangeSlider = sliderArea;

        g.setColour(juce::Colour(0xffc9d1da));
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText("TRANSFER", bounds.reduced(10).removeFromTop(14), juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff8a94a3));
        g.drawText("-100", plot.withY(plot.getBottom() - 13).withWidth(28), juce::Justification::centredLeft);
        g.drawText("0 dB", plot.withLeft(plot.getRight() - 28).withY(plot.getY() + 2), juce::Justification::centredRight);
        g.setColour(juce::Colour(0xff8de3ff));
        g.drawText("THRESH", thresholdSlider.removeFromLeft(34), juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffd8df39));
        g.drawText("RANGE", rangeSlider.removeFromLeft(34), juce::Justification::centredLeft);
    }

    static void drawMeterPanel(juce::Graphics& g, juce::Rectangle<int> bounds, bool showsOutput)
    {
        juce::ignoreUnused(showsOutput);
        drawPanel(g, bounds, juce::Colour(0xff42d96f));
        auto content = bounds.reduced(14, 10);
        g.setColour(juce::Colour(0xffc9d1da));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText("SIGNAL / GAIN REDUCTION", content.removeFromTop(16), juce::Justification::centredLeft);
    }

    wjn::common::ThemeContext theme;
    wjn::common::ToggleBadgeControl perChannelControl;
    wjn::common::ToggleBadgeControl dynOnControl;
    wjn::common::ToggleBadgeControl listenControl;
    wjn::common::ToggleBadgeControl sidechainControl;
    wjn::common::ToggleBadgeControl meterSourceControl;
    wjn::common::ToggleBadgeControl compressorEnabledControl;
    wjn::common::ToggleBadgeControl gateEnabledControl;
    std::array<wjn::common::ToggleBadgeControl, 9> channelTabs;
    std::array<wjn::common::RotaryControl, 6> compressorControls;
    std::array<wjn::common::RotaryControl, 6> gateControls;
    wjn::common::HorizontalSliderControl thresholdSlider;
    wjn::common::HorizontalSliderControl rangeSlider;
    std::array<wjn::common::SegmentedMeterControl, 4> signalMeters;
    int channelCount = 1;
    int selectedChannel = -1;
    bool perChannelMode = true;
    bool meterShowsOutput = true;
    ThresholdChangeCallback thresholdChangeCallback;
};

class MixerConsoleView::DynamicsSettingsWindow final : public juce::DocumentWindow
{
public:
    explicit DynamicsSettingsWindow(int channelCount,
                                    const wjn::common::ThemeContext& themeIn,
                                    const DynamicsSettingsComponent::ThresholdValues& initialValues,
                                    DynamicsSettingsComponent::ThresholdChangeCallback thresholdChangeCallback)
        : DocumentWindow("MixerPro Compressor / Gate", juce::Colour(0xff17191c), juce::DocumentWindow::closeButton),
          layoutChannelCount(channelCount),
          theme(themeIn)
    {
        setUsingNativeTitleBar(true); setResizable(true, true);
        auto* content = new DynamicsSettingsComponent(channelCount, theme);
        content->setThresholdValues(initialValues);
        content->setThresholdChangeCallback(std::move(thresholdChangeCallback));
        setContentOwned(content, true);
        settingsComponent = content;
        centreWithSize(900, 640); setVisible(true);
    }
    void closeButtonPressed() override { setVisible(false); }

    int getChannelCount() const noexcept { return layoutChannelCount; }

    void setThresholdValues(const DynamicsSettingsComponent::ThresholdValues& values)
    {
        if (settingsComponent != nullptr)
            settingsComponent->setThresholdValues(values);
    }

private:
    int layoutChannelCount = 1;
    wjn::common::ThemeContext theme;
    DynamicsSettingsComponent* settingsComponent = nullptr;
};

class MixerConsoleView::ChannelFaderControlComponent final : public juce::Component
{
public:
    explicit ChannelFaderControlComponent(int channels, const wjn::common::ThemeContext& themeIn)
        : channelCount(juce::jlimit(1, 8, channels)),
          theme(themeIn)
    {
        addAndMakeVisible(modeControl);
        modeControl.setText("LINKED");
        modeControl.setToggleState(channelsLinked, juce::dontSendNotification);
        modeControl.setAccent(juce::Colour(0xff42d96f));
        modeControl.setTheme(theme);
        modeControl.setStateChangeCallback([this](bool active)
        {
            channelsLinked = active;
            modeControl.setText(channelsLinked ? "LINKED" : "SPLIT");
            repaint();
        });

        const std::array<float, 8> levels { 0.86f, 0.82f, 0.85f, 0.70f, 0.78f, 0.75f, 0.66f, 0.71f };
        const std::array<float, 8> holds { 0.90f, 0.87f, 0.89f, 0.76f, 0.82f, 0.80f, 0.71f, 0.77f };
        const std::array<float, 8> faderValues { 0.64f, 0.64f, 0.60f, 0.52f, 0.58f, 0.58f, 0.55f, 0.55f };
        const std::array<juce::String, 8> labels { "L", "R", "C", "LFE", "Ls", "Rs", "Lrs", "Rrs" };
        for (size_t index = 0; index < meters.size(); ++index)
        {
            addAndMakeVisible(meters[index]);
            meters[index].setLabel({});
            meters[index].setAccent(juce::Colour(0xff8de3ff));
            meters[index].setTheme(theme);
            meters[index].setLevel(levels[index]);
            meters[index].setHold(holds[index]);

            addAndMakeVisible(muteControls[index]);
            muteControls[index].setText("MUTE");
            muteControls[index].setTheme(theme);
            muteControls[index].setAccent(juce::Colour(0xff6c7480));
            muteControls[index].setStateChangeCallback([this, index](bool active)
            {
                if (channelsLinked)
                    for (auto& control : muteControls)
                        control.setToggleState(active, juce::dontSendNotification);
                juce::ignoreUnused(index);
                repaint();
            });

            addAndMakeVisible(soloControls[index]);
            soloControls[index].setText("SOLO");
            soloControls[index].setTheme(theme);
            soloControls[index].setAccent(juce::Colour(0xff6c7480));
            soloControls[index].setStateChangeCallback([this, index](bool active)
            {
                if (channelsLinked)
                    for (auto& control : soloControls)
                        control.setToggleState(active, juce::dontSendNotification);
                juce::ignoreUnused(index);
                repaint();
            });

            addAndMakeVisible(faders[index]);
            faders[index].setRange(-60.0f, 12.0f);
            faders[index].setValue(-60.0f + faderValues[index] * 72.0f, juce::dontSendNotification);
            faders[index].setAccent(juce::Colour(0xffd6dde6));
            faders[index].setTheme(theme);
            faders[index].setCompactStyle(true);
                faders[index].setLabel("FADER");
            faders[index].setValueLabelVisible(true);
            faders[index].setValueChangeCallback([this, index](float value)
            {
                if (channelsLinked)
                    for (auto& control : faders)
                        control.setValue(value, juce::dontSendNotification);
                juce::ignoreUnused(index);
                repaint();
            });

            juce::ignoreUnused(labels);
        }
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(18);
        bounds.removeFromTop(30);
        modeControl.setBounds(bounds.removeFromTop(28).removeFromLeft(132).reduced(0, 4));
        bounds.removeFromTop(8);

        auto cards = bounds;
        const auto gap = 8;
        const auto cardWidth = (cards.getWidth() - gap * juce::jmax(0, channelCount - 1)) / channelCount;
        for (int channel = 0; channel < channelCount; ++channel)
        {
            auto card = cards.removeFromLeft(channel + 1 == channelCount ? cards.getWidth() : cardWidth);
            if (channel + 1 < channelCount)
                cards.removeFromLeft(juce::jmin(gap, cards.getWidth()));

            auto content = card.reduced(8);
            content.removeFromTop(22);
            auto meterArea = content.removeFromTop(juce::jmax(150, content.getHeight() / 2));
            meterArea.removeFromBottom(20);
            meters[static_cast<size_t>(channel)].setBounds(meterArea);
            content.removeFromTop(6);
            muteControls[static_cast<size_t>(channel)].setBounds(content.removeFromTop(20).reduced(2, 1));
            soloControls[static_cast<size_t>(channel)].setBounds(content.removeFromTop(20).reduced(2, 1));
            content.removeFromTop(6);
            faders[static_cast<size_t>(channel)].setBounds(content.reduced(4, 0));
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff17191c));
        auto bounds = getLocalBounds().reduced(18);

        g.setColour(juce::Colour(0xfff1f4f7));
        g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        g.drawText(channelCount > 6 ? "7.1 Channel Fader Control" : (channelCount > 2 ? "5.1 Channel Fader Control" : "Stereo Channel Fader Control"),
                   bounds.removeFromTop(30),
                   juce::Justification::centredLeft);

        auto modeRow = bounds.removeFromTop(28);
        modeRow.removeFromLeft(132);

        bounds.removeFromTop(8);
        const std::array<const char*, 8> labels { "L", "R", "C", "LFE", "Ls", "Rs", "Lrs", "Rrs" };
        const std::array<float, 8> peaks { -7.8f, -9.2f, -8.4f, -16.2f, -12.0f, -13.6f, -18.2f, -15.4f };
        const std::array<float, 8> holds { -5.4f, -6.8f, -6.1f, -12.8f, -9.5f, -10.4f, -14.0f, -11.8f };
        const std::array<float, 8> faderValues { 0.64f, 0.64f, 0.60f, 0.52f, 0.58f, 0.58f, 0.55f, 0.55f };

        auto cards = bounds;
        const auto gap = 8;
        const auto cardWidth = (cards.getWidth() - gap * juce::jmax(0, channelCount - 1)) / channelCount;
        for (int channel = 0; channel < channelCount; ++channel)
        {
            auto card = cards.removeFromLeft(channel + 1 == channelCount ? cards.getWidth() : cardWidth);
            if (channel + 1 < channelCount)
                cards.removeFromLeft(juce::jmin(gap, cards.getWidth()));

            drawChannelCard(g,
                            card,
                            labels[static_cast<size_t>(channel)],
                            peaks[static_cast<size_t>(channel)],
                            holds[static_cast<size_t>(channel)],
                            faderValues[static_cast<size_t>(channel)]);
        }
    }

private:
    static void drawModeBadge(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef text, bool active)
    {
        g.setColour(active ? juce::Colour(0xff1f4936) : juce::Colour(0xff111418));
        g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
        g.setColour(active ? juce::Colour(0xff42d96f) : juce::Colour(0xff6c7480));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(text, bounds, juce::Justification::centred);
    }

    static float normalisePeak(float db) noexcept
    {
        return juce::jlimit(0.0f, 1.0f, (db + 54.0f) / 54.0f);
    }

    static void drawSegmentedPeak(juce::Graphics& g, juce::Rectangle<int> meter, float peakDb, float holdDb)
    {
        g.setColour(juce::Colour(0xff101318));
        g.fillRoundedRectangle(meter.toFloat(), 3.0f);

        auto fillArea = meter.reduced(4, 5);
        const auto value = normalisePeak(peakDb);
        const auto yellow = normalisePeak(-18.0f);
        const auto red = normalisePeak(-6.0f);

        const std::array<float, 4> stops { 0.0f, yellow, red, 1.0f };
        const std::array<juce::Colour, 4> colours { juce::Colour(0xff42d96f),
                                                    juce::Colour(0xffe0bf35),
                                                    juce::Colour(0xffe34b4b),
                                                    juce::Colour(0xffe34b4b) };
        std::array<float, 5> edges { stops[0], stops[1], stops[2], stops[3], 1.0f };
        for (size_t i = 0; i < colours.size(); ++i)
        {
            if (value <= edges[i] || edges[i + 1] <= edges[i])
                continue;

            const auto visibleEnd = juce::jmin(value, edges[i + 1]);
            const auto yTop = fillArea.getBottom() - juce::roundToInt(static_cast<float>(fillArea.getHeight()) * visibleEnd);
            const auto yBottom = fillArea.getBottom() - juce::roundToInt(static_cast<float>(fillArea.getHeight()) * edges[i]);
            g.setColour(colours[i]);
            g.fillRect(juce::Rectangle<int>(fillArea.getX(), yTop, fillArea.getWidth(), juce::jmax(1, yBottom - yTop)));
        }

        const auto holdY = meter.getBottom() - juce::roundToInt(static_cast<float>(meter.getHeight()) * normalisePeak(holdDb));
        g.setColour(juce::Colour(0xfff6f8fb));
        g.fillRect(juce::Rectangle<int>(meter.getX() + 3, holdY, meter.getWidth() - 6, 2));
    }

    static void drawFader(juce::Graphics& g, juce::Rectangle<int> bounds, float normalisedValue)
    {
        auto slot = bounds.withWidth(8).withCentre({ bounds.getCentreX(), bounds.getCentreY() }).reduced(0, 12);
        g.setColour(juce::Colour(0xff101318));
        g.fillRoundedRectangle(slot.toFloat(), 4.0f);
        g.setColour(juce::Colour(0xff48515e));
        g.drawRoundedRectangle(slot.toFloat(), 4.0f, 1.0f);

        const auto capY = slot.getBottom() - juce::roundToInt(static_cast<float>(slot.getHeight()) * juce::jlimit(0.0f, 1.0f, normalisedValue));
        auto cap = juce::Rectangle<int>(bounds.getX(), capY - 8, bounds.getWidth(), 18);
        g.setColour(juce::Colour(0xffd7dde6));
        g.fillRoundedRectangle(cap.toFloat(), 4.0f);
        g.setColour(juce::Colour(0xff111418));
        g.drawRoundedRectangle(cap.toFloat(), 4.0f, 1.0f);
    }

    static void drawChannelCard(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef label, float peakDb, float holdDb, float fader)
    {
        juce::ignoreUnused(holdDb, fader);
        g.setColour(juce::Colour(0xff242a32));
        g.fillRoundedRectangle(bounds.toFloat(), 6.0f);
        g.setColour(juce::Colour(0xff3a414c));
        g.drawRoundedRectangle(bounds.toFloat(), 6.0f, 1.0f);

        auto content = bounds.reduced(8);
        g.setColour(juce::Colour(0xfff1f4f7));
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(label, content.removeFromTop(22), juce::Justification::centred);

        auto meterArea = content.removeFromTop(juce::jmax(150, content.getHeight() / 2));
        auto valueBox = meterArea.removeFromBottom(20).reduced(4, 2);
        g.setColour(juce::Colour(0xff111418));
        g.fillRoundedRectangle(valueBox.toFloat(), 3.0f);
        g.setColour(juce::Colour(0xff8de3ff));
        g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
        g.drawText(juce::String(juce::roundToInt(peakDb)), valueBox, juce::Justification::centred);

        content.removeFromTop(6);
        content.removeFromTop(20);
        content.removeFromTop(20);
        content.removeFromTop(6);
        juce::ignoreUnused(meterArea, fader);
    }

    int channelCount = 8;
    bool channelsLinked = true;
    wjn::common::ThemeContext theme;
    wjn::common::ToggleBadgeControl modeControl;
    std::array<wjn::common::SegmentedMeterControl, 8> meters;
    std::array<wjn::common::ToggleBadgeControl, 8> muteControls;
    std::array<wjn::common::ToggleBadgeControl, 8> soloControls;
    std::array<wjn::common::VerticalFaderControl, 8> faders;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelFaderControlComponent)
};

class MixerConsoleView::ChannelFaderControlWindow final : public juce::DocumentWindow
{
public:
    explicit ChannelFaderControlWindow(int channelCount, const wjn::common::ThemeContext& theme)
        : DocumentWindow("MixerPro Channel Fader Control", juce::Colour(0xff17191c), juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        setContentOwned(new ChannelFaderControlComponent(channelCount, theme), true);
        centreWithSize(channelCount > 6 ? 760 : 620, 560);
        setVisible(true);
    }

    void closeButtonPressed() override { setVisible(false); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelFaderControlWindow)
};

class MixerConsoleView::ComponentGalleryComponent final : public juce::Component
{
public:
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff17191c));

        auto bounds = getLocalBounds().reduced(20);
        g.setColour(juce::Colour(0xfff1f4f7));
        g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
        g.drawText("MixerPro Component Gallery", bounds.removeFromTop(32), juce::Justification::centredLeft);

        g.setColour(juce::Colour(0xff8a94a3));
        g.setFont(juce::FontOptions(12.0f));
        g.drawText("Reference sheet for manually drawn UI and future agent implementation.", bounds.removeFromTop(22), juce::Justification::centredLeft);
        bounds.removeFromTop(14);

        auto top = bounds.removeFromTop(210);
        drawBasicControls(g, top.removeFromLeft(300).reduced(0, 0));
        top.removeFromLeft(14);
        drawMeterControls(g, top.removeFromLeft(300));
        top.removeFromLeft(14);
        drawPannerControls(g, top);

        bounds.removeFromTop(18);
        auto middle = bounds.removeFromTop(190);
        drawAuxRowReference(g, middle.removeFromLeft(520));
        middle.removeFromLeft(14);
        drawEqReference(g, middle);

        bounds.removeFromTop(18);
        auto bottom = bounds.removeFromTop(170);
        drawStripReference(g, bottom.removeFromLeft(460));
        bottom.removeFromLeft(14);
        drawColourReference(g, bottom.removeFromLeft(330));
        bottom.removeFromLeft(14);
        drawJuceWidgetReference(g, bottom);
    }

private:
    static void drawPanel(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef title)
    {
        g.setColour(juce::Colour(0xff242a32));
        g.fillRoundedRectangle(bounds.toFloat(), 6.0f);
        g.setColour(juce::Colour(0xff3a414c));
        g.drawRoundedRectangle(bounds.toFloat(), 6.0f, 1.0f);
        g.setColour(juce::Colour(0xfff1f4f7));
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(title, bounds.reduced(12).removeFromTop(22), juce::Justification::centredLeft);
    }

    static void drawKnob(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef label, float value, juce::Colour accent)
    {
        const auto centreX = bounds.getCentreX();
        auto knob = bounds.removeFromTop(48).withSizeKeepingCentre(42, 42).toFloat();
        const auto centre = knob.getCentre();
        const auto angle = juce::jmap(juce::jlimit(0.0f, 1.0f, value), -2.35f, 2.35f);

        g.setColour(juce::Colour(0xff101318));
        g.fillEllipse(knob);
        g.setColour(juce::Colour(0xff4c5664));
        g.drawEllipse(knob, 1.2f);
        g.setColour(accent);
        g.drawLine(centre.x, centre.y,
                   centre.x + std::sin(angle) * 15.0f,
                   centre.y - std::cos(angle) * 15.0f,
                   2.0f);

        g.setColour(juce::Colour(0xffd6dde6));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(label, juce::Rectangle<int>(centreX - 36, bounds.getY(), 72, 14), juce::Justification::centred);
    }

    static void drawBadge(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef text, bool active)
    {
        g.setColour(active ? juce::Colour(0xff1f4936) : juce::Colour(0xff101318));
        g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
        g.setColour(active ? juce::Colour(0xff42d96f) : juce::Colour(0xff8a94a3));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(text, bounds, juce::Justification::centred);
    }

    static void drawButton(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef text, juce::Colour accent)
    {
        g.setColour(juce::Colour(0xff101318));
        g.fillRoundedRectangle(bounds.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff4c5664));
        g.drawRoundedRectangle(bounds.toFloat(), 5.0f, 1.0f);
        g.setColour(accent);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(text, bounds, juce::Justification::centred);
    }

    static void drawFader(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        auto slot = bounds.withWidth(8).withCentre({ bounds.getCentreX(), bounds.getCentreY() }).reduced(0, 12);
        g.setColour(juce::Colour(0xff101318));
        g.fillRoundedRectangle(slot.toFloat(), 4.0f);
        g.setColour(juce::Colour(0xff48515e));
        g.drawRoundedRectangle(slot.toFloat(), 4.0f, 1.0f);
        auto cap = juce::Rectangle<int>(bounds.getX(), slot.getCentreY() - 8, bounds.getWidth(), 18);
        g.setColour(juce::Colour(0xffd7dde6));
        g.fillRoundedRectangle(cap.toFloat(), 4.0f);
    }

    static void drawVerticalMeter(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef label, float value)
    {
        g.setColour(juce::Colour(0xffc9d1da));
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText(label, bounds.removeFromTop(14), juce::Justification::centred);

        auto meter = bounds.withSizeKeepingCentre(18, bounds.getHeight() - 18);
        g.setColour(juce::Colour(0xff101318));
        g.fillRoundedRectangle(meter.toFloat(), 3.0f);
        auto fill = meter.reduced(4, 4);
        fill = fill.withTrimmedTop(juce::roundToInt(static_cast<float>(fill.getHeight()) * (1.0f - value)));
        g.setColour(value > 0.78f ? juce::Colour(0xffe34b4b) : (value > 0.55f ? juce::Colour(0xffe0bf35) : juce::Colour(0xff42d96f)));
        g.fillRoundedRectangle(fill.toFloat(), 2.0f);
    }

    static void drawBasicControls(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        drawPanel(g, bounds, "Basic Controls");
        auto content = bounds.reduced(16).withTrimmedTop(34);
        auto row = content.removeFromTop(74);
        drawKnob(g, row.removeFromLeft(70), "KNOB", 0.62f, juce::Colour(0xff8de3ff));
        drawKnob(g, row.removeFromLeft(70), "AUX", 0.42f, juce::Colour(0xfff0c84b));
        drawKnob(g, row.removeFromLeft(70), "PAN", 0.55f, juce::Colour(0xff8de3ff));

        content.removeFromTop(8);
        auto badges = content.removeFromTop(26);
        drawBadge(g, badges.removeFromLeft(64), "ON", true);
        badges.removeFromLeft(8);
        drawBadge(g, badges.removeFromLeft(64), "PRE", true);
        badges.removeFromLeft(8);
        drawBadge(g, badges.removeFromLeft(64), "MUTE", false);

        content.removeFromTop(10);
        drawButton(g, content.removeFromTop(30).removeFromLeft(180), "OPEN WINDOW", juce::Colour(0xff8de3ff));
        content.removeFromTop(10);
        drawFader(g, content.removeFromLeft(64));
    }

    static void drawMeterControls(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        drawPanel(g, bounds, "Metering");
        auto content = bounds.reduced(16).withTrimmedTop(34);
        drawVerticalMeter(g, content.removeFromLeft(54), "M", 0.42f);
        drawVerticalMeter(g, content.removeFromLeft(54), "PEAK", 0.78f);
        drawVerticalMeter(g, content.removeFromLeft(54), "S", 0.50f);
        auto info = content.reduced(8, 22);
        g.setColour(juce::Colour(0xff101318));
        g.fillRoundedRectangle(info.toFloat(), 4.0f);
        g.setColour(juce::Colour(0xff8de3ff));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText("I -19.2 LUFS", info, juce::Justification::centred);
    }

    static void drawPannerControls(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        drawPanel(g, bounds, "Panners");
        auto content = bounds.reduced(16).withTrimmedTop(34);
        drawKnob(g, content.removeFromLeft(80), "STEREO PAN", 0.54f, juce::Colour(0xff8de3ff));
        auto spatial = content.removeFromLeft(180).withSizeKeepingCentre(170, 58);
        drawButton(g, spatial, "5.1 SPATIAL PANNER", juce::Colour(0xff8de3ff));
        auto meters = content.reduced(8, 0);
        for (auto name : { "L", "C", "R", "Ls", "Rs", "LFE" })
        {
            auto row = meters.removeFromTop(18);
            g.setColour(juce::Colour(0xffc9d1da));
            g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
            g.drawText(name, row.removeFromLeft(30), juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff42d96f));
            g.fillRoundedRectangle(row.reduced(0, 6).withWidth(row.getWidth() / 2).toFloat(), 2.0f);
        }
    }

    static void drawAuxRowReference(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        drawPanel(g, bounds, "Aux Row Layout");
        auto row = bounds.reduced(16).withTrimmedTop(42).removeFromTop(118);
        g.setColour(juce::Colour(0xff20242a));
        g.fillRoundedRectangle(row.toFloat(), 6.0f);
        g.setColour(juce::Colour(0xfff0c84b));
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText("AUX 1 - Stereo Reverb", row.reduced(12).removeFromTop(22), juce::Justification::centredLeft);

        auto content = row.reduced(12).withTrimmedTop(30);
        auto control = content.removeFromLeft(190);
        drawBadge(g, control.removeFromTop(24).removeFromLeft(62), "ON", true);
        drawButton(g, control.removeFromTop(30).removeFromLeft(160), "Target: Stereo Aux", juce::Colour(0xff8de3ff));
        drawKnob(g, content.removeFromLeft(100), "LEVEL", 0.55f, juce::Colour(0xfff0c84b));
        drawKnob(g, content.removeFromLeft(100), "PAN", 0.42f, juce::Colour(0xfff0c84b));
        drawButton(g, content.reduced(8, 22), "Spatial button for surround aux", juce::Colour(0xff8de3ff));
    }

    static void drawEqReference(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        drawPanel(g, bounds, "Parametric EQ");
        auto graph = bounds.reduced(16).withTrimmedTop(42);
        g.setColour(juce::Colour(0xff101318));
        g.fillRoundedRectangle(graph.toFloat(), 5.0f);
        juce::Path curve;
        curve.startNewSubPath(static_cast<float>(graph.getX()), static_cast<float>(graph.getCentreY()));
        curve.cubicTo(static_cast<float>(graph.getX() + graph.getWidth() * 0.25f), static_cast<float>(graph.getCentreY() - 28),
                      static_cast<float>(graph.getX() + graph.getWidth() * 0.50f), static_cast<float>(graph.getCentreY() + 34),
                      static_cast<float>(graph.getX() + graph.getWidth() * 0.70f), static_cast<float>(graph.getCentreY() - 8));
        curve.lineTo(static_cast<float>(graph.getRight()), static_cast<float>(graph.getCentreY() - 22));
        g.setColour(juce::Colour(0xff8de3ff));
        g.strokePath(curve, juce::PathStrokeType(2.5f));
    }

    static void drawStripReference(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        drawPanel(g, bounds, "Channel Strip Parts");
        auto content = bounds.reduced(16).withTrimmedTop(36);
        drawButton(g, content.removeFromLeft(96).removeFromTop(28), "GAIN", juce::Colour(0xff4bb7ff));
        content.removeFromLeft(8);
        drawButton(g, content.removeFromLeft(96).removeFromTop(28), "3-BAND EQ", juce::Colour(0xff8de3ff));
        content.removeFromLeft(8);
        drawButton(g, content.removeFromLeft(96).removeFromTop(28), "AUX SENDS", juce::Colour(0xfff0c84b));
        content.removeFromLeft(8);
        drawButton(g, content.removeFromLeft(96).removeFromTop(28), "METER", juce::Colour(0xff42d96f));
    }

    static void drawColourReference(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        drawPanel(g, bounds, "Colours");
        auto content = bounds.reduced(16).withTrimmedTop(36);
        const std::array<std::pair<const char*, juce::Colour>, 7> colours {{
            { "Panel", juce::Colour(0xff242a32) }, { "Field", juce::Colour(0xff101318) },
            { "Blue", juce::Colour(0xff8de3ff) }, { "Aux", juce::Colour(0xfff0c84b) },
            { "Green", juce::Colour(0xff42d96f) }, { "Yellow", juce::Colour(0xffe0bf35) },
            { "Red", juce::Colour(0xffe34b4b) }
        }};

        for (const auto& item : colours)
        {
            auto cell = content.removeFromLeft(74);
            g.setColour(item.second);
            g.fillRoundedRectangle(cell.removeFromTop(30).reduced(8, 4).toFloat(), 4.0f);
            g.setColour(juce::Colour(0xffc9d1da));
            g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
            g.drawText(item.first, cell.removeFromTop(16), juce::Justification::centred);
        }
    }

    static void drawJuceWidgetReference(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        drawPanel(g, bounds, "JUCE Widget Map");
        auto content = bounds.reduced(16).withTrimmedTop(34);
        const std::array<const char*, 8> widgets {
            "TextButton", "ToggleButton", "Slider", "ComboBox",
            "Label", "TextEditor", "Viewport", "TabbedComponent"
        };

        for (auto* widget : widgets)
        {
            auto cell = content.removeFromTop(22);
            g.setColour(juce::Colour(0xff101318));
            g.fillRoundedRectangle(cell.reduced(0, 3).toFloat(), 3.0f);
            g.setColour(juce::Colour(0xffc9d1da));
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            g.drawText(widget, cell.reduced(8, 3), juce::Justification::centredLeft);
        }
    }
};

class MixerConsoleView::ComponentGalleryWindow final : public juce::DocumentWindow
{
public:
    ComponentGalleryWindow()
        : DocumentWindow("MixerPro Component Gallery", juce::Colour(0xff17191c), juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        setContentOwned(new ComponentGalleryComponent(), true);
        centreWithSize(980, 760);
        setVisible(true);
    }

    void closeButtonPressed() override { setVisible(false); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ComponentGalleryWindow)
};

MixerConsoleView::~MixerConsoleView()
{
    stopTimer();
    localeManager.setChangeCallback(nullptr);

    const auto hide = [](auto& window)
    {
        if (window != nullptr)
            window->setVisible(false);
    };

    hide(componentGalleryWindow);
    hide(channelFaderControlWindow);
    hide(dynamicsSettingsWindow);
    hide(parametricEqSettingsWindow);
    hide(auxSendSettingsWindow);
    hide(spatialPanner71SettingsWindow);
    hide(spatialPanner51SettingsWindow);

    componentGalleryWindow.reset();
    channelFaderControlWindow.reset();
    dynamicsSettingsWindow.reset();
    parametricEqSettingsWindow.reset();
    auxSendSettingsWindow.reset();
    spatialPanner71SettingsWindow.reset();
    spatialPanner51SettingsWindow.reset();
}

MixerConsoleView::MixerConsoleView(CommonJackMixerRuntime& audioRuntimeIn,
                                                                     const wjn::common::ThemeContext& themeIn,
                                                                     wjn::common::LocaleManager& localeManagerIn)
        : audioRuntime(audioRuntimeIn),
            theme(themeIn),
            localeManager(localeManagerIn)
{
    setOpaque(true);
    configureCommonControls();
    refreshLocalizedLabels();
    localeManager.setChangeCallback([this]
    {
        refreshLocalizedLabels();
        repaint();
    });
    updateMetersAndState();
    startTimerHz(30);
}

juce::String MixerConsoleView::localizedText(const juce::String& key, const juce::String& fallback) const
{
    return localeManager.text(key, fallback);
}

void MixerConsoleView::configureCommonControls()
{
    const auto accent = theme.colour("accent");
    const auto meterNormal = theme.colour("meterNormal");
    const auto channelCountForStrip = [](size_t strip)
    {
        return std::array<int, stripCount> { 1, 2, 6, 8, 2, 2, 2, 2 }[strip];
    };
    const std::array<int, 3> mainEqBandForPopupBand { 3, 2, 0 };
    const std::array<double, 4> eqFrequencies { 80.0, 620.0, 2400.0, 10000.0 };
    const std::array<double, 4> eqGains { 2.5, -3.0, 1.5, 3.0 };
    const std::array<double, 4> eqQValues { 0.70, 1.40, 1.10, 0.80 };
    const std::array<double, 2> dynamicsDefaults { -18.0, -42.0 };
    const std::array<double, 3> auxDefaults { -12.0, -9.0, -14.0 };

    for (size_t strip = 0; strip < stripCount; ++strip)
    {
        dynamicsThresholdStates[strip] = dynamicsDefaults;
        auxLevelStates[strip] = auxDefaults;
        for (size_t channel = 0; channel < eqValueStates[strip].size(); ++channel)
        {
            const auto channelOffset = (static_cast<int>(channel) - 2) * 0.35;
            for (size_t band = 0; band < eqValueStates[strip][channel].size(); ++band)
                eqValueStates[strip][channel][band] = {
                    eqFrequencies[band], eqGains[band] + channelOffset, eqQValues[band] };
        }

        addAndMakeVisible(gainControls[strip]);
        addAndMakeVisible(panControls[strip]);
        addAndMakeVisible(muteControls[strip]);
        addAndMakeVisible(soloControls[strip]);
        addAndMakeVisible(eqToggleControls[strip]);
        addAndMakeVisible(auxSetControls[strip]);
        addAndMakeVisible(dynamicsToggleControls[strip]);
        addAndMakeVisible(lowCutControls[strip]);
        addAndMakeVisible(inputRouteControls[strip]);
        addAndMakeVisible(outputRouteControls[strip]);
        addAndMakeVisible(meterControls[strip]);
        addAndMakeVisible(faderControls[strip]);

        for (size_t band = 0; band < eqBandCount; ++band)
        {
            addAndMakeVisible(eqControls[strip][band]);
            addAndMakeVisible(auxControls[strip][band]);
        }
        for (size_t control = 0; control < 2; ++control)
            addAndMakeVisible(dynamicsControls[strip][control]);

        setStripControlsVisible(strip, false);

        const auto initialGain = strip < stereoChannelCount ? audioRuntime.getChannelGainDb(static_cast<int>(strip)) : 0.0f;
        configureStripControl(gainControls[strip], "GAIN", initialGain, accent,
                              [this, strip](double value)
        {
            if (strip < stereoChannelCount)
                audioRuntime.setChannelGainDb(static_cast<int>(strip), static_cast<float>(value));
            repaint();
        });
        gainControls[strip].setRange(-12.0, 12.0);
        gainControls[strip].setValue(initialGain, juce::dontSendNotification);
        gainControls[strip].setSuffix(" dB");

        configureStripControl(panControls[strip], "PAN", 0.0f, accent,
                              [this](double) { repaint(); });
        panControls[strip].setRange(-1.0, 1.0);
        panControls[strip].setValue(0.0, juce::dontSendNotification);
        panControls[strip].setValueTextFormatter(formatStereoPan);

        configureToggleControl(muteControls[strip], "MUTE", false, accent,
                               [this, strip](bool active)
        {
            if (strip < stereoChannelCount)
                audioRuntime.setChannelMute(static_cast<int>(strip), active);
            repaint();
        });
        configureToggleControl(soloControls[strip], "SOLO", strip == 5, accent,
                               [this, strip](bool active)
        {
            if (strip < stereoChannelCount)
                audioRuntime.setChannelSolo(static_cast<int>(strip), active);
            repaint();
        });
        configureToggleControl(eqToggleControls[strip], "EQ", eqEnabled[strip], accent,
                               [this, strip](bool active)
        {
            eqEnabled[strip] = active;
            repaint();
        });
        configureToggleControl(auxSetControls[strip], "SET", false, accent,
                               [this](bool) { repaint(); });
        configureToggleControl(dynamicsToggleControls[strip], "DYN", dynamicsEnabled[strip], accent,
                               [this, strip](bool active)
        {
            dynamicsEnabled[strip] = active;
            repaint();
        });
        configureToggleControl(lowCutControls[strip], "80 Hz LC", false, accent,
                               [this](bool) { repaint(); });

        for (size_t band = 0; band < eqBandCount; ++band)
        {
            const auto label = std::array<juce::String, eqBandCount> { "HI", "MID", "LOW" }[band];
            const auto popupBand = static_cast<size_t>(mainEqBandForPopupBand[band]);
            const auto value = static_cast<float>(eqValueStates[strip][0][popupBand][1]);
            configureStripControl(eqControls[strip][band], label, value, accent,
                                  [this, strip, band, mainEqBandForPopupBand](double newValue)
            {
                const auto popupBandIndex = static_cast<size_t>(mainEqBandForPopupBand[band]);
                for (auto& channel : eqValueStates[strip])
                    channel[popupBandIndex][1] = newValue;
                if (activeParametricEqStrip == static_cast<int>(strip) && parametricEqSettingsWindow != nullptr)
                    parametricEqSettingsWindow->setChannelBandValues(eqValueStates[strip]);
                repaint();
            });
            eqControls[strip][band].setRange(-12.0, 12.0);
            eqControls[strip][band].setValue(value, juce::dontSendNotification);
            eqControls[strip][band].setSuffix(" dB");
            eqControls[strip][band].setContextMenuCallback([this, strip, channelCountForStrip]
            {
                showParametricEqSettings(channelCountForStrip(strip), strip);
            });

            const auto auxLabel = std::array<juce::String, eqBandCount> { "AUX1", "AUX2", "AUX3" }[band];
            const auto auxValue = static_cast<float>(auxLevelStates[strip][band]);
            configureStripControl(auxControls[strip][band], auxLabel, auxValue, accent,
                                  [this, strip, band](double newValue)
            {
                auxLevelStates[strip][band] = newValue;
                if (activeAuxStrip == static_cast<int>(strip) && auxSendSettingsWindow != nullptr)
                    auxSendSettingsWindow->setLevel(static_cast<int>(band), newValue);
                repaint();
            });
            auxControls[strip][band].setRange(-60.0, 12.0);
            auxControls[strip][band].setValue(auxValue, juce::dontSendNotification);
            auxControls[strip][band].setSuffix(" dB");
            auxControls[strip][band].setContextMenuCallback([this, strip]
            {
                showAuxSendSettings(strip);
            });
        }

        const auto dynamicsLabels = std::array<juce::String, 2> { "COMP", "GATE" };
        for (size_t control = 0; control < 2; ++control)
        {
            const auto dynamicsValue = static_cast<float>(dynamicsThresholdStates[strip][control]);
            configureStripControl(dynamicsControls[strip][control], dynamicsLabels[control],
                                  dynamicsValue, accent,
                                  [this, strip, control](double newValue)
            {
                dynamicsThresholdStates[strip][control] = newValue;
                if (activeDynamicsStrip == static_cast<int>(strip) && dynamicsSettingsWindow != nullptr)
                    dynamicsSettingsWindow->setThresholdValues(dynamicsThresholdStates[strip]);
                repaint();
            });
            dynamicsControls[strip][control].setRange(-60.0, 0.0);
            dynamicsControls[strip][control].setValue(dynamicsValue, juce::dontSendNotification);
            dynamicsControls[strip][control].setSuffix(" dB");
            dynamicsControls[strip][control].setContextMenuCallback([this, strip, channelCountForStrip]
            {
                showDynamicsSettings(channelCountForStrip(strip), strip);
            });
        }

        eqToggleControls[strip].setContextMenuCallback([this, strip, channelCountForStrip]
        {
            showParametricEqSettings(channelCountForStrip(strip), strip);
        });
        auxSetControls[strip].setContextMenuCallback([this, strip]
        {
            showAuxSendSettings(strip);
        });
        dynamicsToggleControls[strip].setContextMenuCallback([this, strip, channelCountForStrip]
        {
            showDynamicsSettings(channelCountForStrip(strip), strip);
        });

        inputRouteControls[strip].setAccent(accent);
        inputRouteControls[strip].setTheme(theme);
        outputRouteControls[strip].setAccent(accent);
        outputRouteControls[strip].setTheme(theme);
        inputRouteControls[strip].setSelectionChangeCallback([this](int, const juce::String&)
        {
            repaint();
        });
        outputRouteControls[strip].setSelectionChangeCallback([this](int, const juce::String&)
        {
            repaint();
        });

        meterControls[strip].setAccent(meterNormal);
        meterControls[strip].setTheme(theme);
        meterControls[strip].setChannelCount(channelCountForStrip(strip));
        meterControls[strip].setShowsOutput(meterShowsOutput[strip]);
        meterControls[strip].setSourceChangeCallback([this, strip](bool output)
        {
            meterShowsOutput[strip] = output;
            repaint();
        });

        faderControls[strip].setRange(-60.0f, 12.0f);
        faderControls[strip].setValue(strip == 7 ? audioRuntime.getFaderDb() : -60.0f + 0.62f * 72.0f,
                          juce::dontSendNotification);
        faderControls[strip].setAccent(juce::Colour(0xffd6dde6));
        faderControls[strip].setTheme(theme);
        faderControls[strip].setCompactStyle(true);
        faderControls[strip].setValueChangeCallback([this, strip](float value)
        {
            if (strip == 7)
                audioRuntime.setFaderDb(value);
            repaint();
        });
        faderControls[strip].setContextMenuCallback([this, strip, channelCountForStrip]
        {
            showChannelFaderControl(channelCountForStrip(strip));
        });

        lowCutControls[strip].setContextMenuCallback([this, strip, channelCountForStrip]
        {
            showParametricEqSettings(channelCountForStrip(strip), strip);
        });
    }

    addAndMakeVisible(spatial51Control);
    addAndMakeVisible(spatial71Control);
    spatial51Control.setVisible(false);
    spatial71Control.setVisible(false);
    spatial51Control.setCompactPreview(true);
    spatial51Control.setTheme(theme);
    spatial51Control.setPosition(0.62f, 0.34f);
    spatial51Control.setDoubleClickCallback([this] { showSpatialPannerSettings(false); });
    spatial71Control.setCompactPreview(true);
    spatial71Control.setTheme(theme);
    spatial71Control.setPosition(0.62f, 0.34f);
    spatial71Control.setDoubleClickCallback([this] { showSpatialPannerSettings(true); });

    panControls[7].setRange(-1.0, 1.0);
    panControls[7].setValue(audioRuntime.getPan(), juce::dontSendNotification);
    panControls[7].setValueChangeCallback([this](double value)
    {
        audioRuntime.setPan(static_cast<float>(value));
    });

    configureToggleControl(muteControls[7], "MUTE", false, accent, [this](bool active)
    {
        audioRuntime.setMute(active);
    });
    muteControls[7].setContextMenuCallback([this] { showDynamicsSettings(2, 7); });

    addAndMakeVisible(jackStatusControl);
    jackStatusControl.setAccent(accent);
    jackStatusControl.setTheme(theme);
    jackStatusControl.setStateChangeCallback([this](bool active)
    {
        if (active)
        {
            if (! audioRuntime.start())
                jackStatusControl.setToggleState(false, juce::dontSendNotification);
        }
        else
        {
            audioRuntime.stop();
        }
        updateMetersAndState();
    });

    const auto setMeterPreset = [this](size_t strip, std::array<float, 8> peak,
                                       std::array<float, 8> hold)
    {
        meterControls[strip].setPeakDb(peak);
        meterControls[strip].setHoldDb(hold);
    };
    setMeterPreset(0, { -10.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
                      { -7.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    setMeterPreset(1, { -10.0f, -11.6f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
                      { -7.0f, -8.4f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    setMeterPreset(2, { -7.8f, -9.2f, -11.4f, -16.0f, -13.2f, -14.8f, -18.2f, -17.0f },
                      { -5.4f, -6.8f, -8.2f, -12.0f, -9.8f, -11.1f, -14.0f, -13.2f });
    setMeterPreset(3, { -7.8f, -9.2f, -11.4f, -16.0f, -13.2f, -14.8f, -18.2f, -17.0f },
                      { -5.4f, -6.8f, -8.2f, -12.0f, -9.8f, -11.1f, -14.0f, -13.2f });
    setMeterPreset(4, { -10.0f, -11.6f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
                      { -7.0f, -8.4f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    setMeterPreset(5, { -10.0f, -11.6f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
                      { -7.0f, -8.4f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    setMeterPreset(7, { -5.8f, -6.6f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
                      { -3.2f, -4.1f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
}

void MixerConsoleView::configureStripControl(wjn::common::RotaryControl& control,
                                              juce::String label,
                                              float value,
                                              juce::Colour accent,
                                              std::function<void(double)> callback)
{
    control.setRange(0.0, 1.0);
    control.setValue(value, juce::dontSendNotification);
    control.setLabel(std::move(label));
    control.setAccent(accent);
    control.setTheme(theme);
    control.setCompactStyle(true);
    control.setValueChangeCallback(std::move(callback));
}

void MixerConsoleView::configureToggleControl(wjn::common::ToggleBadgeControl& control,
                                               juce::String label,
                                               bool active,
                                               juce::Colour accent,
                                               std::function<void(bool)> callback)
{
    control.setText(std::move(label));
    control.setToggleState(active, juce::dontSendNotification);
    control.setAccent(accent);
    control.setTheme(theme);
    control.setStateChangeCallback(std::move(callback));
}

void MixerConsoleView::setStripControlsVisible(size_t strip, bool visible)
{
    if (strip >= stripCount)
        return;

    gainControls[strip].setVisible(visible);
    panControls[strip].setVisible(visible);
    muteControls[strip].setVisible(visible);
    soloControls[strip].setVisible(visible);
    eqToggleControls[strip].setVisible(visible);
    auxSetControls[strip].setVisible(visible);
    dynamicsToggleControls[strip].setVisible(visible);
    lowCutControls[strip].setVisible(visible);
    inputRouteControls[strip].setVisible(visible);
    outputRouteControls[strip].setVisible(visible);
    meterControls[strip].setVisible(visible);
    faderControls[strip].setVisible(visible);
    for (size_t band = 0; band < eqBandCount; ++band)
    {
        eqControls[strip][band].setVisible(visible);
        auxControls[strip][band].setVisible(visible);
    }
    for (auto& control : dynamicsControls[strip])
        control.setVisible(visible);
}

void MixerConsoleView::layoutStandardStripControls(size_t strip,
                                                    juce::Rectangle<int> bounds,
                                                    bool hasInputSelector,
                                                    bool hasAuxSends,
                                                    bool spatial,
                                                    juce::StringRef panMode)
{
    if (strip >= stripCount)
        return;

    setStripControlsVisible(strip, true);
    lowCutControls[strip].setVisible(false);
    inputRouteControls[strip].setVisible(hasInputSelector);
    for (auto& control : auxControls[strip])
        control.setVisible(hasAuxSends);
    auxSetControls[strip].setVisible(hasAuxSends);
    panControls[strip].setVisible(! spatial);

    auto content = bounds.reduced(10);
    content.removeFromTop(28);
    if (hasInputSelector)
    {
        auto inputRow = content.removeFromTop(28);
        inputRouteControls[strip].setBounds(inputRow.withSizeKeepingCentre(132, inputRow.getHeight()).reduced(2, 2));
        content.removeFromTop(3);
        content.removeFromTop(12);
    }

    auto topRow = content.removeFromTop(60);
    auto topControls = topRow.withSizeKeepingCentre(126, 52);
    gainControls[strip].setBounds(topControls.removeFromLeft(44));
    auto toggles = topControls;
    muteControls[strip].setBounds(toggles.removeFromTop(20).reduced(4, 1));
    soloControls[strip].setBounds(toggles.removeFromTop(20).reduced(4, 1));
    content.removeFromTop(12);

    auto eqRow = content.removeFromTop(68);
    auto eqKnobs = eqRow.withTrimmedBottom(10).reduced(3, 0);
    const auto eqCell = eqKnobs.getWidth() / static_cast<int>(eqBandCount);
    for (size_t band = 0; band < eqBandCount; ++band)
        eqControls[strip][band].setBounds(eqKnobs.removeFromLeft(eqCell).reduced(4, 0));
    auto eqButton = eqRow.removeFromBottom(14).withLeft(eqRow.getRight() - 34).reduced(1, 1);
    eqToggleControls[strip].setBounds(eqButton);
    content.removeFromTop(12);

    auto panArea = content.removeFromTop(68).reduced(2, 5);
    if (spatial)
    {
        if (juce::String(panMode) == "5.1")
        {
            spatial51Control.setBounds(panArea);
            spatial51Control.setVisible(true);
        }
        else
        {
            spatial71Control.setBounds(panArea);
            spatial71Control.setVisible(true);
        }
    }
    else
    {
        panControls[strip].setBounds(panArea.withSizeKeepingCentre(50, 50));
    }
    content.removeFromTop(12);

    auto sendRow = content.removeFromTop(68);
    auto auxKnobs = sendRow.withTrimmedBottom(10).reduced(3, 0);
    const auto auxCell = auxKnobs.getWidth() / static_cast<int>(eqBandCount);
    for (size_t band = 0; band < eqBandCount; ++band)
        auxControls[strip][band].setBounds(auxKnobs.removeFromLeft(auxCell).reduced(4, 0));
    auto setButton = sendRow.removeFromBottom(14).withLeft(sendRow.getRight() - 38).reduced(1, 1);
    auxSetControls[strip].setBounds(setButton);
    content.removeFromTop(12);

    auto dynamicsRow = content.removeFromTop(70);
    auto dynamicsKnobs = dynamicsRow.withTrimmedBottom(14).reduced(6, 1);
    dynamicsControls[strip][0].setBounds(dynamicsKnobs.removeFromLeft(dynamicsKnobs.getWidth() / 2).reduced(2, 0));
    dynamicsControls[strip][1].setBounds(dynamicsKnobs.reduced(2, 0));
    auto dynamicsToggle = dynamicsRow.removeFromBottom(14).withLeft(dynamicsRow.getRight() - 38).reduced(1, 1);
    dynamicsToggleControls[strip].setBounds(dynamicsToggle);
    content.removeFromTop(12);

    auto lower = content;
    auto outputRow = lower.removeFromBottom(30);
    outputRouteControls[strip].setBounds(outputRow.reduced(2, 2));
    meterControls[strip].setBounds(lower.removeFromRight(68));
    lower.removeFromRight(4);
    faderControls[strip].setBounds(lower.reduced(8, 2));
}

void MixerConsoleView::layoutMasterControls(juce::Rectangle<int> bounds)
{
    setStripControlsVisible(7, true);
    gainControls[7].setVisible(false);
    soloControls[7].setVisible(false);
    eqToggleControls[7].setVisible(false);
    auxSetControls[7].setVisible(false);
    for (auto& control : auxControls[7])
        control.setVisible(false);
    inputRouteControls[7].setVisible(false);
    spatial51Control.setVisible(false);
    spatial71Control.setVisible(false);

    auto content = bounds.reduced(12);
    content.removeFromTop(24);
    muteControls[7].setBounds(content.removeFromTop(20).withSizeKeepingCentre(66, 18));
    content.removeFromTop(2);
    content.removeFromTop(10);

    auto eqRow = content.removeFromTop(74);
    auto eqKnobs = eqRow.withSizeKeepingCentre(juce::jmin(168, eqRow.getWidth()), eqRow.getHeight()).reduced(3, 0);
    const auto masterEqCell = eqKnobs.getWidth() / static_cast<int>(eqBandCount);
    for (size_t band = 0; band < eqBandCount; ++band)
        eqControls[7][band].setBounds(eqKnobs.removeFromLeft(masterEqCell).reduced(5, 3));
    content.removeFromTop(14);

    lowCutControls[7].setBounds(content.removeFromTop(24).reduced(16, 2));
    content.removeFromTop(14);
    panControls[7].setBounds(content.removeFromTop(62).withSizeKeepingCentre(58, 58));
    content.removeFromTop(14);

    auto dynamicsRow = content.removeFromTop(70);
    auto dynamicsKnobs = dynamicsRow.withTrimmedBottom(14).reduced(6, 1);
    dynamicsControls[7][0].setBounds(dynamicsKnobs.removeFromLeft(dynamicsKnobs.getWidth() / 2).reduced(2, 0));
    dynamicsControls[7][1].setBounds(dynamicsKnobs.reduced(2, 0));
    dynamicsToggleControls[7].setBounds(
        dynamicsRow.removeFromBottom(14).withLeft(dynamicsRow.getRight() - 38).reduced(1, 1));
    content.removeFromTop(14);

    auto lower = content;
    auto outputRow = lower.removeFromBottom(30);
    meterControls[7].setBounds(lower.removeFromRight(86));
    lower.removeFromRight(8);
    faderControls[7].setBounds(lower.reduced(8, 0));
    outputRouteControls[7].setBounds(outputRow.reduced(14, 2));
}

void MixerConsoleView::refreshLocalizedLabels()
{
    const auto gain = localizedText("mixer.control.gain", "GAIN");
    const auto mute = localizedText("mixer.control.mute", "MUTE");
    const auto solo = localizedText("mixer.control.solo", "SOLO");
    const auto input = localizedText("mixer.route.input", "IN");
    const auto output = localizedText("mixer.route.output", "OUT");

    const std::array<juce::String, 8> inputValues {
        "JACK mic_1", "JACK mic_1-2", "JACK mic_5.1", "JACK bed_7.1",
        {}, {}, {}, {}
    };
    const std::array<juce::String, 8> outputValues {
        "Master", "Submix A", "Submix A", "Submix B",
        "Submix A", "Master", {}, "ASIO Out 1-2"
    };

    for (size_t strip = 0; strip < stripCount; ++strip)
    {
        gainControls[strip].setLabel(strip == 7 ? gain : gain + " " + juce::String(static_cast<int>(strip + 1)));
        muteControls[strip].setText(mute);
        soloControls[strip].setText(solo);
        outputRouteControls[strip].setLabel(output);

        if (strip < 4)
        {
            juce::StringArray stripInputOptions;
            stripInputOptions.add(inputValues[strip]);
            inputRouteControls[strip].setLabel(input);
            inputRouteControls[strip].setOptions(stripInputOptions);
            inputRouteControls[strip].setSelectedIndex(0);
        }

        juce::StringArray stripOutputOptions;
        stripOutputOptions.add(outputValues[strip]);
        outputRouteControls[strip].setOptions(stripOutputOptions);
        outputRouteControls[strip].setSelectedIndex(0);
    }

    panControls[7].setLabel(localizedText("mixer.control.pan", "PAN"));
    jackStatusControl.setText("JACK");
}

void MixerConsoleView::updateMetersAndState()
{
    const auto inputMeter = audioRuntime.getInputMeter();
    const auto outputMeter = audioRuntime.getOutputMeter();

    if (audioRuntime.isRunning())
    {
        const auto toDb = [](const auto& values)
        {
            std::array<float, 8> result {};
            for (size_t index = 0; index < result.size(); ++index)
                result[index] = values[index] > 0.000001f ? juce::jmax(-60.0f, 20.0f * std::log10(values[index])) : -60.0f;
            return result;
        };
        const auto inputPeak = toDb(inputMeter.peak);
        const auto inputHold = toDb(inputMeter.peakHold);
        const auto outputPeak = toDb(outputMeter.peak);
        const auto outputHold = toDb(outputMeter.peakHold);
        for (size_t strip = 0; strip < stripCount; ++strip)
        {
            meterControls[strip].setPeakDb(meterShowsOutput[strip] ? outputPeak : inputPeak);
            meterControls[strip].setHoldDb(meterShowsOutput[strip] ? outputHold : inputHold);
            meterControls[strip].setOverload(meterShowsOutput[strip] ? outputMeter.overload : inputMeter.overload);
        }

        for (size_t channel = 0; channel < stereoChannelCount; ++channel)
        {
            const auto index = static_cast<int>(channel);
            gainControls[channel].setValue(audioRuntime.getChannelGainDb(index),
                                           juce::dontSendNotification);
            muteControls[channel].setToggleState(audioRuntime.isChannelMuted(index), juce::dontSendNotification);
            soloControls[channel].setToggleState(audioRuntime.isChannelSolo(index), juce::dontSendNotification);
        }
    }

    faderControls[7].setValue(audioRuntime.getFaderDb(), juce::dontSendNotification);
    panControls[7].setValue(audioRuntime.getPan(), juce::dontSendNotification);
    muteControls[7].setToggleState(audioRuntime.isMuted(), juce::dontSendNotification);

    const auto running = audioRuntime.isRunning();
    jackStatusControl.setToggleState(running, juce::dontSendNotification);
    if (running)
        jackStatusText = localizedText("mixer.status.running", "JACK running");
    else if (audioRuntime.isConnected())
        jackStatusText = localizedText("mixer.status.connected", "JACK connected");
    else
    {
        jackStatusText = localizedText("mixer.status.disconnected", "JACK disconnected");
        const auto error = audioRuntime.getLastError();
        if (error.isNotEmpty())
            jackStatusText = localizedText("mixer.status.lastError", "JACK error: {error}")
                .replace("{error}", error);
    }
    repaint();
}

void MixerConsoleView::timerCallback()
{
    updateMetersAndState();
}

void MixerConsoleView::paint(juce::Graphics& g)
{
    standardStripPaintIndex = 0;
    auto bounds = getLocalBounds();
    g.fillAll(theme.colour("darkCanvas"));

    auto header = bounds.removeFromTop(72);
    g.setColour(theme.colour("rackPanel"));
    g.fillRect(header);

    auto headerContent = getLocalBounds().reduced(24).removeFromTop(64);
    g.setColour(theme.colour("primaryText"));
    g.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    g.drawText(localizedText("mixer.title", "MixerPro"),
               headerContent.removeFromLeft(240), juce::Justification::centredLeft);
    g.setColour(theme.colour("secondaryText"));
    g.setFont(juce::FontOptions(13.0f));
    g.drawText(localizedText("mixer.subtitle", "JACK2 mixer console with Common audio runtime"),
               headerContent.withTrimmedRight(150), juce::Justification::centredLeft);

    auto console = getLocalBounds().reduced(24).withTrimmedTop(80);
    g.setColour(theme.colour("rackPanel"));
    g.fillRoundedRectangle(console.toFloat(), 6.0f);
    g.setColour(theme.colour("darkCanvas"));
    g.drawRoundedRectangle(console.toFloat(), 6.0f, 1.0f);

    constexpr int stripWidth = 154;
    auto channelLane = console.reduced(28);
    auto masterColumn = console.removeFromRight(186);
    auto masterArea = masterColumn.withWidth(stripWidth)
                                  .withRight(masterColumn.getRight() - 12)
                                  .withY(channelLane.getY())
                                  .withHeight(channelLane.getHeight());
    paintMasterStrip(g, masterArea);

    auto examples = channelLane;
    paintInputStripExample(g, examples.removeFromLeft(stripWidth).reduced(4, 0),
                           "INPUT MONO", "MONO", "JACK mic_1", "Master", false);
    paintInputStripExample(g, examples.removeFromLeft(stripWidth).reduced(4, 0),
                           "INPUT ST", "STEREO", "JACK mic_1-2", "Submix A", false);
    paintInputStripExample(g, examples.removeFromLeft(stripWidth).reduced(4, 0),
                           "INPUT 5.1", "5.1", "JACK mic_5.1", "Submix A", true);
    paintInputStripExample(g, examples.removeFromLeft(stripWidth).reduced(4, 0),
                           "INPUT 7.1", "7.1", "JACK bed_7.1", "Submix B", true);
    paintAuxStripExample(g, examples.removeFromLeft(stripWidth).reduced(4, 0));
    paintSubmixStripExample(g, examples.removeFromLeft(stripWidth).reduced(4, 0));

}

void MixerConsoleView::resized()
{
    auto bounds = getLocalBounds().reduced(24);
    auto header = bounds.removeFromTop(72);
    jackStatusControl.setBounds(header.removeFromRight(92).withY(header.getY() + 25).withHeight(24));
    bounds.removeFromTop(8);

    auto console = bounds;
    auto channelLane = console.reduced(28);
    auto masterColumn = console.removeFromRight(186);
    auto masterArea = masterColumn.withWidth(154)
                                  .withRight(masterColumn.getRight() - 12)
                                  .withY(channelLane.getY())
                                  .withHeight(channelLane.getHeight());
    layoutMasterControls(masterArea);

    spatial51Control.setVisible(false);
    spatial71Control.setVisible(false);
    auto examples = channelLane;
    for (size_t strip = 0; strip < 6; ++strip)
    {
        const auto stripBounds = examples.removeFromLeft(154).reduced(4, 0);
        const auto spatial = strip == 2 || strip == 3;
        const auto titleMode = strip == 0 ? "MONO" : (strip == 1 ? "STEREO" : (strip == 2 ? "5.1" : (strip == 3 ? "7.1" : "STEREO")));
        layoutStandardStripControls(strip, stripBounds, strip < 4, true, spatial, titleMode);
    }
}

void MixerConsoleView::mouseDown(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
}

void MixerConsoleView::mouseDrag(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
}

void MixerConsoleView::mouseUp(const juce::MouseEvent&)
{
}

void MixerConsoleView::enforceMeterThresholdOrder()
{
    constexpr float minimumGapDb = 1.0f;
    const auto peakScale = getPeakScale();

    peakMeterThresholds.yellowDbfs = juce::jlimit(peakScale.minimum, peakScale.maximum - minimumGapDb, peakMeterThresholds.yellowDbfs);
    peakMeterThresholds.redDbfs = juce::jlimit(peakMeterThresholds.yellowDbfs + minimumGapDb, peakScale.maximum, peakMeterThresholds.redDbfs);

    rmsMeterThresholds.greenDbfs = juce::jlimit(peakScale.minimum, peakScale.maximum - minimumGapDb * 2.0f, rmsMeterThresholds.greenDbfs);
    rmsMeterThresholds.yellowDbfs = juce::jlimit(rmsMeterThresholds.greenDbfs + minimumGapDb, peakScale.maximum - minimumGapDb, rmsMeterThresholds.yellowDbfs);
    rmsMeterThresholds.redDbfs = juce::jlimit(rmsMeterThresholds.yellowDbfs + minimumGapDb, peakScale.maximum, rmsMeterThresholds.redDbfs);
}

MixerConsoleView::PeakMeterThresholds MixerConsoleView::getPeakMeterThresholds() const noexcept
{
    return peakMeterThresholds;
}

MixerConsoleView::RmsMeterThresholds MixerConsoleView::getRmsMeterThresholds() const noexcept
{
    return rmsMeterThresholds;
}

MixerConsoleView::MeterScale MixerConsoleView::getPeakScale() const
{
    return { "Digital Peak", "dBFS", -60.0f, 0.0f, { -60.0f, -48.0f, -36.0f, -24.0f, -12.0f, -6.0f, 0.0f }, 7 };
}

MixerConsoleView::MeterScale MixerConsoleView::getLoudnessScale() const
{
    switch (loudnessScalePreset)
    {
        case LoudnessScalePreset::ebuR128:
            return { "EBU R128", "LUFS", -54.0f, 0.0f, { -54.0f, -42.0f, -36.0f, -30.0f, -23.0f, -18.0f, -9.0f }, 7 };

        case LoudnessScalePreset::atscA85:
            return { "ATSC A/85", "LKFS", -48.0f, 0.0f, { -48.0f, -36.0f, -30.0f, -24.0f, -20.0f, -12.0f, 0.0f }, 7 };

        case LoudnessScalePreset::kSystem20:
            return { "K-System K-20", "dB", -40.0f, 0.0f, { -40.0f, -30.0f, -24.0f, -20.0f, -14.0f, -7.0f, 0.0f }, 7 };
    }

    return getPeakScale();
}

float MixerConsoleView::scaleValueToNormalised(float value, const MeterScale& scale) const noexcept
{
    return juce::jlimit(0.0f, 1.0f, (value - scale.minimum) / (scale.maximum - scale.minimum));
}

juce::String MixerConsoleView::getLoudnessPresetName() const
{
    return getLoudnessScale().name;
}

void MixerConsoleView::paintMasterStrip(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour(0xff2a3038));
    g.fillRoundedRectangle(bounds.toFloat(), 5.0f);
    g.setColour(juce::Colour(0xff3a414c));
    g.drawRoundedRectangle(bounds.toFloat(), 5.0f, 1.0f);

    auto content = bounds.reduced(12);
    g.setColour(juce::Colour(0xfff1f4f7));
    g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    g.drawText("STEREO MASTER", content.removeFromTop(24), juce::Justification::centred);

    content.removeFromTop(20);
    content.removeFromTop(2);

    content.removeFromTop(10);
    auto eqRow = content.removeFromTop(74);
    auto masterEqKnobs = eqRow.withSizeKeepingCentre(juce::jmin(168, eqRow.getWidth()), eqRow.getHeight()).reduced(3, 0);
    eqSectionBounds[7] = masterEqKnobs;
    const auto masterEqCell = masterEqKnobs.getWidth() / 3;
    masterEqKnobs.removeFromLeft(masterEqCell);
    masterEqKnobs.removeFromLeft(masterEqCell);
    masterEqKnobs.removeFromLeft(masterEqCell);
    addSectionGap(g, content, 14);

    content.removeFromTop(24);
    addSectionGap(g, content, 14);

    content.removeFromTop(62);
    addSectionGap(g, content, 14);

    auto dynamicsRow = content.removeFromTop(70);
    auto dynamicsKnobs = dynamicsRow.withTrimmedBottom(14).reduced(6, 1);
    dynamicsBounds[7] = dynamicsKnobs;
    dynamicsKnobs.removeFromLeft(dynamicsKnobs.getWidth() / 2);
    dynamicsKnobs.removeFromRight(dynamicsKnobs.getWidth());
    dynamicsToggleBounds[7] = dynamicsRow.removeFromBottom(14).withLeft(dynamicsRow.getRight() - 38).reduced(1, 1);
    dynamicsRow.removeFromBottom(14);
    addSectionGap(g, content, 14);

    auto lower = content;
    auto masterOutputRow = lower.removeFromBottom(30);
    g.setColour(juce::Colour(0xff15181d));
    g.drawHorizontalLine(masterOutputRow.getY(), static_cast<float>(masterOutputRow.getX()), static_cast<float>(masterOutputRow.getRight()));
    auto meterCluster = lower.removeFromRight(86);
    meterSectionBounds[7] = meterCluster;
    auto peakDb = std::array<float, 8> { -5.8f, -6.6f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    auto holdDb = std::array<float, 8> { -3.2f, -4.1f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    auto overload = false;
    if (audioRuntime.isRunning())
    {
        const auto outputMeter = audioRuntime.getOutputMeter();
        peakDb = meterValuesToDb(outputMeter.peak, outputMeter.channelCount);
        holdDb = meterValuesToDb(outputMeter.peakHold, outputMeter.channelCount);
        overload = outputMeter.overload;
    }
    juce::ignoreUnused(meterCluster, peakDb, holdDb, overload);

    lower.removeFromRight(8);
    masterFaderBounds = lower.reduced(8, 0);
    juce::ignoreUnused(masterFaderBounds, masterOutputRow);
}

void MixerConsoleView::paintInputStripExample(juce::Graphics& g,
                                              juce::Rectangle<int> bounds,
                                              juce::StringRef title,
                                              juce::StringRef layout,
                                              juce::StringRef inputPort,
                                              juce::StringRef outputTarget,
                                              bool spatial)
{
    paintStandardChannelStrip(g,
                              bounds,
                              title,
                              juce::Colour(0xff4bb7ff),
                              inputPort,
                              outputTarget,
                              layout,
                              true,
                              true,
                              spatial,
                              0.58f,
                              0.50f);
}

void MixerConsoleView::paintAuxStripExample(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    paintStandardChannelStrip(g,
                              bounds,
                              "AUX SEND 1",
                              juce::Colour(0xfff0c84b),
                              {},
                              "Submix A",
                              "STEREO",
                              false,
                              true,
                              false,
                              0.52f,
                              0.56f);
}

void MixerConsoleView::paintSubmixStripExample(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    paintStandardChannelStrip(g,
                              bounds,
                              "SUBMIX A",
                              juce::Colour(0xff9bdf73),
                              {},
                              "Master",
                              "STEREO",
                              false,
                              true,
                              false,
                              0.55f,
                              0.72f);
}

void MixerConsoleView::paintStandardChannelStrip(juce::Graphics& g,
                                                 juce::Rectangle<int> bounds,
                                                 juce::StringRef title,
                                                 juce::Colour accent,
                                                 juce::StringRef inputPort,
                                                 juce::StringRef outputTarget,
                                                 juce::StringRef panMode,
                                                 bool hasInputSelector,
                                                 bool hasAuxSends,
                                                 bool spatial,
                                                 float gainValue,
                                                 float panValue)
{
    juce::ignoreUnused(inputPort, outputTarget, gainValue, panValue);
    paintStripShell(g, bounds, title, accent);
    const auto titleText = juce::String(title);
    const auto panModeText = juce::String(panMode);
    const auto isInput51 = titleText == "INPUT 5.1";
    const auto stripIndex = standardStripPaintIndex++;

    auto content = bounds.reduced(10);

    content.removeFromTop(28);
    if (hasInputSelector)
    {
        content.removeFromTop(28);
        content.removeFromTop(3);
        addSectionGap(g, content, 12);
    }

    auto topRow = content.removeFromTop(60);
    auto topControls = topRow.withSizeKeepingCentre(126, 52);
    topControls.removeFromLeft(44);
    auto toggles = topControls;
    toggles.removeFromTop(20);
    toggles.removeFromTop(20);
    addSectionGap(g, content, 12);

    auto eqRow = content.removeFromTop(68);
    if (isInput51)
        parametricEqBounds = eqRow;

    auto eqKnobs = eqRow.withTrimmedBottom(10).reduced(3, 0);
    if (static_cast<size_t>(stripIndex) < eqSectionBounds.size())
        eqSectionBounds[static_cast<size_t>(stripIndex)] = eqKnobs;

    const auto eqCell = eqKnobs.getWidth() / 3;
    eqKnobs.removeFromLeft(eqCell);
    eqKnobs.removeFromLeft(eqCell);
    eqKnobs.removeFromLeft(eqCell);
    auto eqButton = eqRow.removeFromBottom(14).withLeft(eqRow.getRight() - 34).reduced(1, 1);
    if (static_cast<size_t>(stripIndex) < eqToggleBounds.size())
    {
        eqToggleBounds[static_cast<size_t>(stripIndex)] = eqButton;
        juce::ignoreUnused(eqButton);
    }
    addSectionGap(g, content, 12);

    auto panArea = content.removeFromTop(68).reduced(2, 5);
    if (spatial)
    {
        if (panModeText == "5.1")
            spatialPannerBounds[0] = panArea;
        else if (panModeText == "7.1")
            spatialPannerBounds[1] = panArea;

        juce::ignoreUnused(panArea);
    }
    else
    {
        juce::ignoreUnused(panArea);
    }
    addSectionGap(g, content, 12);

    auto sendRow = content.removeFromTop(68);
    if (hasAuxSends)
    {
        if (static_cast<size_t>(stripIndex) < auxSectionBounds.size())
            auxSectionBounds[static_cast<size_t>(stripIndex)] = sendRow;

        if (isInput51)
            auxSendBounds = sendRow;

        auto auxKnobs = sendRow.withTrimmedBottom(10).reduced(3, 0);
        const auto auxCell = auxKnobs.getWidth() / 3;
        auxKnobs.removeFromLeft(auxCell);
        auxKnobs.removeFromLeft(auxCell);
        auxKnobs.removeFromLeft(auxCell);
        auto setButton = sendRow.removeFromBottom(14).withLeft(sendRow.getRight() - 38).reduced(1, 1);
        juce::ignoreUnused(setButton);
    }
    addSectionGap(g, content, 12);

    auto dynamicsRow = content.removeFromTop(70);
    auto dynamicsKnobs = dynamicsRow.withTrimmedBottom(14).reduced(6, 1);
    if (static_cast<size_t>(stripIndex) < dynamicsBounds.size())
        dynamicsBounds[static_cast<size_t>(stripIndex)] = dynamicsKnobs;
    dynamicsKnobs.removeFromLeft(dynamicsKnobs.getWidth() / 2);
    dynamicsKnobs.removeFromRight(dynamicsKnobs.getWidth());
    if (static_cast<size_t>(stripIndex) < dynamicsToggleBounds.size())
    {
        dynamicsToggleBounds[static_cast<size_t>(stripIndex)] = dynamicsRow.removeFromBottom(14).withLeft(dynamicsRow.getRight() - 38).reduced(1, 1);
        juce::ignoreUnused(dynamicsToggleBounds[static_cast<size_t>(stripIndex)]);
    }
    addSectionGap(g, content, 12);

    auto lower = content;
    auto outputRow = lower.removeFromBottom(30);
    g.setColour(juce::Colour(0xff15181d));
    g.drawHorizontalLine(outputRow.getY(), static_cast<float>(outputRow.getX()), static_cast<float>(outputRow.getRight()));
    const auto visibleMeterChannels = panModeText == "MONO" ? 1 : (panModeText == "5.1" ? 6 : (panModeText == "7.1" ? 8 : 2));
    auto meterBounds = lower.removeFromRight(68);

    if (static_cast<size_t>(stripIndex) < meterSectionBounds.size())
        meterSectionBounds[static_cast<size_t>(stripIndex)] = meterBounds;

    auto peakDb = spatial
        ? std::array<float, 8> { -7.8f, -9.2f, -11.4f, -16.0f, -13.2f, -14.8f, -18.2f, -17.0f }
        : std::array<float, 8> { -10.0f, -11.6f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    auto holdDb = spatial
        ? std::array<float, 8> { -5.4f, -6.8f, -8.2f, -12.0f, -9.8f, -11.1f, -14.0f, -13.2f }
        : std::array<float, 8> { -7.0f, -8.4f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    auto overload = false;
    if (audioRuntime.isRunning())
    {
        const auto meter = meterShowsOutput[static_cast<size_t>(stripIndex)]
            ? audioRuntime.getOutputMeter()
            : audioRuntime.getInputMeter();
        peakDb = meterValuesToDb(meter.peak, meter.channelCount);
        holdDb = meterValuesToDb(meter.peakHold, meter.channelCount);
        overload = meter.overload;
    }
    juce::ignoreUnused(meterBounds, peakDb, holdDb, visibleMeterChannels, overload);

    lower.removeFromRight(4);
    auto faderBounds = lower.reduced(8, 2);
    const auto faderChannelCount = panModeText == "MONO" ? 1 : (panModeText == "5.1" ? 6 : (panModeText == "7.1" ? 8 : 2));
    if (faderChannelCount >= 2 && static_cast<size_t>(stripIndex) < faderSectionBounds.size())
    {
        faderSectionBounds[static_cast<size_t>(stripIndex)] = faderBounds;
        faderSectionChannelCounts[static_cast<size_t>(stripIndex)] = faderChannelCount;
    }

    juce::ignoreUnused(faderBounds, outputRow);
}

void MixerConsoleView::paintStripShell(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef title, juce::Colour accent)
{
    g.setColour(juce::Colour(0xff2a3038));
    g.fillRoundedRectangle(bounds.toFloat(), 5.0f);
    g.setColour(juce::Colour(0xff3a414c));
    g.drawRoundedRectangle(bounds.toFloat(), 5.0f, 1.0f);

    auto header = bounds.reduced(10).removeFromTop(24);
    g.setColour(accent);
    g.fillRoundedRectangle(header.withHeight(3).withY(header.getBottom() - 3).toFloat(), 2.0f);
    g.setColour(juce::Colour(0xfff1f4f7));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText(title, header, juce::Justification::centred);
}

void MixerConsoleView::paintTinyMeter(juce::Graphics& g, juce::Rectangle<int> bounds, float peakDb, float rmsDb)
{
    if (bounds.getWidth() < 48 || bounds.getHeight() < 72)
        return;

    auto header = bounds.removeFromTop(12);
    g.setColour(juce::Colour(0xffc9d1da));
    g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
    g.drawText("M  P  S", header, juce::Justification::centred);

    auto integrated = bounds.removeFromBottom(18);
    auto meters = bounds.reduced(2, 2);
    auto momentary = meters.removeFromLeft(20);
    auto peak = meters.removeFromLeft(34);
    auto shortTerm = meters;

    const auto peakScale = getPeakScale();
    const auto loudnessScale = getLoudnessScale();
    const auto rmsValue = scaleValueToNormalised(rmsDb, loudnessScale);
    const auto shortValue = scaleValueToNormalised(rmsDb + 2.0f, loudnessScale);
    const auto peakValue = scaleValueToNormalised(peakDb, peakScale);

    auto paintCompactLoudness = [&](juce::Rectangle<int> area, float value)
    {
        auto meter = area.withSizeKeepingCentre(8, area.getHeight()).reduced(0, 3);
        g.setColour(juce::Colour(0xff0d1014));
        g.fillRoundedRectangle(meter.toFloat(), 2.0f);
        paintSegmentedBar(g,
                          meter.reduced(2, 3),
                          value,
                          { 0.0f,
                            scaleValueToNormalised(rmsMeterThresholds.greenDbfs, peakScale),
                            scaleValueToNormalised(rmsMeterThresholds.yellowDbfs, peakScale),
                            scaleValueToNormalised(rmsMeterThresholds.redDbfs, peakScale) },
                          { juce::Colour(0xff163a78),
                            juce::Colour(0xff27bd63),
                            juce::Colour(0xffe0bf35),
                            juce::Colour(0xffe34b4b) });
    };

    paintCompactLoudness(momentary, rmsValue);

    auto peakMeter = peak.withSizeKeepingCentre(14, peak.getHeight()).reduced(0, 3);
    g.setColour(juce::Colour(0xff0d1014));
    g.fillRoundedRectangle(peakMeter.toFloat(), 2.0f);
    paintSegmentedBar(g,
                      peakMeter.reduced(4, 3),
                      peakValue,
                      { 0.0f,
                        scaleValueToNormalised(peakMeterThresholds.yellowDbfs, peakScale),
                        scaleValueToNormalised(peakMeterThresholds.redDbfs, peakScale),
                        1.0f },
                      { juce::Colour(0xff42d96f),
                        juce::Colour(0xffe0bf35),
                        juce::Colour(0xffe34b4b),
                        juce::Colour(0xffe34b4b) });

    const auto holdY = peakMeter.getBottom() - juce::roundToInt(static_cast<float>(peakMeter.getHeight()) * scaleValueToNormalised(peakDb + 3.0f, peakScale));
    g.setColour(juce::Colour(0xfff6f8fb));
    g.fillRect(juce::Rectangle<int>(peakMeter.getX() + 2, holdY, peakMeter.getWidth() - 4, 1));

    paintCompactLoudness(shortTerm, shortValue);

    g.setColour(juce::Colour(0xff111418));
    g.fillRoundedRectangle(integrated.reduced(1, 2).toFloat(), 3.0f);
    g.setColour(juce::Colour(0xff8de3ff));
    g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
    g.drawText("I " + juce::String(juce::roundToInt(rmsDb - 1.0f)), integrated, juce::Justification::centred);
}

void MixerConsoleView::paintSpatialPanner(juce::Graphics& g, juce::Rectangle<int> bounds, float x, float y, juce::StringRef mode)
{
    if (bounds.getWidth() < 64 || bounds.getHeight() < 64)
        return;

    g.setColour(juce::Colour(0xff101318));
    g.fillRoundedRectangle(bounds.toFloat(), 5.0f);
    g.setColour(juce::Colour(0xff4c5664));
    g.drawRoundedRectangle(bounds.toFloat(), 5.0f, 1.0f);

    auto pad = bounds.reduced(12, 14);
    g.setColour(juce::Colour(0xff26303a));
    g.drawEllipse(pad.toFloat(), 1.0f);
    g.drawLine(static_cast<float>(pad.getCentreX()), static_cast<float>(pad.getY()),
               static_cast<float>(pad.getCentreX()), static_cast<float>(pad.getBottom()), 1.0f);
    g.drawLine(static_cast<float>(pad.getX()), static_cast<float>(pad.getCentreY()),
               static_cast<float>(pad.getRight()), static_cast<float>(pad.getCentreY()), 1.0f);

    std::array<juce::String, 6> speakers { "L", "C", "R", "Ls", "LFE", "Rs" };
    std::array<juce::Point<float>, 6> positions {
        juce::Point<float>(0.14f, 0.18f),
        juce::Point<float>(0.50f, 0.10f),
        juce::Point<float>(0.86f, 0.18f),
        juce::Point<float>(0.18f, 0.82f),
        juce::Point<float>(0.50f, 0.52f),
        juce::Point<float>(0.82f, 0.82f)
    };

    g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
    for (size_t i = 0; i < speakers.size(); ++i)
    {
        const auto point = juce::Point<float>(static_cast<float>(pad.getX()) + positions[i].x * static_cast<float>(pad.getWidth()),
                                             static_cast<float>(pad.getY()) + positions[i].y * static_cast<float>(pad.getHeight()));
        g.setColour(juce::Colour(0xff59616c));
        g.fillEllipse(point.x - 6.0f, point.y - 6.0f, 12.0f, 12.0f);
        g.setColour(juce::Colour(0xffd6dde6));
        g.drawText(speakers[i], juce::Rectangle<int>(juce::roundToInt(point.x) - 10, juce::roundToInt(point.y) - 6, 20, 12), juce::Justification::centred);
    }

    const auto source = juce::Point<float>(static_cast<float>(pad.getX()) + x * static_cast<float>(pad.getWidth()),
                                          static_cast<float>(pad.getY()) + y * static_cast<float>(pad.getHeight()));
    g.setColour(juce::Colour(0xff8de3ff));
    g.fillEllipse(source.x - 7.0f, source.y - 7.0f, 14.0f, 14.0f);

    g.setColour(juce::Colour(0xffc9d1da));
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    auto title = bounds.removeFromTop(16);
    g.drawText(mode, title.removeFromLeft(title.getWidth() - 34), juce::Justification::centred);
    g.setColour(juce::Colour(0xff8de3ff));
    g.drawText("SET", title, juce::Justification::centred);
}

void MixerConsoleView::paintSpatialPannerButton(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef label)
{
    if (bounds.getWidth() < 64 || bounds.getHeight() < 28)
        return;

    g.setColour(juce::Colour(0xff101318));
    g.fillRoundedRectangle(bounds.toFloat(), 5.0f);
    g.setColour(juce::Colour(0xff4c5664));
    g.drawRoundedRectangle(bounds.toFloat(), 5.0f, 1.0f);

    auto icon = bounds.removeFromLeft(34).reduced(8, 8);
    g.setColour(juce::Colour(0xff26303a));
    g.drawEllipse(icon.toFloat(), 1.0f);
    g.drawLine(static_cast<float>(icon.getCentreX()), static_cast<float>(icon.getY()),
               static_cast<float>(icon.getCentreX()), static_cast<float>(icon.getBottom()), 1.0f);
    g.drawLine(static_cast<float>(icon.getX()), static_cast<float>(icon.getCentreY()),
               static_cast<float>(icon.getRight()), static_cast<float>(icon.getCentreY()), 1.0f);
    g.setColour(juce::Colour(0xff8de3ff));
    g.fillEllipse(static_cast<float>(icon.getCentreX()) + 2.0f, static_cast<float>(icon.getCentreY()) - 5.0f, 6.0f, 6.0f);

    g.setColour(juce::Colour(0xfff1f4f7));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText(label, bounds.removeFromTop(bounds.getHeight() / 2), juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff8de3ff));
    g.drawText("OPEN PANNER", bounds, juce::Justification::centredLeft);
}

void MixerConsoleView::paintSpatialPannerPreview(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef layout)
{
    paintSharedSpatialPannerPreview(g, bounds, layout);
}

void MixerConsoleView::paintToggleBadge(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef text, bool active)
{
    if (bounds.getWidth() < 16 || bounds.getHeight() < 10)
        return;

    g.setColour(active ? juce::Colour(0xff1f4936) : juce::Colour(0xff111418));
    g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
    g.setColour(active ? juce::Colour(0xff42d96f) : juce::Colour(0xff6c7480));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText(text, bounds, juce::Justification::centred);
}

void MixerConsoleView::paintStripPeakMeter(juce::Graphics& g,
                                           juce::Rectangle<int> bounds,
                                           std::array<float, 8> peakDb,
                                           std::array<float, 8> holdDb,
                                           int visibleChannels,
                                           bool overload,
                                           bool showsOutput)
{
    if (bounds.getWidth() < 28 || bounds.getHeight() < 96)
        return;

    visibleChannels = juce::jlimit(1, 8, visibleChannels);
    if (! showsOutput)
    {
        for (int channel = 0; channel < visibleChannels; ++channel)
        {
            peakDb[static_cast<size_t>(channel)] -= 5.0f;
            holdDb[static_cast<size_t>(channel)] -= 5.0f;
        }
    }
    const auto scale = getPeakScale();
    const auto peakThresholds = getPeakMeterThresholds();

    if (visibleChannels > 2)
    {
        g.setColour(showsOutput ? juce::Colour(0xff42d96f) : juce::Colour(0xff8de3ff));
        g.fillRect(bounds.removeFromTop(3));
        auto meterArea = bounds.reduced(1, 2);
        const auto cellWidth = meterArea.getWidth() / visibleChannels;

        g.setColour(juce::Colour(0xff111418));
        g.fillRect(meterArea);

        for (int channel = 0; channel < visibleChannels; ++channel)
        {
            auto cell = meterArea.removeFromLeft(channel + 1 == visibleChannels ? meterArea.getWidth() : cellWidth);
            auto meter = cell;

            paintSegmentedBar(g,
                              meter,
                              scaleValueToNormalised(peakDb[static_cast<size_t>(channel)], scale),
                              { 0.0f,
                                scaleValueToNormalised(peakThresholds.yellowDbfs, scale),
                                scaleValueToNormalised(peakThresholds.redDbfs, scale),
                                1.0f },
                              { juce::Colour(0xff42d96f),
                                juce::Colour(0xffe0bf35),
                                juce::Colour(0xffe34b4b),
                                juce::Colour(0xffe34b4b) });

            const auto holdY = meter.getBottom()
                             - juce::roundToInt(static_cast<float>(meter.getHeight())
                                                * scaleValueToNormalised(holdDb[static_cast<size_t>(channel)], scale));
            g.setColour(overload ? juce::Colour(0xffff5b58) : juce::Colour(0xfff6f8fb));
            g.fillRect(juce::Rectangle<int>(meter.getX(), holdY, meter.getWidth(), 1));
        }
        return;
    }

    auto title = bounds.removeFromTop(14);
    g.setColour(juce::Colour(0xffc9d1da));
    g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
    g.drawText(showsOutput ? "OUT" : "IN", title, juce::Justification::centred);

    const std::array<const char*, 8> labels { visibleChannels == 1 ? "M" : "L", "R", "C", "F", "s", "S", "r", "R" };

    auto meterArea = bounds;
    std::array<juce::Rectangle<int>, 8> cells {};
    const auto cellWidth = meterArea.getWidth() / visibleChannels;
    for (int channel = 0; channel < visibleChannels; ++channel)
        cells[static_cast<size_t>(channel)] = meterArea.removeFromLeft(channel + 1 == visibleChannels ? meterArea.getWidth() : cellWidth);

    for (int channel = 0; channel < visibleChannels; ++channel)
    {
        auto cell = cells[static_cast<size_t>(channel)].reduced(1, 0);
        auto label = cell.removeFromTop(10);
        auto valueBox = cell.removeFromBottom(18).reduced(0, 2);
        auto meter = cell.withSizeKeepingCentre(juce::jmin(14, juce::jmax(4, cell.getWidth() - 2)), cell.getHeight()).reduced(0, 4);

        g.setColour(juce::Colour(0xffc9d1da));
        g.setFont(juce::FontOptions(visibleChannels > 2 ? 6.0f : 8.0f, juce::Font::bold));
        g.drawText(labels[static_cast<size_t>(channel)], label, juce::Justification::centred);

        g.setColour(juce::Colour(0xff101318));
        g.fillRoundedRectangle(meter.toFloat(), 3.0f);

        paintSegmentedBar(g,
                          meter.reduced(4, 5),
                          scaleValueToNormalised(peakDb[static_cast<size_t>(channel)], scale),
                          { 0.0f,
                            scaleValueToNormalised(peakThresholds.yellowDbfs, scale),
                            scaleValueToNormalised(peakThresholds.redDbfs, scale),
                            1.0f },
                          { juce::Colour(0xff42d96f),
                            juce::Colour(0xffe0bf35),
                            juce::Colour(0xffe34b4b),
                            juce::Colour(0xffe34b4b) });

        const auto holdY = meter.getBottom()
                         - juce::roundToInt(static_cast<float>(meter.getHeight())
                                            * scaleValueToNormalised(holdDb[static_cast<size_t>(channel)], scale));
        g.setColour(overload ? juce::Colour(0xffff5b58) : juce::Colour(0xfff6f8fb));
        g.fillRect(juce::Rectangle<int>(meter.getX() + 3, holdY, meter.getWidth() - 6, 2));

        g.setColour(juce::Colour(0xff111418));
        g.fillRoundedRectangle(valueBox.toFloat(), 3.0f);
        g.setColour(overload ? juce::Colour(0xffff5b58) : juce::Colour(0xff8de3ff));
        g.setFont(juce::FontOptions(visibleChannels > 2 ? 6.0f : 8.0f, juce::Font::bold));
        g.drawText(juce::String(juce::roundToInt(peakDb[static_cast<size_t>(channel)])), valueBox, juce::Justification::centred);
    }
}

void MixerConsoleView::paintRouteSelector(juce::Graphics& g,
                                          juce::Rectangle<int> bounds,
                                          juce::StringRef label,
                                          juce::StringRef value,
                                          juce::Colour accent)
{
    if (bounds.getWidth() < 54 || bounds.getHeight() < 16)
        return;

    auto labelArea = bounds.removeFromLeft(28);
    g.setColour(juce::Colour(0xffc9d1da));
    g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
    g.drawText(label, labelArea, juce::Justification::centredLeft);

    auto field = bounds.reduced(0, 2);
    g.setColour(juce::Colour(0xff111418));
    g.fillRoundedRectangle(field.toFloat(), 4.0f);
    g.setColour(juce::Colour(0xff26303a));
    g.drawRoundedRectangle(field.toFloat(), 4.0f, 1.0f);

    auto arrow = field.removeFromRight(12).reduced(3, 7).toFloat();
    juce::Path triangle;
    triangle.startNewSubPath(arrow.getX(), arrow.getY());
    triangle.lineTo(arrow.getRight(), arrow.getY());
    triangle.lineTo(arrow.getCentreX(), arrow.getBottom());
    triangle.closeSubPath();
    g.setColour(accent);
    g.fillPath(triangle);

    g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
    g.drawText(value, field.reduced(5, 0), juce::Justification::centredLeft);
}

void MixerConsoleView::showSpatialPannerSettings(bool isSevenOne)
{
    auto& window = isSevenOne ? spatialPanner71SettingsWindow : spatialPanner51SettingsWindow;
    if (window == nullptr)
        window = std::make_unique<SpatialPannerSettingsWindow>(isSevenOne, theme);

    window->setVisible(true);
    window->toFront(true);
}

void MixerConsoleView::showAuxSendSettings(size_t strip)
{
    strip = juce::jmin(strip, stripCount - 1);
    activeAuxStrip = static_cast<int>(strip);
    if (auxSendSettingsWindow == nullptr)
    {
        auxSendSettingsWindow = std::make_unique<AuxSendSettingsWindow>(
            theme,
            auxLevelStates[strip],
            [this](int row, double level)
            {
                const auto currentStrip = activeAuxStrip;
                if (currentStrip < 0 || currentStrip >= static_cast<int>(stripCount)
                    || row < 0 || row >= static_cast<int>(eqBandCount))
                    return;

                auxLevelStates[static_cast<size_t>(currentStrip)][static_cast<size_t>(row)] = level;
                auxControls[static_cast<size_t>(currentStrip)][static_cast<size_t>(row)].setValue(level, juce::dontSendNotification);
                repaint();
            });
    }
    else
    {
        auxSendSettingsWindow->setLevels(auxLevelStates[strip]);
    }

    auxSendSettingsWindow->setVisible(true);
    auxSendSettingsWindow->toFront(true);
}

void MixerConsoleView::showParametricEqSettings(int channelCount, size_t strip)
{
    strip = juce::jmin(strip, stripCount - 1);
    activeParametricEqStrip = static_cast<int>(strip);
    if (parametricEqSettingsWindow == nullptr || parametricEqSettingsWindow->getChannelCount() != channelCount)
    {
        parametricEqSettingsWindow.reset();
        parametricEqSettingsWindow = std::make_unique<ParametricEqSettingsWindow>(
            channelCount,
            eqValueStates[strip],
            [this](int channel, int band, double gain)
            {
                const auto currentStrip = activeParametricEqStrip;
                if (currentStrip < 0 || currentStrip >= static_cast<int>(stripCount)
                    || channel < 0 || channel >= static_cast<int>(eqValueStates[0].size())
                    || band < 0 || band >= 4)
                    return;

                auto& values = eqValueStates[static_cast<size_t>(currentStrip)][static_cast<size_t>(channel)][static_cast<size_t>(band)];
                values[1] = gain;
                const auto mainBandForPopupBand = std::array<int, 4> { 2, -1, 1, 0 };
                const auto mainBand = mainBandForPopupBand[static_cast<size_t>(band)];
                if (mainBand >= 0)
                    eqControls[static_cast<size_t>(currentStrip)][static_cast<size_t>(mainBand)].setValue(gain, juce::dontSendNotification);
                repaint();
            });
    }
    else
    {
        parametricEqSettingsWindow->setChannelBandValues(eqValueStates[strip]);
    }

    parametricEqSettingsWindow->setVisible(true);
    parametricEqSettingsWindow->toFront(true);
}

void MixerConsoleView::showDynamicsSettings(int channelCount, size_t strip)
{
    strip = juce::jmin(strip, stripCount - 1);
    activeDynamicsStrip = static_cast<int>(strip);
    if (dynamicsSettingsWindow == nullptr || dynamicsSettingsWindow->getChannelCount() != channelCount)
    {
        dynamicsSettingsWindow.reset();
        dynamicsSettingsWindow = std::make_unique<DynamicsSettingsWindow>(
            channelCount,
            theme,
            dynamicsThresholdStates[strip],
            [this](int control, double value)
            {
                const auto currentStrip = activeDynamicsStrip;
                if (currentStrip < 0 || currentStrip >= static_cast<int>(stripCount)
                    || control < 0 || control >= 2)
                    return;

                dynamicsThresholdStates[static_cast<size_t>(currentStrip)][static_cast<size_t>(control)] = value;
                dynamicsControls[static_cast<size_t>(currentStrip)][static_cast<size_t>(control)].setValue(value, juce::dontSendNotification);
                repaint();
            });
    }
    else
    {
        dynamicsSettingsWindow->setThresholdValues(dynamicsThresholdStates[strip]);
    }

    dynamicsSettingsWindow->setVisible(true);
    dynamicsSettingsWindow->toFront(true);
}

void MixerConsoleView::showChannelFaderControl(int channelCount)
{
    if (channelCount <= 0)
        channelCount = 8;

    channelFaderControlWindow = std::make_unique<ChannelFaderControlWindow>(channelCount, theme);
    channelFaderControlWindow->setVisible(true);
    channelFaderControlWindow->toFront(true);
}

void MixerConsoleView::showComponentGallery()
{
    if (componentGalleryWindow == nullptr)
        componentGalleryWindow = std::make_unique<ComponentGalleryWindow>();

    componentGalleryWindow->setVisible(true);
    componentGalleryWindow->toFront(true);
}

void MixerConsoleView::paintKnob(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef label, float normalisedValue)
{
    if (bounds.getWidth() < 24 || bounds.getHeight() < 34)
        return;

    auto labelArea = bounds.removeFromBottom(18);
    auto knobArea = bounds.withSizeKeepingCentre(34, 34).toFloat();
    const auto centre = knobArea.getCentre();
    const auto radius = knobArea.getWidth() * 0.5f;
    const auto angle = juce::jmap(juce::jlimit(0.0f, 1.0f, normalisedValue), -2.35f, 2.35f);

    g.setColour(juce::Colour(0xff111418));
    g.fillEllipse(knobArea);
    g.setColour(juce::Colour(0xff4c5664));
    g.drawEllipse(knobArea, 1.2f);
    g.setColour(juce::Colour(0xff8de3ff));
    g.drawLine(centre.x, centre.y,
               centre.x + std::sin(angle) * radius * 0.70f,
               centre.y - std::cos(angle) * radius * 0.70f,
               2.0f);

    g.setColour(juce::Colour(0xffd6dde6));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText(label, labelArea, juce::Justification::centred);
}

void MixerConsoleView::paintMeterCluster(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    if (bounds.getWidth() < 120 || bounds.getHeight() < 120)
        return;

    auto top = bounds.removeFromTop(18);
    g.setColour(juce::Colour(0xffc9d1da));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText(getLoudnessPresetName(), top, juce::Justification::centred);

    auto integrated = bounds.removeFromBottom(36);
    auto meters = bounds.reduced(0, 2);

    auto momentary = meters.removeFromLeft(45);
    auto peak = meters.removeFromLeft(58);
    auto shortTerm = meters;

    paintLoudnessMeter(g, momentary, "M", -18.4f);
    paintPeakMeter(g, peak, -5.8f, -3.2f, false);
    paintLoudnessMeter(g, shortTerm, "S", -20.1f);

    g.setColour(juce::Colour(0xff111418));
    g.fillRoundedRectangle(integrated.reduced(4, 4).toFloat(), 4.0f);
    g.setColour(juce::Colour(0xff8de3ff));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText("I  -19.2 " + getLoudnessScale().unit, integrated.reduced(6, 5), juce::Justification::centred);
}

void MixerConsoleView::paintPeakMeter(juce::Graphics& g, juce::Rectangle<int> bounds, float peakDb, float holdDb, bool overload)
{
    if (bounds.getWidth() < 36 || bounds.getHeight() < 80)
        return;

    auto label = bounds.removeFromTop(16);
    g.setColour(juce::Colour(0xfff1f4f7));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText("PEAK", label, juce::Justification::centred);

    auto scale = getPeakScale();
    auto meter = bounds.reduced(12, 4);
    auto scaleArea = bounds.withRight(meter.getX() - 2);

    g.setColour(juce::Colour(0xff0d1014));
    g.fillRoundedRectangle(meter.toFloat(), 3.0f);

    const auto peakThresholds = getPeakMeterThresholds();
    paintSegmentedBar(g,
                      meter.reduced(5, 5),
                      scaleValueToNormalised(peakDb, scale),
                      { 0.0f,
                        scaleValueToNormalised(peakThresholds.yellowDbfs, scale),
                        scaleValueToNormalised(peakThresholds.redDbfs, scale),
                        1.0f },
                      { juce::Colour(0xff42d96f),
                        juce::Colour(0xffe0bf35),
                        juce::Colour(0xffe34b4b),
                        juce::Colour(0xffe34b4b) });

    const auto hold = scaleValueToNormalised(holdDb, scale);
    const auto holdY = meter.getBottom() - juce::roundToInt(static_cast<float>(meter.getHeight()) * hold);
    g.setColour(overload ? juce::Colour(0xffff5b58) : juce::Colour(0xfff6f8fb));
    g.fillRect(juce::Rectangle<int>(meter.getX() + 5, holdY, meter.getWidth() - 10, 2));

    paintMeterScale(g, scaleArea, scale, false);
    paintMeterScale(g, bounds.withLeft(meter.getRight() + 2), scale, true);
}

void MixerConsoleView::paintLoudnessMeter(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef label, float loudnessValue)
{
    if (bounds.getWidth() < 20 || bounds.getHeight() < 80)
        return;

    auto title = bounds.removeFromTop(16);
    g.setColour(juce::Colour(0xffc9d1da));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText(label, title, juce::Justification::centred);

    auto scale = getLoudnessScale();
    auto meter = bounds.withSizeKeepingCentre(15, bounds.getHeight() - 8);

    g.setColour(juce::Colour(0xff0d1014));
    g.fillRoundedRectangle(meter.toFloat(), 3.0f);

    const auto rmsThresholds = getRmsMeterThresholds();
    paintSegmentedBar(g,
                      meter.reduced(4, 5),
                      scaleValueToNormalised(loudnessValue, scale),
                      { 0.0f,
                        scaleValueToNormalised(rmsThresholds.greenDbfs, getPeakScale()),
                        scaleValueToNormalised(rmsThresholds.yellowDbfs, getPeakScale()),
                        scaleValueToNormalised(rmsThresholds.redDbfs, getPeakScale()) },
                      { juce::Colour(0xff163a78),
                        juce::Colour(0xff27bd63),
                        juce::Colour(0xffe0bf35),
                        juce::Colour(0xffe34b4b) });

    paintMeterScale(g, bounds.withRight(meter.getX() - 2), scale, false);
}

void MixerConsoleView::paintMeterScale(juce::Graphics& g, juce::Rectangle<int> bounds, const MeterScale& scale, bool labelsOnRight)
{
    if (bounds.getWidth() <= 4 || bounds.getHeight() <= 8)
        return;

    g.setColour(juce::Colour(0xff59616c));
    g.setFont(juce::FontOptions(8.0f));

    for (int i = 0; i < scale.tickCount; ++i)
    {
        const auto value = scale.ticks[static_cast<size_t>(i)];
        const auto normalised = scaleValueToNormalised(value, scale);
        const auto y = bounds.getBottom() - juce::roundToInt(static_cast<float>(bounds.getHeight()) * normalised);
        const auto tickStart = labelsOnRight ? bounds.getX() : bounds.getRight() - 5;
        const auto tickEnd = labelsOnRight ? bounds.getX() + 5 : bounds.getRight();

        g.drawLine(static_cast<float>(tickStart), static_cast<float>(y), static_cast<float>(tickEnd), static_cast<float>(y), 1.0f);

        auto text = juce::Rectangle<int>(bounds.getX(), y - 6, bounds.getWidth(), 12);
        const auto just = labelsOnRight ? juce::Justification::centredLeft : juce::Justification::centredRight;
        g.drawText(juce::String(juce::roundToInt(value)), text, just);
    }
}

void MixerConsoleView::paintMeterSettings(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    if (bounds.getWidth() < 220 || bounds.getHeight() < 220)
        return;

    g.setColour(juce::Colour(0xff242a32));
    g.fillRoundedRectangle(bounds.toFloat(), 5.0f);
    g.setColour(juce::Colour(0xff3a414c));
    g.drawRoundedRectangle(bounds.toFloat(), 5.0f, 1.0f);

    auto content = bounds.reduced(14, 12);
    g.setColour(juce::Colour(0xfff1f4f7));
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    auto titleRow = content.removeFromTop(24);
    g.drawText("Meter Thresholds", titleRow.removeFromLeft(150), juce::Justification::centredLeft);

    loudnessPresetBounds = titleRow.removeFromRight(150).reduced(0, 2);
    g.setColour(juce::Colour(0xff111418));
    g.fillRoundedRectangle(loudnessPresetBounds.toFloat(), 4.0f);
    g.setColour(juce::Colour(0xff8de3ff));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText(getLoudnessPresetName(), loudnessPresetBounds, juce::Justification::centred);

    paintThresholdControl(g, 0, content.removeFromTop(34), "Peak Yellow", peakMeterThresholds.yellowDbfs);
    content.removeFromTop(4);
    paintThresholdControl(g, 1, content.removeFromTop(34), "Peak Red", peakMeterThresholds.redDbfs);
    content.removeFromTop(12);
    paintThresholdControl(g, 2, content.removeFromTop(34), "RMS Green", rmsMeterThresholds.greenDbfs);
    content.removeFromTop(4);
    paintThresholdControl(g, 3, content.removeFromTop(34), "RMS Yellow", rmsMeterThresholds.yellowDbfs);
    content.removeFromTop(4);
    paintThresholdControl(g, 4, content.removeFromTop(34), "RMS Red", rmsMeterThresholds.redDbfs);
}

void MixerConsoleView::paintFader(juce::Graphics& g, juce::Rectangle<int> bounds, float normalisedValue)
{
    if (bounds.getWidth() < 16 || bounds.getHeight() < 60)
        return;

    auto slot = bounds.withWidth(8).withCentre({ bounds.getCentreX(), bounds.getCentreY() }).reduced(0, 14);
    g.setColour(juce::Colour(0xff101318));
    g.fillRoundedRectangle(slot.toFloat(), 4.0f);
    g.setColour(juce::Colour(0xff48515e));
    g.drawRoundedRectangle(slot.toFloat(), 4.0f, 1.0f);

    const auto capY = slot.getBottom() - juce::roundToInt(static_cast<float>(slot.getHeight()) * juce::jlimit(0.0f, 1.0f, normalisedValue));
    auto cap = juce::Rectangle<int>(bounds.getX(), capY - 8, bounds.getWidth(), 18);
    g.setColour(juce::Colour(0xffd7dde6));
    g.fillRoundedRectangle(cap.toFloat(), 4.0f);
    g.setColour(juce::Colour(0xff111418));
    g.drawRoundedRectangle(cap.toFloat(), 4.0f, 1.0f);

    g.setColour(juce::Colour(0xffc9d1da));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("0 dB", bounds.removeFromBottom(20), juce::Justification::centred);
}

void MixerConsoleView::paintThresholdControl(juce::Graphics& g,
                                             int index,
                                             juce::Rectangle<int> bounds,
                                             juce::StringRef label,
                                             float valueDbfs)
{
    if (bounds.getWidth() < 180 || bounds.getHeight() < 20)
        return;

    if (juce::isPositiveAndBelow(index, static_cast<int>(thresholdControlBounds.size())))
        thresholdControlBounds[static_cast<size_t>(index)] = bounds;

    g.setColour(juce::Colour(0xffc9d1da));
    g.setFont(juce::FontOptions(12.0f));
    g.drawText(label, bounds.removeFromLeft(96), juce::Justification::centredLeft);

    auto valueBox = bounds.removeFromRight(66);
    g.setColour(juce::Colour(0xff101318));
    g.fillRoundedRectangle(valueBox.toFloat(), 3.0f);
    g.setColour(juce::Colour(0xffd6dde6));
    g.drawText(juce::String(juce::roundToInt(valueDbfs)) + " dBFS", valueBox, juce::Justification::centred);

    auto rail = bounds.reduced(6, 14);
    const auto normalisedValue = scaleValueToNormalised(valueDbfs, getPeakScale());
    g.setColour(juce::Colour(0xff111418));
    g.fillRoundedRectangle(rail.toFloat(), 3.0f);
    g.setColour(juce::Colour(0xff3f8cff));
    g.fillRoundedRectangle(rail.withWidth(juce::roundToInt(static_cast<float>(rail.getWidth()) * normalisedValue)).toFloat(), 3.0f);

    const auto thumbX = rail.getX() + juce::roundToInt(static_cast<float>(rail.getWidth()) * normalisedValue);
    auto thumb = juce::Rectangle<int>(thumbX - 5, rail.getCentreY() - 8, 10, 16);
    g.setColour(index == activeThresholdControl ? juce::Colour(0xffffffff) : juce::Colour(0xffe6edf5));
    g.fillRoundedRectangle(thumb.toFloat(), 3.0f);
}

void MixerConsoleView::paintSegmentedBar(juce::Graphics& g,
                                         juce::Rectangle<int> bounds,
                                         float value,
                                         std::array<float, 4> stops,
                                         std::array<juce::Colour, 4> colours)
{
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
        return;

    value = juce::jlimit(0.0f, 1.0f, value);
    stops[0] = juce::jlimit(0.0f, 1.0f, stops[0]);

    for (size_t i = 1; i < stops.size(); ++i)
        stops[i] = juce::jlimit(stops[i - 1], 1.0f, stops[i]);

    std::array<float, 5> edges { stops[0], stops[1], stops[2], stops[3], 1.0f };

    for (size_t i = 0; i < colours.size(); ++i)
    {
        const auto start = edges[i];
        const auto end = edges[i + 1];

        if (value <= start || end <= start)
            continue;

        const auto visibleEnd = juce::jmin(value, end);
        const auto yTop = bounds.getBottom() - juce::roundToInt(static_cast<float>(bounds.getHeight()) * visibleEnd);
        const auto yBottom = bounds.getBottom() - juce::roundToInt(static_cast<float>(bounds.getHeight()) * start);
        auto segment = juce::Rectangle<int>(bounds.getX(), yTop, bounds.getWidth(), juce::jmax(1, yBottom - yTop));

        g.setColour(colours[i]);
        g.fillRect(segment);
    }
}

void MixerConsoleView::updateThresholdFromPoint(int index, juce::Point<int> point)
{
    if (! juce::isPositiveAndBelow(index, static_cast<int>(thresholdControlBounds.size())))
        return;

    auto rail = thresholdControlBounds[static_cast<size_t>(index)];
    rail.removeFromLeft(96);
    rail.removeFromRight(66);
    rail = rail.reduced(6, 14);

    if (rail.getWidth() <= 0)
        return;

    const auto normalisedValue = juce::jlimit(0.0f, 1.0f, static_cast<float>(point.x - rail.getX()) / static_cast<float>(rail.getWidth()));
    const auto peakScale = getPeakScale();
    const auto valueDbfs = juce::jmap(normalisedValue, peakScale.minimum, peakScale.maximum);

    switch (index)
    {
        case 0: peakMeterThresholds.yellowDbfs = valueDbfs; break;
        case 1: peakMeterThresholds.redDbfs = valueDbfs; break;
        case 2: rmsMeterThresholds.greenDbfs = valueDbfs; break;
        case 3: rmsMeterThresholds.yellowDbfs = valueDbfs; break;
        case 4: rmsMeterThresholds.redDbfs = valueDbfs; break;
        default: break;
    }

    enforceMeterThresholdOrder();
    repaint();
}

void MixerConsoleView::cycleLoudnessPreset()
{
    switch (loudnessScalePreset)
    {
        case LoudnessScalePreset::ebuR128:
            loudnessScalePreset = LoudnessScalePreset::atscA85;
            break;

        case LoudnessScalePreset::atscA85:
            loudnessScalePreset = LoudnessScalePreset::kSystem20;
            break;

        case LoudnessScalePreset::kSystem20:
            loudnessScalePreset = LoudnessScalePreset::ebuR128;
            break;
    }
}

} // namespace mixerpro
