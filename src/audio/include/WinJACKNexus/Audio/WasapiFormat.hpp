#pragma once

#include <cstdint>

namespace WinJACKNexus::Audio {

enum class WasapiStreamDirection : std::uint8_t {
    render,
    capture
};

struct WasapiStreamFormat final {
    std::uint32_t sampleRate = 0;
    std::uint16_t channels = 0;
    std::uint16_t bitsPerSample = 0;
    bool floatingPoint = false;
};

}