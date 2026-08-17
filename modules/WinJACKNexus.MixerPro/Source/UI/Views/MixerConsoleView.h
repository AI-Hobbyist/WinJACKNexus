#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <WinJACKNexus/Common/UI/RotaryControl.h>
#include <WinJACKNexus/Common/UI/RouteSelectorControl.h>
#include <WinJACKNexus/Common/UI/SegmentedMeterControl.h>
#include <WinJACKNexus/Common/UI/MultiChannelMeterControl.h>
#include <WinJACKNexus/Common/UI/SpatialPannerComponent.h>
#include <WinJACKNexus/Common/UI/ToggleBadgeControl.h>
#include <WinJACKNexus/Common/UI/VerticalFaderControl.h>

#include <array>
#include <functional>

namespace wjn::common
{
class LocaleManager;
class ThemeContext;
}

namespace mixerpro
{

class CommonJackMixerRuntime;

class MixerConsoleView final : public juce::Component, private juce::Timer
{
public:
    MixerConsoleView(CommonJackMixerRuntime& audioRuntime,
                     const wjn::common::ThemeContext& theme,
                     wjn::common::LocaleManager& localeManager);
    ~MixerConsoleView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    static constexpr size_t stereoChannelCount = 2;
    static constexpr size_t stripCount = 8;
    static constexpr size_t eqBandCount = 3;
    using EqBandValueState = std::array<double, 3>;
    using EqChannelValueState = std::array<EqBandValueState, 4>;
    using EqStripValueState = std::array<EqChannelValueState, 8>;

    void timerCallback() override;
    void configureCommonControls();
    void configureStripControl(wjn::common::RotaryControl& control,
                               juce::String label,
                               float value,
                               juce::Colour accent,
                               std::function<void(double)> callback = {});
    void configureToggleControl(wjn::common::ToggleBadgeControl& control,
                                juce::String label,
                                bool active,
                                juce::Colour accent,
                                std::function<void(bool)> callback = {});
    void setStripControlsVisible(size_t strip, bool visible);
    void layoutStandardStripControls(size_t strip,
                                     juce::Rectangle<int> bounds,
                                     bool hasInputSelector,
                                     bool hasAuxSends,
                                     bool spatial,
                                     juce::StringRef panMode);
    void layoutMasterControls(juce::Rectangle<int> bounds);
    void refreshLocalizedLabels();
    void updateMetersAndState();
    juce::String localizedText(const juce::String& key, const juce::String& fallback) const;

    class SpatialPannerSettingsComponent;
    class SpatialPannerSettingsWindow;
    class AuxSendSettingsComponent;
    class AuxSendSettingsWindow;
    class ParametricEqSettingsComponent;
    class ParametricEqSettingsWindow;
    class DynamicsSettingsComponent;
    class DynamicsSettingsWindow;
    class ChannelFaderControlComponent;
    class ChannelFaderControlWindow;
    class ComponentGalleryComponent;
    class ComponentGalleryWindow;

    struct PeakMeterThresholds
    {
        float yellowDbfs = -18.0f;
        float redDbfs = -6.0f;
    };

    struct RmsMeterThresholds
    {
        float greenDbfs = -36.0f;
        float yellowDbfs = -18.0f;
        float redDbfs = -9.0f;
    };

    enum class LoudnessScalePreset
    {
        ebuR128,
        atscA85,
        kSystem20
    };

    struct MeterScale
    {
        juce::String name;
        juce::String unit;
        float minimum = -60.0f;
        float maximum = 0.0f;
        std::array<float, 7> ticks {};
        int tickCount = 0;
    };

    void enforceMeterThresholdOrder();
    PeakMeterThresholds getPeakMeterThresholds() const noexcept;
    RmsMeterThresholds getRmsMeterThresholds() const noexcept;
    MeterScale getPeakScale() const;
    MeterScale getLoudnessScale() const;
    float scaleValueToNormalised(float value, const MeterScale& scale) const noexcept;
    juce::String getLoudnessPresetName() const;

    void paintMasterStrip(juce::Graphics& g, juce::Rectangle<int> bounds);
    void paintInputStripExample(juce::Graphics& g,
                                juce::Rectangle<int> bounds,
                                juce::StringRef title,
                                juce::StringRef layout,
                                juce::StringRef inputPort,
                                juce::StringRef outputTarget,
                                bool spatial);
    void paintAuxStripExample(juce::Graphics& g, juce::Rectangle<int> bounds);
    void paintSubmixStripExample(juce::Graphics& g, juce::Rectangle<int> bounds);
    void paintStandardChannelStrip(juce::Graphics& g,
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
                                   float panValue);
    void paintStripShell(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef title, juce::Colour accent);
    void paintTinyMeter(juce::Graphics& g, juce::Rectangle<int> bounds, float peakDb, float rmsDb);
    void paintSpatialPanner(juce::Graphics& g, juce::Rectangle<int> bounds, float x, float y, juce::StringRef mode);
    void paintSpatialPannerButton(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef label);
    void paintSpatialPannerPreview(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef layout);
    void paintToggleBadge(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef text, bool active);
    void paintStripPeakMeter(juce::Graphics& g,
                             juce::Rectangle<int> bounds,
                             std::array<float, 8> peakDb,
                             std::array<float, 8> holdDb,
                             int visibleChannels,
                             bool overload,
                             bool showsOutput);
    void paintRouteSelector(juce::Graphics& g,
                            juce::Rectangle<int> bounds,
                            juce::StringRef label,
                            juce::StringRef value,
                            juce::Colour accent);
    void showSpatialPannerSettings(bool isSevenOne);
    void showAuxSendSettings(size_t strip);
    void showParametricEqSettings(int channelCount, size_t strip);
    void showDynamicsSettings(int channelCount, size_t strip);
    void showChannelFaderControl(int channelCount);
    void showComponentGallery();
    void paintKnob(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef label, float normalisedValue);
    void paintMeterCluster(juce::Graphics& g, juce::Rectangle<int> bounds);
    void paintPeakMeter(juce::Graphics& g, juce::Rectangle<int> bounds, float peakDb, float holdDb, bool overload);
    void paintLoudnessMeter(juce::Graphics& g, juce::Rectangle<int> bounds, juce::StringRef label, float loudnessValue);
    void paintMeterScale(juce::Graphics& g, juce::Rectangle<int> bounds, const MeterScale& scale, bool labelsOnRight);
    void paintMeterSettings(juce::Graphics& g, juce::Rectangle<int> bounds);
    void paintFader(juce::Graphics& g, juce::Rectangle<int> bounds, float normalisedValue);
    void paintThresholdControl(juce::Graphics& g, int index, juce::Rectangle<int> bounds, juce::StringRef label, float valueDbfs);
    void paintSegmentedBar(juce::Graphics& g,
                           juce::Rectangle<int> bounds,
                           float value,
                           std::array<float, 4> stops,
                           std::array<juce::Colour, 4> colours);
    void updateThresholdFromPoint(int index, juce::Point<int> point);
    void cycleLoudnessPreset();

