#include <WinJACKNexus/Audio/MidiEvent.hpp>

#include <algorithm>

namespace WinJACKNexus::Audio {

bool MidiEvent::assign(const std::uint8_t* source,
                       std::size_t sourceSize,
                       std::uint32_t timestamp) noexcept
{
    if (source == nullptr || sourceSize == 0 || sourceSize > midiEventDataCapacity)
        return false;
    std::copy_n(source, sourceSize, data);
    size = static_cast<std::uint16_t>(sourceSize);
    timestampMilliseconds = timestamp;
    return true;
}

bool MidiEvent::assignShortMessage(std::uint32_t message,
                                   std::uint32_t timestamp) noexcept
{
    const auto status = static_cast<std::uint8_t>(message & 0xffu);
    if (status == 0)
        return false;

    std::size_t messageSize = 3;
    if ((status & 0xf0u) == 0xc0u || (status & 0xf0u) == 0xd0u)
        messageSize = 2;
    else if ((status & 0xf0u) == 0xf0u) {
        switch (status) {
        case 0xf1:
        case 0xf3:
            messageSize = 2;
            break;
        case 0xf2:
            messageSize = 3;
            break;
        default:
            messageSize = 1;
            break;
        }
    }

    const std::uint8_t bytes[] {
        static_cast<std::uint8_t>(message & 0xffu),
        static_cast<std::uint8_t>((message >> 8) & 0xffu),
        static_cast<std::uint8_t>((message >> 16) & 0xffu)
    };
    return assign(bytes, messageSize, timestamp);
}

}