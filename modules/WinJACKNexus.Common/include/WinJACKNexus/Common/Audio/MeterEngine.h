#pragma once

#include <array>
#include <deque>
#include <vector>

namespace wjn::common
{

struct MeterValues
{
    float peakDbfs = -100.0f;
    float rmsDbfs = -100.0f;
    float truePeakDbtp = -100.0f;
    float momentaryLufs = -100.0f;
    float shortTermLufs = -100.0f;
    float integratedLufs = -100.0f;
    float lraLu = 0.0f;
};

class MeterEngine final
{
public:
    void setSampleRate(double newSampleRate);
    void process(const float* samples, int frameCount);
    void reset();
    MeterValues getValues() const { return values; }

private:
    struct Biquad
    {
        float b0 = 1.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
        float z1 = 0.0f;
        float z2 = 0.0f;
        float process(float sample);
        void clear();
    };

    static float toDecibels(double linear);
    static float toLufs(double meanSquare);
    static float percentile(std::vector<float> values, float fraction);
    static Biquad makeHighShelf(double sampleRate);
    static Biquad makeHighPass(double sampleRate);
    void appendEnergy(float sample, float weightedSample);
    void appendIntegratedBlock();
    void appendShortTermBlock();
    void updateIntegratedLoudness();
    void updateLra();
    float calculateTruePeak(float sample);

    double sampleRate = 0.0;
    int rmsWindowSamples = 480;
    int momentaryWindowSamples = 19200;
    int shortTermWindowSamples = 144000;
    int integratedHopSamples = 4800;
    int shortTermHopSamples = 48000;
    int samplesSinceIntegratedBlock = 0;
    int samplesSinceShortTermBlock = 0;
    double rmsEnergySum = 0.0;
    double momentaryEnergySum = 0.0;
    double shortTermEnergySum = 0.0;
    std::deque<float> rmsEnergies;
    std::deque<float> momentaryEnergies;
    std::deque<float> shortTermEnergies;
    std::vector<double> integratedBlockEnergies;
    std::vector<float> shortTermLoudnesses;
    std::array<float, 32> truePeakHistory {};
    int truePeakWriteIndex = 0;
    int truePeakHistorySize = 0;
    Biquad preFilter;
    Biquad highPass;
    MeterValues values;
};

} // namespace wjn::common
