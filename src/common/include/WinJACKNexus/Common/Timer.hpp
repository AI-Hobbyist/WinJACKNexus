#pragma once

#include <WinJACKNexus/Common/Export.hpp>

#include <cstddef>
#include <cstdint>

namespace WinJACKNexus::Common {

struct Timestamp final {
    std::int64_t ticks {};
};

class WINJACK_COMMON_API CpuClock final {
public:
    static Timestamp now() noexcept;
    static double frequency() noexcept;
    static double secondsBetween(Timestamp start, Timestamp end) noexcept;
    static double cpuLoad(Timestamp start,
                          Timestamp end,
                          std::size_t frames,
                          double sampleRate) noexcept;
};

}