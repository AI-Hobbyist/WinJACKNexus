/* SPDX-License-Identifier: AGPL-3.0-only */
#include "Service.hpp"
#include <QCoreApplication>
#include <QLibrary>
#include <WinJACKNexus/Node/NodeApi.h>
#include <array>
#include <cstdio>
#include <vector>

namespace SS = WinJACKNexus::SignalSwitch;
namespace DSP = WinJACKNexus::Common::SignalSwitch;

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    int failures = 0;
    auto check = [&](bool passed, const char *name) {
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", name);
        failures += !passed;
    };
    auto &service = SS::Service::instance();
    for (const auto &kind : {"mono", "stereo", "5_1", "7_1", "midi"})
    {
        std::printf("MODULE %s\n", kind);
        QLibrary library(QCoreApplication::applicationDirPath() + "/signal_switch/" + kind + "/wjn_signal_switch_" +
                         kind + ".dll");
        check(library.load(), "load module");
        if (!library.isLoaded())
        {
            std::printf("%s\n", library.errorString().toUtf8().constData());
            continue;
        }
        auto metadata = reinterpret_cast<int32_t (*)(WjnNodeMetadataV4 *)>(library.resolve("wjn_get_node_metadata_v4"));
        auto create = reinterpret_cast<WinJACKNexus::Node::INode *(*)(uint32_t)>(library.resolve("wjn_create_node_v4"));
        auto destroy = reinterpret_cast<void (*)(WinJACKNexus::Node::INode *)>(library.resolve("wjn_destroy_node"));
        auto getApi =
            reinterpret_cast<const WjnNodeVirtualApiV1 *(*)()>(library.resolve("wjn_get_node_virtual_api_v1"));
        auto settings = reinterpret_cast<const char *(*)(WinJACKNexus::Node::INode *)>(
            library.resolve("wjn_get_node_settings_json"));
        auto restore = reinterpret_cast<int (*)(WinJACKNexus::Node::INode *, const char *)>(
            library.resolve("wjn_set_node_settings_json"));
        check(metadata && create && destroy && getApi && settings && restore, "required exports");
        if (!metadata || !create || !destroy || !getApi || !settings || !restore)
            continue;
        WjnNodeMetadataV4 info{sizeof(info)};
        check(metadata(&info) && info.capabilities == 3 && info.features == 0, "dual mode and no editor");
        check(!create(0) && !create(3), "invalid factory mode rejected");
        auto *jack = create(WJN_NODE_MODE_JACK);
        check(jack && !jack->createWindow(nullptr), "JACK instance creation without server or GUI");
        destroy(jack);
        auto *node = create(WJN_NODE_MODE_VIRT);
        auto *api = getApi();
        check(node && api && service.endpoints.contains(node), "shared main module registry");
        if (!node || !api)
            continue;
        check(!service.endpoints[node].desired, "new switch defaults off");
        WjnNodePrepareV1 prepare{sizeof(prepare), 64, 48000, nullptr};
        check(api->prepare(node, &prepare), "offline prepare");
        const unsigned portCount = api->portCount(node);
        bool descriptors = portCount >= 2;
        for (unsigned index = 0; index < portCount; ++index)
        {
            WjnNodePortV1 port{sizeof(port)};
            descriptors &= api->port(node, index, &port) && port.id == index + 1 && port.name;
        }
        check(descriptors, "stable port descriptors");
        const auto id = service.endpoints[node].id;
        const bool midi = QByteArray(kind) == "midi";
        WjnNodeProcessV1 block{};
        block.structSize = sizeof(block);
        block.frames = 64;
        if (!midi)
        {
            const unsigned channels = portCount / 2;
            std::array<std::array<float, 64>, 8> input{}, output{};
            std::array<float *, 8> inputPointers{}, outputPointers{};
            std::array<WjnNodeAudioBufferV1, 8> inputs{}, outputs{};
            for (unsigned channel = 0; channel < channels; ++channel)
            {
                input[channel].fill(float(channel + 1) / 10);
                output[channel].fill(99);
                inputPointers[channel] = input[channel].data();
                outputPointers[channel] = output[channel].data();
                inputs[channel] = {channel + 1, 1, &inputPointers[channel]};
                outputs[channel] = {channels + channel + 1, 1, &outputPointers[channel]};
            }
            block.audioInputCount = block.audioOutputCount = channels;
            block.audioInputs = inputs.data();
            block.audioOutputs = outputs.data();
            api->process(node, &block);
            bool silent = true;
            for (unsigned channel = 0; channel < channels; ++channel)
                for (float value : output[channel])
                    silent &= value == 0;
            check(silent, "all channels closed write silence");
            service.setEnabled({id}, true);
            api->process(node, &block);
            bool copied = true;
            for (unsigned channel = 0; channel < channels; ++channel)
                copied &= input[channel] == output[channel];
            check(copied, "all channels open preserve channel mapping");
            inputs[0].samples = nullptr;
            api->process(node, &block);
            check(output[0][0] == 0, "missing input writes zero");
            service.setEnabled({id}, false);
            check(!service.endpoints[node].gate->quiet(), "close waits for callback acknowledgement");
            api->process(node, &block);
            check(service.endpoints[node].gate->quiet(), "completed audio close acknowledged");
        }
        else
        {
            const uint8_t on[]{0x90, 60, 100}, pedal[]{0xb0, 64, 127};
            WjnNodeMidiEventV1 events[]{{7, 3, on}, {9, 3, on}, {11, 3, pedal}};
            WjnNodeMidiInputV1 input{1, 3, events};
            std::array<WjnNodeMidiEventV1, 16> storage{};
            std::array<uint8_t, 128> bytes{};
            WjnNodeMidiOutputV1 output{2, 16, storage.data(), bytes.data(), 128};
            block.midiInputCount = block.midiOutputCount = 1;
            block.midiInputs = &input;
            block.midiOutputs = &output;
            api->process(node, &block);
            check(output.eventCount == 0, "closed MIDI blocks input");
            service.setEnabled({id}, true);
            api->process(node, &block);
            check(output.eventCount == 3 && output.events[0].frame == 7 && output.events[2].frame == 11 &&
                      output.events[0].data[2] == 100,
                  "MIDI preserves data order and frame offsets");
            service.setEnabled({id}, false);
            output.eventCapacity = 1;
            api->process(node, &block);
            check(output.eventCount == 1 && output.events[0].data[0] == 0xb0 && output.events[0].data[2] == 0 &&
                      !service.endpoints[node].gate->quiet(),
                  "bounded cleanup releases sustain and retains pending notes");
            api->process(node, &block);
            check(output.eventCount == 1 && output.events[0].data[0] == 0x80 && !service.endpoints[node].gate->quiet(),
                  "repeated note first release");
            api->process(node, &block);
            check(output.eventCount == 1 && service.endpoints[node].gate->quiet(),
                  "cleanup finishes across bounded output blocks");
            api->process(node, &block);
            check(output.eventCount == 0, "closed MIDI does not repeat cleanup or pass new notes");
            const uint8_t noteOff[]{0x90, 60, 0};
            events[1] = {9, 3, noteOff};
            input.eventCount = 2;
            output.eventCapacity = 16;
            service.setEnabled({id}, true);
            api->process(node, &block);
            service.setEnabled({id}, false);
            api->process(node, &block);
            check(output.eventCount == 0 && service.endpoints[node].gate->quiet(),
                  "velocity-zero Note On releases tracked note");
        }
        const auto saved = QByteArray(settings(node));
        check(restore(node, saved.constData()) && !restore(node, "{\"version\":99}"),
              "node settings restore and reject malformed");
        api->reset(node);
        api->stop(node);
        node->shutdown();
        destroy(node);
        check(service.endpoints.isEmpty(), "destruction unregisters service endpoint");
        library.unload();
    }
    DSP::Gate first, second;
    first.running = true;
    second.running = true;
    service.attach(&first, {"a", "A", "main", "mono", &first});
    service.attach(&second, {"b", "B", "sub", "mono", &second});
    service.state.setExclusive("pair", {"a", "b"});
    service.setEnabled({"a"}, true);
    service.setEnabled({"b"}, true);
    check(!(second.command.load() & 1) && !(first.command.load() & 1), "cross-domain break before make");
    first.acknowledged = first.command.load();
    service.reconcile();
    check(second.command.load() & 1, "acknowledgement permits new exclusive member");
    service.setEnabled({"a"}, true);
    second.running = false;
    service.reconcile();
    check(first.command.load() & 1, "stopped peer does not block handoff");
    service.detach(&first);
    service.detach(&second);
    return failures ? 1 : 0;
}
