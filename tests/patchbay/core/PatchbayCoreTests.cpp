#include <WinJACKNexus/Patchbay/Core/GraphModel.hpp>
#include <WinJACKNexus/Patchbay/Core/SearchModel.hpp>
#include <WinJACKNexus/Patchbay/Core/Snapshot.hpp>

#include <WinJACKNexus/Audio/JackApi.hpp>
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Audio/JackGraph.hpp>
#include <WinJACKNexus/Audio/JackPort.hpp>
#include <WinJACKNexus/Audio/JackServerGuard.hpp>

#include <algorithm>
#include <cstdio>

namespace {

using namespace WinJACKNexus::Patchbay::Core;

bool check(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "FAILED: %s\n", message);
    return condition;
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
    JackClient client;
    JackClientOptions options;
    options.name = "WJNPatchbaySnapshotTests";
    options.autoStartServer = false;
    if (!client.open(options)) {
        JackServerOptions serverOptions;
        serverOptions.backend = L"dummy";
        if (!server.start(serverOptions) || !server.waitUntilReady(5000) || !client.open(options)) {
            std::puts("JACK server unavailable; patchbay snapshot check skipped");
            return true;
        }
    }

    JackPort output;
    JackPort input;
    bool passed = true;
    passed &= check(output.registerPort(client, "snapshot_out", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput),
                    "JACK snapshot output port registers");
    passed &= check(input.registerPort(client, "snapshot_in", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput),
                    "JACK snapshot input port registers");
    passed &= check(client.activate(), "JACK snapshot client activates");
    passed &= check(JackGraph::connect(client, output.name(), input.name()), "JACK snapshot ports connect");

    const auto snapshot = Snapshot::capture(client);
    passed &= check(std::any_of(snapshot.clients.cbegin(), snapshot.clients.cend(), [](const ClientInfo& value) {
                        return value.id == "WJNPatchbaySnapshotTests";
                    }),
                    "snapshot contains the JACK client");
    passed &= check(std::any_of(snapshot.ports.cbegin(), snapshot.ports.cend(), [](const PortInfo& value) {
                        return value.id == "WJNPatchbaySnapshotTests:snapshot_out"
                            && value.kind == PortKind::audio
                            && value.direction == Direction::output;
                    }),
                    "snapshot contains the audio output port");
    passed &= check(std::any_of(snapshot.connections.cbegin(), snapshot.connections.cend(), [](const ConnectionInfo& value) {
                        return value.sourcePortId == "WJNPatchbaySnapshotTests:snapshot_out"
                            && value.destinationPortId == "WJNPatchbaySnapshotTests:snapshot_in";
                    }),
                    "snapshot contains the JACK connection");

    client.deactivate();
    client.close();
    server.stop();
    return passed;
}

}

int main()
{
    const bool passed = testGraphModel() && testSearchModel() && testJackSnapshot();
    if (passed)
        std::puts("Patchbay core tests passed");
    return passed ? 0 : 1;
}