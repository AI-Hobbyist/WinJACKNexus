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
#include <WinJACKNexus/Patchbay/Core/GraphModel.hpp>
#include <WinJACKNexus/Patchbay/Core/SearchModel.hpp>
#include <WinJACKNexus/Patchbay/Core/Snapshot.hpp>

#include <WinJACKNexus/Audio/JackApi.hpp>
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Audio/JackGraph.hpp>
#include <WinJACKNexus/Audio/JackPort.hpp>
#include <WinJACKNexus/Audio/JackServerGuard.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

namespace {

using namespace WinJACKNexus::Patchbay::Core;

bool check(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "FAILED: %s\n", message);
    return condition;
}

bool waitForConnections(const WinJACKNexus::Audio::JackPort& port)
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (port.visitConnections([](const char*, void*) noexcept {
                return true;
            }, nullptr) != 0)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

Snapshot testSnapshot()
{
    Snapshot snapshot;
    snapshot.clients = {
        { .id = "system", .name = "system" },
        { .id = "synth", .name = "Synthesizer", .alias = "Lead" }
    };
    snapshot.ports = {
        { .id = "system:capture_1", .clientId = "system", .name = "capture_1", .kind = PortKind::audio, .direction = Direction::output },
        { .id = "synth:midi_in", .clientId = "synth", .name = "midi_in", .alias = "keyboard", .kind = PortKind::midi, .direction = Direction::input }
    };
    snapshot.connections = {
        { .sourcePortId = "system:capture_1", .destinationPortId = "synth:midi_in" }
    };
    return snapshot;
}

bool testGraphModel()
{
    GraphModel model;
    model.replace(testSnapshot());
    bool passed = true;
    passed &= check(model.clientById("synth") != nullptr, "graph finds a client by ID");
    passed &= check(model.portById("system:capture_1") != nullptr, "graph finds a port by ID");
    passed &= check(model.connection("system:capture_1", "synth:midi_in") != nullptr, "graph finds a connection");
    passed &= check(model.setClientSplit("synth", true) && model.clientById("synth")->split,
                    "graph persists split state on a client");
    passed &= check(!model.setClientSplit("missing", true), "graph rejects an unknown client");

    auto changedSnapshot = testSnapshot();
    changedSnapshot.ports.pop_back();
    changedSnapshot.connections.clear();
    changedSnapshot.clients[1].x = 0.0;
    changedSnapshot.clients[1].y = 0.0;
    changedSnapshot.clients[1].split = false;
    model.replace(std::move(changedSnapshot));
    const auto& events = model.snapshot().events;
    passed &= check(events.size() == 2, "graph reports port and connection removal events");
    passed &= check(events[0].type == SnapshotEventType::portRemoved && events[0].subjectId == "synth:midi_in",
                    "graph reports the removed port ID");
    passed &= check(events[1].type == SnapshotEventType::connectionRemoved
                        && events[1].subjectId == "system:capture_1"
                        && events[1].relatedId == "synth:midi_in",
                    "graph reports the removed connection endpoints");
    const auto* refreshedSynth = model.clientById("synth");
    passed &= check(refreshedSynth != nullptr && refreshedSynth->split,
                    "graph preserves split state across a snapshot refresh");
    return passed;
}

bool testSearchModel()
{
    GraphModel model;
    model.replace(testSnapshot());
    bool passed = true;
    const auto clients = SearchModel::matchingClientIds(model, "lead");
    const auto ports = SearchModel::matchingPortIds(model, "KEYBOARD");
    passed &= check(clients.size() == 1 && clients.front() == "synth", "client search matches aliases case-insensitively");
    passed &= check(ports.size() == 1 && ports.front() == "synth:midi_in", "port search matches aliases case-insensitively");
    passed &= check(SearchModel::matchingPortIds(model, "").size() == 2, "empty search returns all ports");
    return passed;
}

bool testJackSnapshot()
{
    using namespace WinJACKNexus::Audio;

    if (!JackApi::instance().load()) {
        std::puts("JACK runtime unavailable; patchbay snapshot check skipped");
        return true;
    }

    JackServerGuard server;
    JackClient sourceClient;
    JackClient destinationClient;
    JackClientOptions sourceOptions;
    sourceOptions.name = "WJNPatchbaySnapshotSource";
    sourceOptions.autoStartServer = false;
    JackClientOptions destinationOptions;
    destinationOptions.name = "WJNPatchbaySnapshotDestination";
    destinationOptions.autoStartServer = false;
    if (!sourceClient.open(sourceOptions)) {
        JackServerOptions serverOptions;
        serverOptions.backend = L"dummy";
        if (!server.start(serverOptions) || !server.waitUntilReady(5000) || !sourceClient.open(sourceOptions)) {
            std::puts("JACK server unavailable; patchbay snapshot check skipped");
            return true;
        }
    }
    if (!destinationClient.open(destinationOptions)) {
        std::puts("JACK destination client unavailable; patchbay snapshot check skipped");
        return true;
    }

    JackPort output;
    JackPort input;
    bool passed = true;
    passed &= check(output.registerPort(sourceClient, "snapshot_out", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput),
                    "JACK snapshot output port registers");
    passed &= check(input.registerPort(destinationClient, "snapshot_in", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput),
                    "JACK snapshot input port registers");
    passed &= check(sourceClient.activate() && destinationClient.activate(), "JACK snapshot clients activate");
    const auto connected = JackGraph::connect(sourceClient, output.name(), input.name());
    passed &= check(connected && waitForConnections(output), "JACK snapshot ports connect");

    const auto snapshot = Snapshot::capture(sourceClient);
    passed &= check(std::any_of(snapshot.clients.cbegin(), snapshot.clients.cend(), [](const ClientInfo& value) {
                        return value.id == "WJNPatchbaySnapshotSource";
                    }),
                    "snapshot contains the JACK client");
    passed &= check(std::any_of(snapshot.ports.cbegin(), snapshot.ports.cend(), [](const PortInfo& value) {
                        return value.id == "WJNPatchbaySnapshotSource:snapshot_out"
                            && value.kind == PortKind::audio
                            && value.direction == Direction::output;
                    }),
                    "snapshot contains the audio output port");
    passed &= check(std::any_of(snapshot.connections.cbegin(), snapshot.connections.cend(), [](const ConnectionInfo& value) {
                        return value.sourcePortId == "WJNPatchbaySnapshotSource:snapshot_out"
                            && value.destinationPortId == "WJNPatchbaySnapshotDestination:snapshot_in";
                    }),
                    "snapshot contains the JACK connection");

    destinationClient.deactivate();
    sourceClient.deactivate();
    destinationClient.close();
    sourceClient.close();
    server.stop();
    return passed;
}

