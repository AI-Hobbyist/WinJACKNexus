// SPDX-License-Identifier: AGPL-3.0-only
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLibrary>
#include <WinJACKNexus/Common/ClassicMixer.hpp>
#include <WinJACKNexus/Common/ClassicMixerRouting.hpp>
#include <WinJACKNexus/Node/ModuleCompatibility.hpp>
#include <cmath>
#include <iostream>
#include <map>
#include <mod_patchbay/VirtualGraph.hpp>
#include <stdexcept>

namespace CM = WinJACKNexus::Common::ClassicMixer;
namespace Node = WinJACKNexus::Node;
QString mixerModule(int layout)
{
    switch (layout)
    {
    case 8:
        return QString::fromUtf8(CLASSIC_MIXER_DLL_8);
    case 16:
        return QString::fromUtf8(CLASSIC_MIXER_DLL_16);
    default:
        return QString::fromUtf8(CLASSIC_MIXER_DLL_24);
    }
}
int checks = 0;
void check(bool value, const char *message)
{
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
struct FakePorts : CM::PortBackend
{
    std::map<uint32_t, QString> names;
    std::vector<CM::Connection> connections;
    int renameCalls = 0;
    int failRename = -1;
    bool snapshotFails = false;
    bool connectFails = false;
    QString clientName() const override
    {
        return "Mixer";
    }
    bool snapshot(std::vector<CM::Connection> &result) override
    {
        result = connections;
        return !snapshotFails;
    }
    bool rename(uint32_t id, const QString &name) override
    {
        if (++renameCalls == failRename)
            return false;
        names[id] = name;
        return true;
    }
    bool connect(const CM::Connection &) override
    {
        return !connectFails;
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        for (int layout : {8, 16, 24})
        {
            const auto path = mixerModule(layout);
            QLibrary library(path);
            check(library.load(), "actual module loads without JACK");
            Node::ModuleContract contract;
            std::string reason;
            check(Node::inspectNodeModule(GetModuleHandleW(reinterpret_cast<LPCWSTR>(
                                              QFileInfo(path).fileName().utf16())),
                                          contract) == Node::ModuleError::none,
                  "actual v4 contract validates");
            check(contract.metadata.capabilities == (WJN_NODE_JACK_NODE | WJN_NODE_VIRT_NODE),
                  "dual-mode capability");
            const auto create = reinterpret_cast<decltype(&wjn_create_node_v4)>(
                library.resolve("wjn_create_node_v4"));
            const auto destroy =
                reinterpret_cast<decltype(&wjn_destroy_node)>(library.resolve("wjn_destroy_node"));
            const auto get = reinterpret_cast<decltype(&wjn_get_node_settings_json)>(
                library.resolve("wjn_get_node_settings_json"));
            const auto set = reinterpret_cast<decltype(&wjn_set_node_settings_json)>(
                library.resolve("wjn_set_node_settings_json"));
            check(create && destroy && get && set, "required factories/settings");
            check(!create(0) && !create(3), "invalid modes rejected");
            auto *jack = create(WJN_NODE_MODE_JACK);
            check(jack != nullptr && std::string(jack->info().category) == "Classic Mixer",
                  "JACK instance creation is offline");
            // Deliberately do not call JACK init in an offline test.
            destroy(jack);
            auto *first = create(WJN_NODE_MODE_VIRT);
            auto *second = create(WJN_NODE_MODE_VIRT);
            check(first && second && first != second, "independent instances");
            const auto initial = QByteArray(get(first));
            auto wrong = CM::State::create(layout == 8 ? 16 : 8);
            auto wrongJson = wrong.toJson();
            wrongJson.insert("clientName", "Wrong");
            check(!set(first, QJsonDocument(wrongJson).toJson().constData()) &&
                      QByteArray(get(first)) == initial,
                  "fixed DLL rejects another layout atomically before prepare");
            check(QJsonDocument::fromJson(initial).object().value("layoutChosen").toBool(),
                  "new fixed-layout instance requires no chooser");
            auto state = CM::State::create(layout);
            state.layoutChosen = true;
            state.inputs[0].pan = -1;
            state.inputs[0].name = QString::fromUtf8("输入 보컬");
            check(state.savePreset("Embedded"), "preset snapshot");
            auto json = state.toJson();
            json.insert("clientName", "CustomMixer");
            const auto bytes = QJsonDocument(json).toJson(QJsonDocument::Compact);
            check(set(first, bytes.constData()), "restore before virtual prepare");
            check(QJsonDocument::fromJson(get(first)).object() == json,
                  "real DLL settings round trip");
            check(QJsonDocument::fromJson(get(second)).object().value("layoutId") ==
                      QJsonValue(layout),
                  "other instance unchanged");
            WjnNodePrepareV1 prepare{sizeof(prepare), 256, 48000, nullptr};
            check(contract.processing->prepare(first, &prepare), "actual virtual prepare");
            check(contract.processing->portCount(first) == layout + 8, "virtual channel count");
            std::vector<WinJACKNexus::Patchbay::VirtualGraph::Port> graphPorts;
            for (uint32_t index = 0; index < contract.processing->portCount(first); ++index)
            {
                WjnNodePortV1 port{};
                port.structSize = sizeof(port);
                check(contract.processing->port(first, index, &port) && port.channels == 1 &&
                          port.name,
                      "actual port description");
                graphPorts.push_back({"root/child/mixer:port-" + std::to_string(port.id),
                                      "root/child/mixer", port.id, port.type, port.channels,
                                      port.direction, false});
            }
            WinJACKNexus::Patchbay::VirtualGraph graph;
            WinJACKNexus::Patchbay::VirtualGraph::Topology topology;
            topology.ports = graphPorts;
            topology.ports.push_back(
                {"root/interface", {}, 0, WJN_NODE_PORT_AUDIO, 1, WJN_NODE_PORT_INPUT, true});
            topology.routes.push_back({"root/interface", "root/child/mixer:port-1"});
            topology.processors.push_back({"root/child/mixer", first, contract.processing});
            check(graph.rebuild(topology, 256, reason),
                  "production VirtualGraph compiles nested identity");
            check(graph.begin(256), "graph begins");
            std::fill_n(graph.audio(graph.slot("root/interface")), 256, 0.5f);
            graph.process(256, 0);
            check(std::abs(graph.audio(graph.slot("root/child/mixer:port-101"))[10] - 0.5f) < 1e-6,
                  "actual DLL through production graph");
            const auto original = QJsonDocument::fromJson(get(first)).object();
            auto renamed = original;
            auto restored = state;
            restored.inputs[0].name = "Renamed";
            renamed = restored.toJson();
            renamed.insert("clientName", "RenamedNode");
            check(set(first, QJsonDocument(renamed).toJson().constData()), "live VIRT rename");
            graph.begin(256);
            std::fill_n(graph.audio(graph.slot("root/interface")), 256, 0.25f);
            graph.process(256, 256);
            check(std::abs(graph.audio(graph.slot("root/child/mixer:port-101"))[10] - 0.25f) < 1e-6,
                  "rename retains compiled graph routes");
            auto broken = renamed;
            broken.insert("masterFader", "bad");
            check(!set(first, QJsonDocument(broken).toJson().constData()),
                  "bad actual state rejected");
            check(QJsonDocument::fromJson(get(first)).object() == renamed,
                  "failed actual state atomic");
            contract.processing->stop(first);
            first->shutdown();
            destroy(first);
            destroy(second);
            auto *reopened = create(WJN_NODE_MODE_VIRT);
            check(set(reopened, QJsonDocument(renamed).toJson().constData()),
                  "recreate restores embedded preset/name");
            check(QJsonDocument::fromJson(get(reopened)).object() == renamed,
                  "recreated full state");
            destroy(reopened);
            check(library.unload(), "module unload");
        }
        auto before = CM::ports(CM::State::create(8));
        auto after = before;
        after[6].name = "Stereo Changed_L";
        after[7].name = "Stereo Changed_R";
        FakePorts backend;
        for (const auto &port : before)
            backend.names[port.id] = port.name;
        backend.connections = {{"External:out", "Mixer:Stereo 1_L"},
                               {"Mixer:Master_R", "Mixer:Stereo 1_R"}};
        check(CM::renamePorts(backend, before, after).success, "stereo rename transaction");
        check(backend.names[after[6].id] == after[6].name &&
                  backend.names[after[7].id] == after[7].name,
              "both stereo names commit");
        check(CM::mappedEndpoint("Mixer:Stereo 1_R", "Mixer", before, after) ==
                  "Mixer:Stereo Changed_R",
              "self endpoint remap");
        backend.failRename = backend.renameCalls + 4;
        const auto failed = CM::renamePorts(backend, after, before);
        check(!failed.success && failed.rollbackComplete,
              "second stereo target failure rolls back");
        check(backend.names[after[6].id] == after[6].name &&
                  backend.names[after[7].id] == after[7].name,
              "rollback restores both names");
        backend.snapshotFails = true;
        const int calls = backend.renameCalls;
        check(!CM::renamePorts(backend, after, before).success && backend.renameCalls == calls,
              "snapshot failure no mutation");
        backend.snapshotFails = false;
        backend.connectFails = true;
        const auto partial = CM::renamePorts(backend, after, before);
        check(!partial.success && !partial.rollbackComplete && !partial.failedConnections.empty(),
              "partial reconnection reported");
        std::cout << "PASS " << checks << " offline module/routing checks\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
