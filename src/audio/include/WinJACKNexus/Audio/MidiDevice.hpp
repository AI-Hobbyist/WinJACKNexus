#pragma once

#include <WinJACKNexus/Audio/Export.hpp>

#include <cstddef>
#include <cstdint>

namespace WinJACKNexus::Audio {

enum class MidiDirection : std::uint8_t {
    input,
    output
};

constexpr std::size_t midiDeviceNameCapacity = 128;

struct MidiDeviceInfo final {
    std::uint32_t deviceId = 0;
    wchar_t name[midiDeviceNameCapacity] {};
    bool isInput = false;
    bool isOutput = false;
};

using MidiDeviceVisitor = bool (*)(const MidiDeviceInfo& device, void* userData) noexcept;

class WINJACK_AUDIO_API MidiDeviceEnumerator final {
public:
    MidiDeviceEnumerator() noexcept = default;

    std::size_t enumerate(MidiDirection direction,
                          MidiDeviceVisitor visitor,
                          void* userData) noexcept;
    bool firstDevice(MidiDirection direction, MidiDeviceInfo& result) noexcept;
    std::uint32_t lastError() const noexcept;

private:
    std::uint32_t lastError_ = 0;
};

}