bool testSubGraphInterfacePorts()
{
    using namespace WinJACKNexus::Audio;

    if (!JackApi::instance().load()) {
        std::puts("JACK runtime unavailable; subgraph interface port check skipped");
        return true;
    }

    JackServerGuard server;
    JackClient client;
    JackClientOptions options;
    options.name = "WJNS2InterfaceTests";
    options.autoStartServer = false;
    if (!client.open(options)) {
        JackServerOptions serverOptions;
        serverOptions.backend = L"dummy";
        if (!server.start(serverOptions) || !server.waitUntilReady(5000) || !client.open(options)) {
            std::puts("JACK server unavailable; subgraph interface port check skipped");
            return true;
        }
    }

    JackPort audioIn;
    JackPort audioOut;
    JackPort midiIn;
    JackPort midiOut;
    bool passed = true;
    passed &= check(audioIn.registerPort(client, "sub_s2_audio_in", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput),
                    "S2 audio input interface registers as a JACK input");
    passed &= check(audioOut.registerPort(client, "sub_s2_audio_out", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput),
                    "S2 audio output interface registers as a JACK output");
    passed &= check(midiIn.registerPort(client, "sub_s2_midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput),
                    "S2 MIDI input interface registers as a JACK input");
    passed &= check(midiOut.registerPort(client, "sub_s2_midi_out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput),
                    "S2 MIDI output interface registers as a JACK output");
    passed &= check(client.activate(), "S2 interface client activates");
    const auto audioConnected = JackGraph::connect(client, audioOut.name(), audioIn.name());
    const auto midiConnected = JackGraph::connect(client, midiOut.name(), midiIn.name());
    passed &= check(audioConnected && waitForConnections(audioOut),
                    "S2 audio interface ports connect through JACK");
    passed &= check(midiConnected && waitForConnections(midiOut),
                    "S2 MIDI interface ports connect through JACK");

    const auto snapshot = Snapshot::capture(client);
    const auto groupedPorts = std::count_if(snapshot.ports.cbegin(), snapshot.ports.cend(), [](const auto& port) {
        return port.clientId == "WJNS2InterfaceTests";
    });
    passed &= check(groupedPorts == 4, "S2 interface ports stay in one JACK client group");
    const auto hasPort = [&snapshot](const std::string& id, PortKind kind, Direction direction) {
        return std::any_of(snapshot.ports.cbegin(), snapshot.ports.cend(), [&id, kind, direction](const auto& port) {
            return port.id == id && port.kind == kind && port.direction == direction;
        });
    };
    passed &= check(hasPort("WJNS2InterfaceTests:sub_s2_audio_in", PortKind::audio, Direction::input),
                    "snapshot preserves the S2 audio input interface semantics");
    passed &= check(hasPort("WJNS2InterfaceTests:sub_s2_audio_out", PortKind::audio, Direction::output),
                    "snapshot preserves the S2 audio output interface semantics");
    passed &= check(hasPort("WJNS2InterfaceTests:sub_s2_midi_in", PortKind::midi, Direction::input),
                    "snapshot preserves the S2 MIDI input interface semantics");
    passed &= check(hasPort("WJNS2InterfaceTests:sub_s2_midi_out", PortKind::midi, Direction::output),
                    "snapshot preserves the S2 MIDI output interface semantics");

    audioIn.unregisterPort();
    audioOut.unregisterPort();
    midiIn.unregisterPort();
    midiOut.unregisterPort();
    const auto cleanedSnapshot = Snapshot::capture(client);
    passed &= check(std::none_of(cleanedSnapshot.ports.cbegin(), cleanedSnapshot.ports.cend(), [](const auto& port) {
                        return port.clientId == "WJNS2InterfaceTests";
                    }),
                    "grouped Sub Graph interface ports unregister with the node lifetime");
    client.deactivate();
    client.close();
    server.stop();
    return passed;
}

}

int main()
{
    bool passed = true;
    passed &= testGraphModel();
    passed &= testSearchModel();
    passed &= testJackSnapshot();
    passed &= testSubGraphInterfacePorts();
    if (passed)
        std::puts("Patchbay core tests passed");
    return passed ? 0 : 1;
}