// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <WinJACKNexus/Node/ModuleCompatibility.hpp>
#include <array>
#include <stdexcept>

inline void checkVirtualMidiMode(HMODULE library, bool matrix)
{
    namespace Node = WinJACKNexus::Node;
    const auto require = [](bool passed, const char *message) {
        if (!passed)
            throw std::runtime_error(message);
    };
    Node::ModuleContract contract;
    require(Node::inspectNodeModule(library, contract) == Node::ModuleError::none &&
                contract.metadata.capabilities == (WJN_NODE_JACK_NODE | WJN_NODE_VIRT_NODE),
            "MIDI module declares complete dual-mode contract");
    const auto destroy =
        reinterpret_cast<decltype(&wjn_destroy_node)>(GetProcAddress(library, "wjn_destroy_node"));
    auto *node = Node::createNodeForMode(library, WJN_NODE_MODE_VIRT);
    require(node && !node->init(), "VIRT node rejects JACK init");
    const auto *api = contract.processing;
    WjnNodePrepareV1 preparation{sizeof(preparation), 64, 48000, nullptr};
    require(api->prepare(node, &preparation) == 1 && api->portCount(node) == (matrix ? 32u : 17u),
            "VIRT MIDI prepares fixed logical port layout");
    for (uint32_t index = 0; index < api->portCount(node); ++index)
    {
        WjnNodePortV1 port{};
        port.structSize = sizeof(port);
        require(api->port(node, index, &port) == 1 && port.id == index + 1 &&
                    port.type == WJN_NODE_PORT_MIDI && port.channels == 0 &&
                    port.direction ==
                        (index < (matrix ? 16u : 1u) ? WJN_NODE_PORT_INPUT : WJN_NODE_PORT_OUTPUT),
                "MIDI port identity and direction are stable");
    }
    std::array<std::array<WjnNodeMidiEventV1, 64>, 16> events{};
    std::array<std::array<uint8_t, 1024>, 16> payloads{};
    std::array<WjnNodeMidiOutputV1, 16> outputs{};
    for (uint32_t channel = 0; channel < 16; ++channel)
    {
        outputs[channel] = {channel + (matrix ? 17u : 2u),
                            64,
                            events[channel].data(),
                            payloads[channel].data(),
                            1024,
                            0,
                            0,
                            0,
                            0};
    }
    const uint8_t note[]{0x95, 60, 100};
    WjnNodeMidiEventV1 event{17, 3, note};
    WjnNodeMidiInputV1 input{matrix ? 6u : 1u, 1, &event};
    WjnNodeProcessV1 block{};
    block.structSize = sizeof(block);
    block.frames = 64;
    block.samplePosition = 123456;
    block.midiInputCount = 1;
    block.midiInputs = &input;
    block.midiOutputCount = 16;
    block.midiOutputs = outputs.data();
    api->process(node, &block);
    require(outputs[5].eventCount == 1 && events[5][0].frame == 17 &&
                events[5][0].data == payloads[5].data() && payloads[5][2] == 100,
            "VIRT adapter preserves actual core routing, byte payload and time offset");
    api->reset(node);
    outputs[5].byteCapacity = 2;
    api->process(node, &block);
    require(outputs[5].eventCount == 0 && outputs[5].dropped != 0,
            "VIRT MIDI rejects bounded payload overflow");
    api->reset(node);
    block.midiInputCount = 0;
    api->process(node, &block);
    for (const auto &output : outputs)
        require(output.eventCount == 0, "missing VIRT MIDI input clears each output");
    api->stop(node);
    api->process(node, &block);
    for (const auto &output : outputs)
        require(output.eventCount == 0, "stopped VIRT MIDI is silent");
    node->shutdown();
    destroy(node);
}