    PeakMeterThresholds peakMeterThresholds;
    RmsMeterThresholds rmsMeterThresholds;
    std::array<juce::Rectangle<int>, 5> thresholdControlBounds;
    juce::Rectangle<int> loudnessPresetBounds;
    std::array<juce::Rectangle<int>, 2> spatialPannerBounds;
    juce::Rectangle<int> auxSendBounds;
    juce::Rectangle<int> parametricEqBounds;
    std::array<juce::Rectangle<int>, 8> eqSectionBounds;
    std::array<juce::Rectangle<int>, 8> eqToggleBounds;
    std::array<juce::Rectangle<int>, 8> dynamicsBounds;
    std::array<juce::Rectangle<int>, 8> dynamicsToggleBounds;
    std::array<bool, 8> dynamicsEnabled { true, false, true, true, false, true, true, true };
    std::array<bool, 8> eqEnabled { true, false, true, false, true, true, true, true };
    std::array<bool, 8> meterShowsOutput { false, false, false, true, false, true, true, true };
    std::array<juce::Rectangle<int>, 8> auxSectionBounds;
    std::array<juce::Rectangle<int>, 8> meterSectionBounds;
    std::array<juce::Rectangle<int>, 8> faderSectionBounds;
    std::array<int, 8> faderSectionChannelCounts {};
    juce::Rectangle<int> masterFaderBounds;
    juce::Rectangle<int> componentGalleryBounds;
    std::unique_ptr<SpatialPannerSettingsWindow> spatialPanner51SettingsWindow;
    std::unique_ptr<SpatialPannerSettingsWindow> spatialPanner71SettingsWindow;
    std::unique_ptr<AuxSendSettingsWindow> auxSendSettingsWindow;
    std::unique_ptr<ParametricEqSettingsWindow> parametricEqSettingsWindow;
    std::unique_ptr<DynamicsSettingsWindow> dynamicsSettingsWindow;
    std::unique_ptr<ChannelFaderControlWindow> channelFaderControlWindow;
    std::unique_ptr<ComponentGalleryWindow> componentGalleryWindow;
    std::array<EqStripValueState, stripCount> eqValueStates {};
    std::array<std::array<double, 2>, stripCount> dynamicsThresholdStates {};
    std::array<std::array<double, 3>, stripCount> auxLevelStates {};
    int activeAuxStrip = -1;
    int activeParametricEqStrip = -1;
    int activeDynamicsStrip = -1;
    LoudnessScalePreset loudnessScalePreset = LoudnessScalePreset::ebuR128;
    int activeThresholdControl = -1;
    int standardStripPaintIndex = 0;
    std::array<wjn::common::RotaryControl, stripCount> gainControls;
    std::array<std::array<wjn::common::RotaryControl, eqBandCount>, stripCount> eqControls;
    std::array<std::array<wjn::common::RotaryControl, eqBandCount>, stripCount> auxControls;
    std::array<std::array<wjn::common::RotaryControl, 2>, stripCount> dynamicsControls;
    std::array<wjn::common::RotaryControl, stripCount> panControls;
    std::array<wjn::common::ToggleBadgeControl, stripCount> muteControls;
    std::array<wjn::common::ToggleBadgeControl, stripCount> soloControls;
    std::array<wjn::common::ToggleBadgeControl, stripCount> eqToggleControls;
    std::array<wjn::common::ToggleBadgeControl, stripCount> auxSetControls;
    std::array<wjn::common::ToggleBadgeControl, stripCount> dynamicsToggleControls;
    std::array<wjn::common::ToggleBadgeControl, stripCount> lowCutControls;
    std::array<wjn::common::RouteSelectorControl, stripCount> inputRouteControls;
    std::array<wjn::common::RouteSelectorControl, stripCount> outputRouteControls;
    std::array<wjn::common::MultiChannelMeterControl, stripCount> meterControls;
    std::array<wjn::common::VerticalFaderControl, stripCount> faderControls;
    wjn::common::SpatialPannerComponent spatial51Control { false };
    wjn::common::SpatialPannerComponent spatial71Control { true };
    wjn::common::ToggleBadgeControl jackStatusControl { "JACK", false };
    juce::String jackStatusText;
    CommonJackMixerRuntime& audioRuntime;
    const wjn::common::ThemeContext& theme;
    wjn::common::LocaleManager& localeManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerConsoleView)
};

} // namespace mixerpro
