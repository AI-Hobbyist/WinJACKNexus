#include "SilenceDetector.h"

#include <algorithm>

namespace wjn::common
{

void SilenceDetector::setThresholdDb(float newThresholdDb)
{
    thresholdDb = std::clamp(newThresholdDb, -120.0f, 0.0f);
    reset();
}

void SilenceDetector::setDurationSeconds(float newDurationSeconds)
{
    durationSeconds = std::clamp(newDurationSeconds, 0.1f, 3600.0f);
    reset();
}

void SilenceDetector::reset()
{
    silentSamples = 0.0;
    armed = false;
    triggered = false;
}

bool SilenceDetector::processBlock(float peakDbfs, int frameCount, double sampleRate)
{
    if (frameCount <= 0 || sampleRate <= 0.0)
        return false;
    if (peakDbfs > thresholdDb)
    {
        armed = true;
        silentSamples = 0.0;
        triggered = false;
        return false;
    }
    if (!armed || triggered)
        return false;
    silentSamples += frameCount;
    if (silentSamples < durationSeconds * sampleRate)
        return false;
    triggered = true;
    return true;
}

} // namespace wjn::common
