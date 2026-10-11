// SPDX-License-Identifier: AGPL-3.0-only
#include <WinJACKNexus/Node/ModuleCompatibility.hpp>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace Node = WinJACKNexus::Node;
extern "C" int wjn_node_v4_c_abi(void);
static_assert(std::is_same_v<decltype(&wjn_create_node_v4), Node::INode *(*)(uint32_t)>);
static_assert(sizeof(WjnNodeMetadataV4) == 24);

void require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
struct Library
{
    HMODULE handle;
    explicit Library(const std::filesystem::path &path) : handle(LoadLibraryW(path.c_str()))
    {
        require(handle != nullptr, "fixture/actual module DLL loads");
    }
    ~Library()
    {
        FreeLibrary(handle);
    }
};
void processingTest(const Node::ModuleContract &contract, Node::INode *node)
{
    const auto *api = contract.processing;
    WjnNodePrepareV1 context{sizeof(context), 16, 48000, nullptr};
    require(api->prepare(node, &context) == 1, "offline VIRT prepare");
    WjnNodePortV1 description{};
    description.structSize = sizeof(description);
    require(api->portCount(node) == 4 && api->port(node, 0, &description) == 1 &&
                description.id == 1 && description.direction == WJN_NODE_PORT_INPUT &&
                description.type == WJN_NODE_PORT_AUDIO && description.channels == 1,
            "stable logical port describes audio input");
    require(api->port(node, 2, &description) == 1 && description.id == 3 &&
                description.type == WJN_NODE_PORT_MIDI && description.channels == 0,
            "MIDI logical ports do not advertise audio channels");
    std::array<float, 16> input{}, output{};
    input[3] = 0.5f;
    float *inputs[]{input.data()}, *outputs[]{output.data()};
    WjnNodeAudioBufferV1 audioIn{1, 1, inputs}, audioOut{2, 1, outputs};
    const uint8_t bytes[]{0x90, 60, 100};
    WjnNodeMidiEventV1 event{7, sizeof(bytes), bytes};
    WjnNodeMidiInputV1 midiIn{3, 1, &event};
    std::array<WjnNodeMidiEventV1, 1> events{};
    std::array<uint8_t, 3> storage{};
    WjnNodeMidiOutputV1 midiOut{4, 1, events.data(), storage.data(), 3, 0, 0, 0, 0};
    WjnNodeProcessV1 block{sizeof(block), 16, 987, 1,       1,       &audioIn,
                           &audioOut,     1,  1,   &midiIn, &midiOut};
    api->process(node, &block);
    require(output == input && midiOut.eventCount == 1 && events[0].frame == 7 &&
                events[0].data == storage.data() && storage[2] == 100,
            "audio and offset MIDI use caller-owned buffers");
    midiOut.byteCapacity = 2;
    api->process(node, &block);
    require(midiOut.eventCount == 0 && midiOut.dropped == 1,
            "bounded MIDI payload overflow drops newest and counts it");
    block.audioInputCount = block.midiInputCount = 0;
    api->process(node, &block);
    require(output == std::array<float, 16>{} && midiOut.eventCount == 0,
            "absent input clears outputs");
    api->reset(node);
    api->stop(node);
}
int main(int argc, char **argv)
{
    try
    {
        require(argc == 2 && wjn_node_v4_c_abi() == 1, "runtime path and C ABI");
        const std::filesystem::path runtime(argv[1]);
        struct Case
        {
            const char *name;
            Node::ModuleError error;
            uint32_t capabilities;
        };
        const Case cases[]{{"jack", Node::ModuleError::none, 1},
                           {"virt", Node::ModuleError::none, 2},
                           {"dual", Node::ModuleError::none, 3},
                           {"zero", Node::ModuleError::capabilities, 0},
                           {"unknown", Node::ModuleError::capabilities, 4},
                           {"old", Node::ModuleError::version, 3},
                           {"missing", Node::ModuleError::metadata, 3},
                           {"incomplete", Node::ModuleError::processing, 3}};
        for (const auto &test : cases)
        {
            Library library(runtime / "tests/node-v4" /
                            (std::string("wjn_v4_fixture_") + test.name + ".dll"));
            const auto counter = reinterpret_cast<uint32_t (*)(uint32_t)>(
                GetProcAddress(library.handle, "wjn_fixture_counter"));
            require(counter != nullptr, "fixture counters available");
            Node::ModuleContract contract;
            const auto error = Node::inspectNodeModule(library.handle, contract);
            require(error == test.error && counter(0) == 0 && counter(1) == 0 && counter(2) == 0,
                    "creation-free scan classifies exact ABI failure without JACK effects");
            for (uint32_t mode : {0u, 1u, 2u, 3u, 4u})
            {
                auto *node = Node::createNodeForMode(library.handle, mode);
                const bool supported = error == Node::ModuleError::none &&
                                       (mode == 1 || mode == 2) && (test.capabilities & mode);
                require(bool(node) == supported, "mode and capability checks reject bypasses");
                if (node)
                {
                    if (mode == WJN_NODE_MODE_VIRT)
                    {
                        const auto previousJack = counter(2);
                        processingTest(contract, node);
                        require(counter(2) == previousJack,
                                "VIRT prepare/process never registers JACK");
                    }
                    node->shutdown();
                    const auto destroy = reinterpret_cast<decltype(&wjn_destroy_node)>(
                        GetProcAddress(library.handle, "wjn_destroy_node"));
                    destroy(node);
                }
            }
            std::cout << "PASS " << test.name << ": " << Node::moduleErrorText(error) << '\n';
        }
        unsigned modules = 0;
        for (const auto folder : {"nodes", "midi_tools", "examples/node", "examples/native_fx"})
        {
            const auto directory = runtime / folder;
            if (!std::filesystem::exists(directory))
            {
                continue;
            }
            for (const auto &entry : std::filesystem::recursive_directory_iterator(directory))
            {
                if (entry.path().extension() != ".dll")
                {
                    continue;
                }
                Library library(entry.path());
                if (!GetProcAddress(library.handle, "wjn_get_node_api_version") ||
                    entry.path().filename() == "mod_adapter.dll" ||
                    entry.path().filename() == "mod_meterbridge_probe.dll")
                {
                    continue;
                }
                Node::ModuleContract contract;
                const auto error = Node::inspectNodeModule(library.handle, contract);
                if (error != Node::ModuleError::none)
                {
                    std::cerr << entry.path() << ": " << Node::moduleErrorText(error) << '\n';
                }
                require(error == Node::ModuleError::none &&
                            contract.metadata.capabilities == WJN_NODE_JACK_NODE,
                        "S2 built-ins expose only currently implemented JACK capability");
                require(Node::createNodeForMode(library.handle, WJN_NODE_MODE_VIRT) == nullptr,
                        "unmigrated built-ins cannot create VIRT instances");
                const bool hasGui = entry.path().filename() != "mod_midi_spilit.dll" &&
                                    entry.path().filename() != "wjn_example_node.dll";
                require(contract.metadata.features == (hasGui ? WJN_NODE_HAS_GUI : 0u),
                        "actual modules describe custom GUI support before creation");
                ++modules;
            }
        }
        require(modules >= 25, "all current built-in module families and example discovered");
        std::cout << "PASS C/C++ Node v4 ABI, precreation scan, explicit modes and " << modules
                  << " actual JACK-only modules; no JACK server\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
