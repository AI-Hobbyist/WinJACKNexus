#pragma once

#include "ThemeContext.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace wjn::common
{

class MultiChannelMeterControl final : public juce::Component
{
public:
    static constexpr int maxChannels = 8;

    explicit MultiChannelMeterControl(int visibleChannels = 2);

    void setChannelCount(int newChannelCount);
    int getChannelCount() const noexcept { return channelCount; }
    void setPeakDb(const std::array<float, maxChannels>& values);
    void setHoldDb(const std::array<float, maxChannels>& values);
    void setOverload(bool shouldShowOverload);
    void setShowsOutput(bool shouldShowOutput, juce::NotificationType notification = juce::dontSendNotification);
    bool getShowsOutput() const noexcept { return showsOutput; }
    void setAccent(juce::Colour newAccent);
    void setTheme(const ThemeContext& newTheme);
    void setSourceChangeCallback(std::function<void(bool)> callback);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void paintSegmentedBar(juce::Graphics& g, juce::Rectangle<int> bounds,
                           float value, std::array<float, 4> stops,
                           std::array<juce::Colour, 4> colours) const;
    float scaleValueToNormalised(float value) const noexcept;

    ThemeContext theme;
    juce::Colour accent { 0xff8de3ff };
    std::array<float, maxChannels> peakDb { -60.0f, -60.0f, -60.0f, -60.0f,
                                             -60.0f, -60.0f, -60.0f, -60.0f };
    std::array<float, maxChannels> holdDb { -60.0f, -60.0f, -60.0f, -60.0f,
                                             -60.0f, -60.0f, -60.0f, -60.0f };
    int channelCount = 2;
    bool overload = false;
    bool showsOutput = false;
    std::function<void(bool)> sourceChangeCallback;
};

} // namespace wjn::common