#include <WinJACKNexus/Common/Audio/SilenceDetector.h>

#include <cstdlib>
#include <iostream>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "SilenceDetector test failure: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    wjn::common::SilenceDetector detector;
    detector.setThresholdDb(-60.0f);
    detector.setDurationSeconds(1.0f);
    for (int index = 0; index < 100; ++index)
        require(!detector.processBlock(-80.0f, 480, 48000.0), "Initial silence must not trigger");
    detector.processBlock(-20.0f, 480, 48000.0);
    for (int index = 0; index < 99; ++index)
        require(!detector.processBlock(-80.0f, 480, 48000.0), "Must wait for full duration");
    require(detector.processBlock(-80.0f, 480, 48000.0), "Silence must trigger at duration");
    require(!detector.processBlock(-80.0f, 480, 48000.0), "Trigger must fire once");
    std::cout << "SilenceDetector tests passed\n";
    return 0;
}