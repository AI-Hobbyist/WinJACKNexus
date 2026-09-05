/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
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