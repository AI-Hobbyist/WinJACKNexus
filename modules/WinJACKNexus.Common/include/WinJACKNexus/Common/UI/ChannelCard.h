#pragma once

#include "MeterComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::common
{

class ChannelCard final : public juce::Component
{
public:
    using Action = std::function<void(ChannelCard&)>;

    explicit ChannelCard(int channelIndex);
    void setChannelName(const juce::String& name);
    juce::String getChannelName() const;
    void setPreset(float targetLufs, float toleranceLu, float truePeakMaxDbtp);
    void setTheme(const ThemeContext& theme);
    void setOnReset(Action callback);
    void setOnRecord(Action callback);
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    juce::Label channelName;
    juce::TextButton resetButton { "重置" };
    juce::TextButton recordButton { "记录" };
    juce::ComboBox presetBox;
    MeterComponent peak { "PEAK", MeterComponent::MeterType::decibels };
    MeterComponent rms { "RMS", MeterComponent::MeterType::decibels };
    MeterComponent truePeak { "dBTP", MeterComponent::MeterType::truePeak };
    MeterComponent momentary { "M", MeterComponent::MeterType::loudness, -60.0f, 0.0f };
    MeterComponent shortTerm { "S", MeterComponent::MeterType::loudness, -60.0f, 0.0f };
    MeterComponent integrated { "I", MeterComponent::MeterType::loudness, -60.0f, 0.0f };
    MeterComponent range { "LRA", MeterComponent::MeterType::range, 0.0f, 72.0f };
    ThemeContext theme;
    Action onReset;
    Action onRecord;
};

} // namespace wjn::common
