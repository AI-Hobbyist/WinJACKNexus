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