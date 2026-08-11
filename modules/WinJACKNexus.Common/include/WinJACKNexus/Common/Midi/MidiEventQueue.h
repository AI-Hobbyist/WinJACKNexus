#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <WinJACKNexus/Common/IO/SpscRingBuffer.h>

namespace wjn::common
{

struct MidiEvent
{
    static constexpr size_t maxBytes = 16;

    uint32_t sampleOffset = 0;
    uint8_t size = 0;
    std::array<uint8_t, maxBytes> bytes {};

    static MidiEvent make(uint32_t offset, const uint8_t* data, size_t byteCount) noexcept;
};

class MidiEventQueue final
{
public:
    static constexpr size_t storageCapacity = 257;

    bool push(const MidiEvent& event) noexcept;
    bool push(uint32_t sampleOffset, const uint8_t* data, size_t byteCount) noexcept;
    bool pop(MidiEvent& event) noexcept;
    void reset() noexcept;
    size_t size() const noexcept;

private:
    SpscRingBuffer<MidiEvent, storageCapacity> queue;
};

} // namespace wjn::common
