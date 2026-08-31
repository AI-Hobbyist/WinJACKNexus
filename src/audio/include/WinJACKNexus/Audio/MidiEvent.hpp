#pragma once

#include <WinJACKNexus/Audio/Export.hpp>

#include <cstddef>
#include <cstdint>

namespace WinJACKNexus::Audio {

constexpr std::size_t midiEventDataCapacity = 1024;

struct WINJACK_AUDIO_API MidiEvent final {
    std::uint32_t timestampMilliseconds = 0;
    std::uint16_t size = 0;
    std::uint8_t data[midiEventDataCapacity] {};

    bool assign(const std::uint8_t* source,
                std::size_t sourceSize,
                std::uint32_t timestamp = 0) noexcept;
    bool assignShortMessage(std::uint32_t message,
                            std::uint32_t timestamp = 0) noexcept;
};

}