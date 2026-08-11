#include <WinJACKNexus/Common/Audio/MeterEngine.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
constexpr double pi = 3.14159265358979323846;

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "MeterEngine test failure: " << message << '\n';
        std::exit(1);
    }
}

void processSine(wjn::common::MeterEngine& engine, float amplitude, double seconds)
{
    constexpr int sampleRate = 48000;
    constexpr int blockSize = 480;
    std::vector<float> block(blockSize);
    const auto totalSamples = static_cast<int>(seconds * sampleRate);
    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        for (int sample = 0; sample < blockSize; ++sample)
            block[static_cast<size_t>(sample)] = amplitude * static_cast<float>(
                std::sin(2.0 * pi * 1000.0 * (offset + sample) / sampleRate));
        engine.process(block.data(), blockSize);
    }
}
}

int main()
{
    wjn::common::MeterEngine engine;
    engine.setSampleRate(48000.0);
    processSine(engine, 0.1f, 4.0);
    const auto tone = engine.getValues();
    require(std::abs(tone.peakDbfs + 20.0f) < 0.15f, "Peak must match -20 dBFS");
    require(std::abs(tone.rmsDbfs + 23.01f) < 0.15f, "RMS must match a sine wave");
    require(tone.momentaryLufs > -26.0f && tone.momentaryLufs < -20.0f, "Momentary loudness must converge");
    require(tone.integratedLufs > -26.0f && tone.integratedLufs < -20.0f, "Integrated loudness must converge");
    require(tone.lraLu < 0.1f, "Steady tone must have negligible LRA");

    engine.reset();
    std::vector<float> silence(480, 0.0f);
    for (int index = 0; index < 120; ++index)
        engine.process(silence.data(), static_cast<int>(silence.size()));
    require(engine.getValues().integratedLufs <= -99.0f, "Silence must remain gated");
    std::cout << "MeterEngine tests passed\n";
    return 0;
}