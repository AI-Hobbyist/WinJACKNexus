// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <WinJACKNexus/Node/ProcessingApi.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace WinJACKNexus::Patchbay
{
// The control thread compiles a root and all descendants into one schedule.
// Interface IDs are junctions shared by parent pins and child interface nodes.
// The host pauses the root callback before replacing topology or instances.
class VirtualGraph final
{
  public:
    static constexpr uint32_t midiEvents = 2048;
    static constexpr uint32_t midiBytes = 65536;
    static constexpr std::size_t invalidSlot = std::numeric_limits<std::size_t>::max();
    struct Port
    {
        std::string id;
        std::string owner;
        uint32_t apiId{};
        uint32_t type{};
        uint32_t channels{};
        uint32_t direction{};
        bool junction{};
    };
    struct Processor
    {
        std::string id;
        void *instance{};
        const WjnNodeVirtualApiV1 *api{};
    };
    struct Route
    {
        std::string source;
        std::string destination;
    };
    struct Topology
    {
        std::vector<Port> ports;
        std::vector<Processor> processors;
        std::vector<Route> routes;
    };

    bool rebuild(const Topology &topology, uint32_t maxFrames, std::string &error)
    {
        try
        {
            auto candidate = std::make_unique<State>();
            if (!candidate->compile(topology, maxFrames, error))
            {
                return false;
            }
            state_.swap(candidate);
            error.clear();
            return true;
        }
        catch (const std::exception &exception)
        {
            error = exception.what();
            return false;
        }
    }

    std::size_t slot(const std::string &id) const noexcept
    {
        if (!state_)
        {
            return invalidSlot;
        }
        const auto found = state_->portSlots.find(id);
        return found == state_->portSlots.end() ? invalidSlot : found->second;
    }

    bool begin(uint32_t frames) noexcept
    {
        if (!state_ || frames > state_->maxFrames)
        {
            return false;
        }
        for (auto &buffer : state_->buffers)
        {
            for (auto *channel : buffer.channels)
            {
                std::fill_n(channel, frames, 0.0f);
            }
            buffer.output.eventCount = 0;
            buffer.output.byteCount = 0;
        }
        return true;
    }

    float *audio(std::size_t index, uint32_t channel = 0) noexcept
    {
        return state_ && index < state_->buffers.size() &&
                       channel < state_->buffers[index].channels.size()
                   ? state_->buffers[index].channels[channel]
                   : nullptr;
    }

    const WjnNodeMidiOutputV1 *midi(std::size_t index) const noexcept
    {
        return state_ && index < state_->buffers.size() ? &state_->buffers[index].output : nullptr;
    }

    bool appendMidi(std::size_t index, const WjnNodeMidiEventV1 &event, uint32_t frames) noexcept
    {
        return state_ && index < state_->buffers.size() &&
               append(state_->buffers[index].output, event, frames);
    }

    void process(uint32_t frames, uint64_t samplePosition) noexcept
    {
        if (!state_ || frames > state_->maxFrames)
        {
            return;
        }
        for (const auto index : state_->schedule)
        {
            auto &operation = state_->operations[index];
            for (const auto &route : operation.incoming)
            {
                auto &source = state_->buffers[route.first];
                auto &destination = state_->buffers[route.second];
                for (std::size_t channel = 0; channel < source.channels.size(); ++channel)
                {
                    for (uint32_t frame = 0; frame < frames; ++frame)
                    {
                        destination.channels[channel][frame] += source.channels[channel][frame];
                    }
                }
                for (uint32_t event = 0; event < source.output.eventCount; ++event)
                {
                    append(destination.output, source.output.events[event], frames);
                }
            }
            if (!operation.processor.api)
            {
                continue;
            }
            for (std::size_t index = 0; index < operation.midiInputSlots.size(); ++index)
            {
                const auto &buffer = state_->buffers[operation.midiInputSlots[index]];
                operation.midiInputs[index].eventCount = buffer.output.eventCount;
                operation.midiInputs[index].events = buffer.output.events;
            }
            for (std::size_t index = 0; index < operation.midiOutputSlots.size(); ++index)
            {
                operation.midiOutputs[index] =
                    state_->buffers[operation.midiOutputSlots[index]].output;
            }
            WjnNodeProcessV1 block{sizeof(block),
                                   frames,
                                   samplePosition,
                                   static_cast<uint32_t>(operation.audioInputs.size()),
                                   static_cast<uint32_t>(operation.audioOutputs.size()),
                                   operation.audioInputs.data(),
                                   operation.audioOutputs.data(),
                                   static_cast<uint32_t>(operation.midiInputs.size()),
                                   static_cast<uint32_t>(operation.midiOutputs.size()),
                                   operation.midiInputs.data(),
                                   operation.midiOutputs.data()};
            operation.processor.api->process(operation.processor.instance, &block);
            for (std::size_t index = 0; index < operation.midiOutputSlots.size(); ++index)
            {
                auto &output = operation.midiOutputs[index];
                if (output.eventCount > output.eventCapacity ||
                    output.byteCount > output.byteCapacity)
                {
                    output.eventCount = output.byteCount = 0;
                    ++output.dropped;
                }
                state_->buffers[operation.midiOutputSlots[index]].output = output;
            }
        }
    }

  private:
    struct Buffer
    {
        std::vector<float> samples;
        std::vector<float *> channels;
        std::vector<WjnNodeMidiEventV1> events;
        std::vector<uint8_t> bytes;
        WjnNodeMidiOutputV1 output{};
    };
    struct Operation
    {
        Processor processor;
        std::vector<std::pair<std::size_t, std::size_t>> incoming;
        std::vector<WjnNodeAudioBufferV1> audioInputs;
        std::vector<WjnNodeAudioBufferV1> audioOutputs;
        std::vector<WjnNodeMidiInputV1> midiInputs;
        std::vector<WjnNodeMidiOutputV1> midiOutputs;
        std::vector<std::size_t> midiInputSlots;
        std::vector<std::size_t> midiOutputSlots;
    };
    struct State
    {
        uint32_t maxFrames{};
        std::map<std::string, std::size_t> portSlots;
        std::vector<Buffer> buffers;
        std::vector<Operation> operations;
        std::vector<std::size_t> schedule;

        bool compile(const Topology &topology, uint32_t frames, std::string &error)
        {
            const auto fail = [&error](const char *message) {
                error = message;
                return false;
            };
            if (!frames || frames > 65536)
            {
                return fail("Invalid virtual block capacity");
            }
            maxFrames = frames;
            std::map<std::string, std::size_t> owners;
            for (const auto &processor : topology.processors)
            {
                if (processor.id.empty() || !processor.instance || !processor.api ||
                    !processor.api->process ||
                    !owners.emplace(processor.id, operations.size()).second)
                {
                    return fail("Invalid or duplicate virtual processor");
                }
                operations.push_back({processor});
            }
            buffers.resize(topology.ports.size());
            std::vector<std::size_t> portOwners;
            for (std::size_t index = 0; index < topology.ports.size(); ++index)
            {
                const auto &port = topology.ports[index];
                if (port.id.empty() || !portSlots.emplace(port.id, index).second ||
                    (port.type != WJN_NODE_PORT_AUDIO && port.type != WJN_NODE_PORT_MIDI) ||
                    (port.type == WJN_NODE_PORT_AUDIO && (!port.channels || port.channels > 64)) ||
                    (port.type == WJN_NODE_PORT_MIDI && port.channels) ||
                    (port.direction != WJN_NODE_PORT_INPUT &&
                     port.direction != WJN_NODE_PORT_OUTPUT))
                {
                    return fail("Invalid or duplicate virtual port");
                }
                const auto ownerId = port.junction ? port.id : port.owner;
                auto owner = owners.find(ownerId);
                if (port.junction && owner == owners.end())
                {
                    owner = owners.emplace(ownerId, operations.size()).first;
                    operations.emplace_back();
                }
                if (owner == owners.end())
                {
                    return fail("Virtual port has no processor");
                }
                portOwners.push_back(owner->second);
                auto &buffer = buffers[index];
                if (port.type == WJN_NODE_PORT_AUDIO)
                {
                    buffer.samples.resize(static_cast<std::size_t>(frames) * port.channels);
                    for (uint32_t channel = 0; channel < port.channels; ++channel)
                    {
                        buffer.channels.push_back(buffer.samples.data() + channel * frames);
                    }
                }
                else
                {
                    buffer.events.resize(midiEvents);
                    buffer.bytes.resize(midiBytes);
                    buffer.output = {port.apiId,
                                     midiEvents,
                                     buffer.events.data(),
                                     buffer.bytes.data(),
                                     midiBytes,
                                     0,
                                     0,
                                     0,
                                     0};
                }
                if (port.junction)
                {
                    continue;
                }
                auto &operation = operations[owner->second];
                const bool input = port.direction == WJN_NODE_PORT_INPUT;
                if (port.type == WJN_NODE_PORT_AUDIO)
                {
                    auto &ports = input ? operation.audioInputs : operation.audioOutputs;
                    ports.push_back({port.apiId, port.channels, buffer.channels.data()});
                }
                else if (input)
                {
                    operation.midiInputs.push_back({port.apiId, 0, buffer.events.data()});
                    operation.midiInputSlots.push_back(index);
                }
                else
                {
                    operation.midiOutputs.push_back(buffer.output);
                    operation.midiOutputSlots.push_back(index);
                }
            }
            std::vector<std::vector<std::size_t>> successors(operations.size());
            std::vector<std::size_t> incoming(operations.size());
            for (const auto &route : topology.routes)
            {
                const auto source = portSlots.find(route.source),
                           destination = portSlots.find(route.destination);
                if (source == portSlots.end() || destination == portSlots.end())
                {
                    return fail("Virtual route references a missing port");
                }
                const auto &from = topology.ports[source->second];
                const auto &to = topology.ports[destination->second];
                if ((!from.junction && from.direction != WJN_NODE_PORT_OUTPUT) ||
                    (!to.junction && to.direction != WJN_NODE_PORT_INPUT) || from.type != to.type ||
                    from.channels != to.channels)
                {
                    return fail("Virtual route direction or buffer type mismatch");
                }
                const auto fromOwner = portOwners[source->second],
                           toOwner = portOwners[destination->second];
                successors[fromOwner].push_back(toOwner);
                ++incoming[toOwner];
                operations[toOwner].incoming.emplace_back(source->second, destination->second);
            }
            for (std::size_t index = 0; index < incoming.size(); ++index)
            {
                if (!incoming[index])
                {
                    schedule.push_back(index);
                }
            }
            for (std::size_t index = 0; index < schedule.size(); ++index)
            {
                for (const auto successor : successors[schedule[index]])
                {
                    if (!--incoming[successor])
                    {
                        schedule.push_back(successor);
                    }
                }
            }
            return schedule.size() == operations.size() ||
                   fail("Zero-latency virtual cycle rejected");
        }
    };

    static bool append(WjnNodeMidiOutputV1 &output, const WjnNodeMidiEventV1 &event,
                       uint32_t frames) noexcept
    {
        if (!event.data || !event.size || event.frame >= frames ||
            output.eventCount >= output.eventCapacity ||
            event.size > output.byteCapacity - output.byteCount)
        {
            ++output.dropped;
            return false;
        }
        auto *bytes = output.bytes + output.byteCount;
        std::memcpy(bytes, event.data, event.size);
        output.byteCount += event.size;
        auto position = output.eventCount++;
        while (position && output.events[position - 1].frame > event.frame)
        {
            output.events[position] = output.events[position - 1];
            --position;
        }
        output.events[position] = {event.frame, event.size, bytes};
        return true;
    }

    std::unique_ptr<State> state_;
};
}
