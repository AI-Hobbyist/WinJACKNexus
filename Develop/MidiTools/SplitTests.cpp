// Offline processing + actual DLL settings, no JACK init/server/connection calls.
// SPDX-License-Identifier: AGPL-3.0-only
#include "VirtualModeChecks.hpp"
#include "JackBackend.hpp"
#include <QApplication>
#include <QDir>
#include <QFontInfo>
#include <QGlyphRun>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>
#include <QTextLayout>
#include <WinJACKNexus/Common/MidiToolsSplit.hpp>
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/UI/HostI18n.hpp>
#include <WinJACKNexus/UI/LocalizedText.hpp>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace WinJACKNexus::Common::MidiTools;
void check(bool ok, const char *message)
{
    if (!ok)
    {
        throw std::runtime_error(message);
    }
}
struct Captured
{
    unsigned port, frame;
    std::vector<uint8_t> bytes;
};
struct RenameModel final : ClientBackend
{
    std::string name = "old";
    bool snapshotOk = true, createOk = true, rollbackOk = true;
    std::vector<Connection> saved, restored;
    unsigned replaced = 0;
    std::string actualName() const override
    {
        return name;
    }
    bool validName(const std::string &n) const override
    {
        return !n.empty() && n.find(':') == std::string::npos;
    }
    bool snapshot(const std::vector<PortIdentity> &, std::vector<Connection> &out) override
    {
        out = saved;
        return snapshotOk;
    }
    bool replace(const std::string &n, const std::vector<PortIdentity> &) override
    {
        ++replaced;
        if ((n != "old" && !createOk) || (n == "old" && !rollbackOk))
        {
            name.clear();
            return false;
        }
        name = n == "occupied" ? "occupied-01" : n;
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
void modelTests()
{
    SplitProcessor p;
    std::vector<Captured> captured;
    auto writer = [&](unsigned port, EventView e) {
        captured.push_back({port, e.frame, {e.bytes.begin(), e.bytes.end()}});
        return true;
    };
    for (unsigned channel = 0; channel < 16; ++channel)
    {
        for (unsigned type = 8; type <= 14; ++type)
        {
            const uint8_t b[]{uint8_t(type * 16 + channel), 60, 100};
            const unsigned length = type == 12 || type == 13 ? 2 : 3;
            captured.clear();
            p.process(256, 1, [&](unsigned) { return EventView{255, {b, length}}; }, writer);
            check(captured.size() == 1 && captured[0].port == channel && captured[0].frame == 255 &&
                      captured[0].bytes == std::vector<uint8_t>(b, b + length),
                  "all channel messages preserve bytes/time");
        }
    }
    for (const auto bytes : std::vector<std::vector<uint8_t>>{{0xf0, 1, 2, 0xf7},
                                                              {0xf1, 1},
                                                              {0xf2, 1, 2},
                                                              {0xf3, 1},
                                                              {0xf6},
                                                              {0xf8},
                                                              {0xfa},
                                                              {0xfb},
                                                              {0xfc},
                                                              {0xfe},
                                                              {0xff}})
    {
        captured.clear();
        p.process(256, 1, [&](unsigned) { return EventView{127, bytes}; }, writer);
        check(captured.size() == 16, "system broadcast");
        for (auto &e : captured)
        {
            check(e.bytes == bytes && e.frame == 127, "broadcast exact payload");
        }
    }
    const uint8_t note[]{0x90, 60, 100}, off[]{0x80, 60, 0}, bad[]{0x90, 60, 255};
    captured.clear();
    p.process(
        256, 3,
        [&](unsigned i) {
            return EventView{i == 2 ? 256u : 0u, i == 0 ? std::span<const uint8_t>(bad)
                                                        : std::span<const uint8_t>(note)};
        },
        writer);
    check(captured.size() == 1, "invalid bytes/frame rejected");
    check(p.rejectedEvents() == 2, "malformed payload and frame diagnostics accumulate");
    p.process(
        256, 1, [&](unsigned) { return EventView{0, off}; },
        [](unsigned, EventView) { return false; });
    captured.clear();
    p.process(256, 0, [](unsigned) { return EventView{}; }, writer);
    check(captured.size() == 3 && captured[0].bytes[1] == 64 && captured[2].bytes[1] == 120,
          "failed release retried as bounded panic");
    p.process(256, maxEvents + 1, [&](unsigned) { return EventView{0, note}; }, writer);
    captured.clear();
    p.process(256, 0, [](unsigned) { return EventView{}; }, writer);
    check(captured.size() == 48, "event overflow schedules all-channel release");
    check(p.rejectedEvents() == 3, "event limit adds lifetime diagnostic");
    p.reset();
    check(p.rejectedEvents() == 3, "client restart retains diagnostics");
    const auto ports = WinJACKNexus::MidiTools::splitPorts();
    check(ports.size() == 17 && ports.front().input && ports.back().shortName == "MIDI_OUT_16",
          "fixed port layout");
    RenameModel m;
    m.saved.push_back({"peer:out", "old:MIDI_IN"});
    for (unsigned i = 1; i < ports.size(); ++i)
    {
        m.saved.push_back({"old:" + ports[i].shortName, "peer:in"});
    }
    m.saved.push_back({"old:MIDI_OUT_16", "old:MIDI_IN"});
    const auto saved = m.saved;
    auto result = renameClient(m, ports, "occupied");
    check(result.actualName == "occupied-01" && m.restored.size() == 18,
          "all17 ports and self reconnect actual name");
    check(m.restored.back().destination == "occupied-01:MIDI_IN", "self input remapped");
    m = {};
    m.saved = saved;
    m.createOk = false;
    check(renameClient(m, ports, "new").status == RenameResult::createFailed &&
              m.restored.size() == 18,
          "17port rollback");
    m = {};
    m.saved = saved;
    m.saved.push_back({"old:MIDI_OUT_01", "missing:in"});
    result = renameClient(m, ports, "new");
    check(result.status == RenameResult::partial && result.failed.size() == 1 &&
              m.restored.size() == 18,
          "partial failure retained");
    std::cout << "PASS split byte/time routing, malformed events, bounded overflow releases, all17 "
                 "port rename model\n";
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    try
    {
        check(argc == 4, "runtime root, repository root, screenshot dir required");
        check(app.platformName() == "windows", "normal Windows QPA required");
        modelTests();
        const auto dllPath =
            QString::fromUtf8(argv[1]) + "/midi_tools/midi_spilit/mod_midi_spilit.dll";
        const HMODULE dll = LoadLibraryW(reinterpret_cast<const wchar_t *>(dllPath.utf16()));
        check(dll, "real splitter DLL");
        checkVirtualMidiMode(dll, false);
        auto symbol = [&](const char *name) {
            auto p = GetProcAddress(dll, name);
            check(p, "required export");
            return p;
        };
        const auto create = reinterpret_cast<decltype(&wjn_create_node)>(symbol("wjn_create_node"));
        const auto destroy =
            reinterpret_cast<decltype(&wjn_destroy_node)>(symbol("wjn_destroy_node"));
        const auto get = reinterpret_cast<decltype(&wjn_get_node_settings_json)>(
            symbol("wjn_get_node_settings_json"));
        const auto set = reinterpret_cast<decltype(&wjn_set_node_settings_json)>(
            symbol("wjn_set_node_settings_json"));
        const auto widget = reinterpret_cast<decltype(&wjn_create_settings_widget)>(
            symbol("wjn_create_settings_widget"));
        const auto remove = reinterpret_cast<decltype(&wjn_destroy_settings_widget)>(
            symbol("wjn_destroy_settings_widget"));
        auto *node = create();
        check(node && !node->createWindow(nullptr), "no business window");
        check(set(node, R"({"version":1,"clientName":"split-offline"})"), "restore without JACK");
        const QByteArray before = get(node);
        for (const auto json :
             {R"({"version":2,"clientName":"x"})", R"({"version":1,"clientName":"bad:name"})",
              R"({"version":1,"clientName":"x","extra":1})", "{"})
        {
            check(!set(node, json) && QByteArray(get(node)) == before,
                  "atomic invalid state rejection");
        }
        QTemporaryDir settings;
        WinJACKNexus::UI::HostI18n host({settings.filePath("i18n.ini"), {"en-US"}, false});
        QString error;
        check(host.initialize(QString::fromUtf8(argv[2]), &error), qPrintable(error));
        auto lease = host.manager().registerLibrary(
            dllPath, WinJACKNexus::UI::I18nManager::Extension::node, 0, &error);
        check(bool(lease), qPrintable(error));
        QWidget *w = widget(node, nullptr);
        check(w, "public sidebar widget");
        w->resize(480, 160);
        w->show();
        w->raise();
        w->activateWindow();
        QTest::qWait(650);
        check(w->isVisible() && IsWindowVisible(reinterpret_cast<HWND>(w->winId())) &&
                  w->devicePixelRatioF() == 1.0,
              "visible native100% window");
        std::cout << "platform=windows, visible=1, DPR=" << w->devicePixelRatioF()
                  << ", system font=" << QFontInfo(app.font()).family().toStdString()
                  << ", window budget=1/100\n";
        auto *edit = w->findChild<QLineEdit *>("gate.clientName");
        auto *apply = w->findChild<QPushButton *>("gate.applyClientName");
        check(edit && apply, "shared public control");
        edit->setText("renamed-offline");
        QTest::mouseClick(apply, Qt::LeftButton);
        check(QByteArray(get(node)).contains("renamed-offline"), "actual widget apply");
        const QString output = QString::fromUtf8(argv[3]);
        QDir().mkpath(output);
        for (const auto locale : {"en-US", "zh-CN", "ja-JP", "ko-KR"})
        {
            check(host.manager().setLanguage(locale, &error), qPrintable(error));
            const auto expectedName =
                std::strcmp(locale, "zh-CN") == 0   ? QStringLiteral("MIDI 通道拆分")
                : std::strcmp(locale, "ja-JP") == 0 ? QStringLiteral("MIDI チャンネル分割")
                : std::strcmp(locale, "ko-KR") == 0 ? QStringLiteral("MIDI 채널 분할")
                                                    : QStringLiteral("MIDI Split");
            const auto displayName =
                WinJACKNexus::UI::localizedText("midi.midi_spilit", "name", "MIDI Split");
            check(displayName == expectedName,
                  "corrected Split name follows four-language catalog");
            w->setWindowTitle(displayName);
            QTest::qWait(450);
            check(edit->text() == "renamed-offline", "language preserves client state");
            for (auto *label : w->findChildren<QLabel *>())
            {
                if (!label->text().isEmpty())
                {
                    QTextLayout layout(label->text(), label->font());
                    layout.beginLayout();
                    layout.createLine();
                    layout.endLayout();
                    for (const auto &run : layout.glyphRuns())
                    {
                        for (auto glyph : run.glyphIndexes())
                        {
                            check(glyph != 0, "system fallback glyph present");
                        }
                    }
                }
            }
            check(w->screen()
                      ->grabWindow(w->winId())
                      .save(output + "/split-sidebar-" + locale + ".png"),
                  "real visible window screenshot");
        }
        edit->setText("bad:name");
        QTest::mouseClick(apply, Qt::LeftButton);
        QTest::qWait(100);
        check(QByteArray(get(node)).contains("renamed-offline"), "GUI rejected invalid name");
        check(
            w->screen()->grabWindow(w->winId()).save(output + "/split-sidebar-rejected-ko-KR.png"),
            "real rejected status screenshot");
        w->close();
        remove(node, w);
        lease.reset();
        node->shutdown();
        destroy(node);
        check(FreeLibrary(dll), "DLL unload after UI/lease");
        std::cout << "PASS real DLL state/GUI apply, four-language100% real Windows "
                     "captures/glyphs, no JACK init\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
