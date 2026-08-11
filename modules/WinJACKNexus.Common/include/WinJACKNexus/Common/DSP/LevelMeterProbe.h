#pragma once

#include <WinJACKNexus/Common/Metering/MeterFrame.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace wjn::common
{

class LevelMeterProbe final
{
public:
    void prepare(int channelCount);
    void reset() noexcept;
    void process(const juce::AudioBuffer<float>& buffer) noexcept;
    const MeterFrame& getLatestFrame() const noexcept;

private:
    MeterFrame latestFrame;
};

} // namespace wjn::common
