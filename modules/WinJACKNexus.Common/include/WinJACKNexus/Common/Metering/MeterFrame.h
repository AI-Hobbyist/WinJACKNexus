#pragma once

#include <array>

namespace wjn::common
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

} // namespace wjn::common
