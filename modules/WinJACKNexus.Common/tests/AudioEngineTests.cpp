#include <WinJACKNexus/Common/Audio/AudioEngine.h>

#include <cmath>
#include <iostream>

namespace
{

int fail(const char* message)
{
    std::cerr << message << '\n';
    return 1;
}

bool nearlyEqual(float actual, float expected)
{
    return std::abs(actual - expected) < 0.000001f;
}

} // namespace

int main()
{
    wjn::common::AudioEngine engine;
    engine.prepare({48000.0, 128, 2, 2});

    float inputLeft[4] {0.1f, 0.2f, 0.3f, 0.4f};
    float inputRight[4] {-0.1f, -0.2f, -0.3f, -0.4f};
    float outputLeft[4] {1.0f, 1.0f, 1.0f, 1.0f};
    float outputRight[4] {1.0f, 1.0f, 1.0f, 1.0f};
    const float* inputs[] {inputLeft, inputRight};
    float* outputs[] {outputLeft, outputRight};
    wjn::common::AudioProcessContext context {inputs, outputs, 2, 2, 4};

    engine.process(context);
    for (float sample : outputLeft)
        if (!nearlyEqual(sample, 0.0f))
            return fail("Stopped engine must clear output");

    engine.start();
    engine.process(context);
    for (int index = 0; index < 4; ++index)
        if (!nearlyEqual(outputLeft[index], inputLeft[index])
            || !nearlyEqual(outputRight[index], inputRight[index]))
            return fail("Running engine must pass through audio");

    std::cout << "AudioEngine tests passed\n";
    return 0;
}