#pragma once

#include <cstdint>

namespace WinJACKNexus::Patchbay::Core {

enum class PortKind : std::uint8_t {
    audio,
    midi,
    cv,
    osc,
    unknown
};

enum class Direction : std::uint8_t {
    input,
    output,
    unknown
};

enum class PortGroupHint : std::uint32_t {
    none = 0,
    mainInput = 1u << 0,
    mainOutput = 1u << 1,
    stereo = 1u << 2,
    midSide = 1u << 3
};

constexpr PortGroupHint operator|(PortGroupHint left, PortGroupHint right) noexcept
{
    return static_cast<PortGroupHint>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

constexpr bool hasHint(PortGroupHint value, PortGroupHint hint) noexcept
{
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(hint)) != 0;
}

}