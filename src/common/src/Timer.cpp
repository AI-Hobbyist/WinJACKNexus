#include <WinJACKNexus/Common/Timer.hpp>

#include <Windows.h>

namespace WinJACKNexus::Common {

Timestamp CpuClock::now() noexcept
{
    LARGE_INTEGER counter {};
    QueryPerformanceCounter(&counter);
    return { counter.QuadPart };
}

double CpuClock::frequency() noexcept
{
    LARGE_INTEGER frequencyValue {};
    QueryPerformanceFrequency(&frequencyValue);
    return static_cast<double>(frequencyValue.QuadPart);
}

double CpuClock::secondsBetween(Timestamp start, Timestamp end) noexcept
{
    const auto clockFrequency = frequency();
    if (clockFrequency <= 0.0)
        return 0.0;
    return static_cast<double>(end.ticks - start.ticks) / clockFrequency;
}

double CpuClock::cpuLoad(Timestamp start,
                         Timestamp end,
                         std::size_t frames,
                         double sampleRate) noexcept
{
    if (frames == 0 || sampleRate <= 0.0)
        return 0.0;
    return secondsBetween(start, end) / (static_cast<double>(frames) / sampleRate);
}

}