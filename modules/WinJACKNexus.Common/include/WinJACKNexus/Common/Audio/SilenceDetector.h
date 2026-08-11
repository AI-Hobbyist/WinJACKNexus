#pragma once

namespace wjn::common
{

class SilenceDetector final
{
public:
    void setThresholdDb(float newThresholdDb);
    void setDurationSeconds(float newDurationSeconds);
    void reset();
    bool processBlock(float peakDbfs, int frameCount, double sampleRate);

private:
    float thresholdDb = -60.0f;
    float durationSeconds = 5.0f;
    double silentSamples = 0.0;
    bool armed = false;
    bool triggered = false;
};

} // namespace wjn::common
