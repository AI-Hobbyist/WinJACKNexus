// SPDX-License-Identifier: AGPL-3.0-only
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <WinJACKNexus/Common/MidiToolsCore.hpp>
#include <WinJACKNexus/Common/MidiToolsRename.hpp>
#include <WinJACKNexus/Node/INode.hpp>
#include <iostream>
#include <stdexcept>
using namespace WinJACKNexus::Common::MidiTools;
void check(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
struct Model final : ClientBackend
{
    std::string name = "old";
    bool snapshotOk = true, createOk = true, rollbackOk = true;
    int snapshots{}, replacements{};
    std::vector<Connection> restored;
    std::vector<Connection> saved{
        {"peer:out", "old:MIDI_IN"}, {"old:OUT_L", "peer:in"}, {"old:OUT_R", "old:MIDI_IN"}};
    std::string actualName() const override
    {
        return name;
    }
    bool validName(const std::string &s) const override
    {
        return !s.empty() && s.find(':') == std::string::npos;
    }
    bool snapshot(const std::vector<PortIdentity> &, std::vector<Connection> &out) override
    {
        ++snapshots;
        out = saved;
        return snapshotOk;
    }
    bool replace(const std::string &s, const std::vector<PortIdentity> &) override
    {
        ++replacements;
        if ((s != "old" && !createOk) || (s == "old" && !rollbackOk))
        {
            name.clear();
            return false;
        }
        name = s == "conflict" ? "conflict-01" : s;
        return true;
    }
    bool connect(const Connection &c) override
    {
        if (c.destination == "missing:in")
        {
            return false;
        }
        restored.push_back(c);
        return true;
    }
};
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        check(argc == 2, "runtime root argument");
        for (const auto kind : {"sf2_loader", "midi_spilit", "midi_matrix"})
        {
            const QString base = QString::fromUtf8(argv[1]) + "/midi_tools/" + kind;
            const auto path = base + "/mod_" + kind + ".dll";
            const HMODULE dll = LoadLibraryW(reinterpret_cast<const wchar_t *>(path.utf16()));
            check(dll != nullptr, "load real node DLL without JACK");
            const auto version = reinterpret_cast<const char *(*)()>(
                GetProcAddress(dll, "wjn_get_node_api_version"));
            const auto category = reinterpret_cast<const char *(*)(void)>(
                GetProcAddress(dll, "wjn_get_node_category"));
            const auto descriptor = reinterpret_cast<const char *(*)(void)>(
                GetProcAddress(dll, "wjn_get_node_i18n_descriptor_json"));
            const auto create = reinterpret_cast<WinJACKNexus::Node::INode *(*)(void)>(
                GetProcAddress(dll, "wjn_create_node"));
            const auto destroy = reinterpret_cast<void (*)(WinJACKNexus::Node::INode *)>(
                GetProcAddress(dll, "wjn_destroy_node"));
            check(version && category && descriptor && create && destroy, "required exports");
            check(std::string(version()) == "3" && std::string(category()) == "Midi Tools",
                  "identity");
            check(GetProcAddress(dll, "wjn_get_node_count") == nullptr,
                  "retired count export is absent");
            const auto desc = QJsonDocument::fromJson(descriptor()).object();
            QFile manifest(base + "/locate/manifest.json");
            check(manifest.open(QIODevice::ReadOnly), "independent manifest path");
            check(QJsonDocument::fromJson(manifest.readAll()).object()["moduleId"] ==
                      desc["moduleId"],
                  "manifest identity");
            for (const auto lang : {"en-US", "zh-CN", "ja-JP", "ko-KR"})
            {
                QFile f(base + "/locate/" + lang + ".json");
                check(f.open(QIODevice::ReadOnly), "catalog deployed");
                const auto catalog = QJsonDocument::fromJson(f.readAll()).object();
                check(catalog["moduleId"] == desc["moduleId"] &&
                          catalog["language"].toString() == lang,
                      "catalog identity");
            }
            auto *a = create();
            auto *b = create();
            check(a && b && a != b, "independent instances");
            // Do not initialize implemented JACK clients in offline loading tests.
            a->shutdown();
            b->shutdown();
            destroy(a);
            destroy(b);
            check(FreeLibrary(dll), "unload");
            std::cout << "PASS real DLL foundation " << kind << '\n';
        }
        for (unsigned channel = 0; channel < 16; ++channel)
        {
            for (unsigned type = 8; type <= 14; ++type)
            {
                const uint8_t bytes[]{uint8_t(type * 16 + channel), 60, 100};
                const auto length = (type == 12 || type == 13) ? 2 : 3;
                check(splitTargets({bytes, static_cast<size_t>(length)}) == (1u << channel),
                      "channel routing classification");
            }
        }
        const uint8_t sys[] = {0xf0, 1, 2, 0xf7}, bad[] = {0x90, 60, 0x80};
        check(splitTargets(sys) == 0xffff && !validMessage(bad), "system and invalid data");
        check(!validMessage(std::span<const uint8_t>{}), "empty event");
        check(validEvent({127, sys}, 128) && !validEvent({128, sys}, 128), "frame boundary");
        const std::vector<PortIdentity> ports{{"MIDI_IN", true, PortType::midi},
                                              {"OUT_L", false, PortType::audio},
                                              {"OUT_R", false, PortType::audio}};
        Model m;
        auto result = renameClient(m, ports, "new");
        check(result.status == RenameResult::applied && m.restored.size() == 3,
              "all input/output connections");
        check(m.restored[2].source == "new:OUT_R" && m.restored[2].destination == "new:MIDI_IN",
              "self mapping");
        m = Model{};
        check(renameClient(m, ports, "old").status == RenameResult::unchanged && m.snapshots == 0,
              "same name no op");
        check(renameClient(m, ports, "").status == RenameResult::invalidName && m.replacements == 0,
              "invalid no destruction");
        m.snapshotOk = false;
        check(renameClient(m, ports, "new").status == RenameResult::snapshotFailed &&
                  m.replacements == 0,
              "snapshot protection");
        m = Model{};
        m.createOk = false;
        check(renameClient(m, ports, "new").status == RenameResult::createFailed &&
                  m.restored.size() == 3 && m.name == "old",
              "rollback");
        m = Model{};
        m.createOk = m.rollbackOk = false;
        check(renameClient(m, ports, "new").status == RenameResult::rollbackFailed,
              "rollback failure");
        m = Model{};
        check(renameClient(m, ports, "conflict").actualName == "conflict-01",
              "actual conflict result");
        m = Model{};
        m.saved.push_back({"old:OUT_L", "missing:in"});
        result = renameClient(m, ports, "new");
        check(result.status == RenameResult::partial && result.failed.size() == 1 &&
                  m.restored.size() == 3,
              "partial retained failure");
        std::cout << "PASS MIDI parser and control-thread rename transaction boundaries\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
