// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <WinJACKNexus/Node/ProcessingApi.h>
#include <WinJACKNexus/Common/MidiToolsCore.hpp>
#include <cstring>

namespace WinJACKNexus::MidiTools
{
inline const WjnNodeMidiInputV1 *virtualMidiInput(const WjnNodeProcessV1 &block, uint32_t id)
{
    for (uint32_t index = 0; block.midiInputs && index < block.midiInputCount; ++index)
    {
        if (block.midiInputs[index].portId == id)
        {
            return &block.midiInputs[index];
        }
    }
    return nullptr;
}

inline void clearVirtualMidi(WjnNodeProcessV1 &block)
{
    for (uint32_t index = 0; block.midiOutputs && index < block.midiOutputCount; ++index)
    {
        block.midiOutputs[index].eventCount = 0;
        block.midiOutputs[index].byteCount = 0;
    }
}

inline bool writeVirtualMidi(WjnNodeProcessV1 &block, uint32_t id,
                             Common::MidiTools::EventView event)
{
    for (uint32_t index = 0; block.midiOutputs && index < block.midiOutputCount; ++index)
    {
        auto &output = block.midiOutputs[index];
        if (output.portId != id)
        {
            continue;
        }
        if (!output.events || !output.bytes || output.eventCount >= output.eventCapacity ||
            output.byteCount > output.byteCapacity ||
            event.bytes.size() > output.byteCapacity - output.byteCount)
        {
            ++output.dropped;
            return false;
        }
        auto *payload = output.bytes + output.byteCount;
        std::memcpy(payload, event.bytes.data(), event.bytes.size());
        output.events[output.eventCount++] = {event.frame,
                                              static_cast<uint32_t>(event.bytes.size()), payload};
        output.byteCount += static_cast<uint32_t>(event.bytes.size());
        return true;
    }
    return false;
}
} // namespace WinJACKNexus::MidiTools
