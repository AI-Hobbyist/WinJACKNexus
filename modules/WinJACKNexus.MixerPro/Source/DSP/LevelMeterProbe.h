#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

namespace mixerpro
{

struct MeterFrame
{
    static constexpr int maxChannels = 8;

    std::array<float, maxChannels> peak {};
    std::array<float, maxChannels> rms {};
    std::array<float, maxChannels> peakHold {};
    int channelCount = 0;
    bool overload = false;
};

class LevelMeterProbe
{
public:
    void prepare(int channelCount);
    void reset() noexcept;
    void process(const juce::AudioBuffer<float>& buffer) noexcept;
    const MeterFrame& getLatestFrame() const noexcept;

private:
    MeterFrame latestFrame;
};

} // namespace mixerpro
