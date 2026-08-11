#include "LevelMeterProbe.h"

#include <algorithm>
#include <cmath>

namespace wjn::common
{

void LevelMeterProbe::prepare(int channelCount)
{
    reset();
    latestFrame.channelCount = std::clamp(channelCount, 0, MeterFrame::maxChannels);
}

void LevelMeterProbe::reset() noexcept
{
    latestFrame = {};
}

void LevelMeterProbe::process(const juce::AudioBuffer<float>& buffer) noexcept
{
    const auto channels = std::min({ buffer.getNumChannels(), latestFrame.channelCount, MeterFrame::maxChannels });
    latestFrame.overload = false;
    for (int channel = 0; channel < channels; ++channel)
    {
        const auto* data = buffer.getReadPointer(channel);
        const auto samples = buffer.getNumSamples();
        float peak = 0.0f;
        double sumSquares = 0.0;
        for (int sample = 0; sample < samples; ++sample)
        {
            peak = std::max(peak, std::abs(data[sample]));
            sumSquares += static_cast<double>(data[sample]) * data[sample];
        }
        latestFrame.peak[static_cast<size_t>(channel)] = peak;
        latestFrame.rms[static_cast<size_t>(channel)] = samples > 0
            ? static_cast<float>(std::sqrt(sumSquares / samples)) : 0.0f;
        latestFrame.peakHold[static_cast<size_t>(channel)] =
            std::max(latestFrame.peakHold[static_cast<size_t>(channel)], peak);
        if (peak >= 1.0f)
            latestFrame.overload = true;
    }
}

const MeterFrame& LevelMeterProbe::getLatestFrame() const noexcept { return latestFrame; }

} // namespace wjn::common
