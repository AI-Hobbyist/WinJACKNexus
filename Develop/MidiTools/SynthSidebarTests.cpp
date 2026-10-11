// Offline processing + actual DLL settings, no JACK init/server/connection calls.
// SPDX-License-Identifier: AGPL-3.0-only
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
#include <WinJACKNexus/Common/MidiToolsSynth.hpp>
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/UI/HostI18n.hpp>
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
    const auto ports = WinJACKNexus::MidiTools::synthPorts();
    check(ports.size() == 3 && ports[0].input && ports[1].type == PortType::audio &&
              ports[2].type == PortType::audio,
          "MIDI input and stereo audio layout");
    RenameModel model;
    model.saved = {{"peer:midi", "old:MIDI_IN"},
                   {"old:OUT_L", "peer:left"},
                   {"old:OUT_R", "peer:right"},
                   {"old:OUT_L", "old:MIDI_IN"}};
    const auto saved = model.saved;
    auto result = renameClient(model, ports, "occupied");
    check(result.actualName == "occupied-01" && model.restored.size() == 4 &&
              model.restored.back().source == "occupied-01:OUT_L" &&
              model.restored.back().destination == "occupied-01:MIDI_IN",
          "all MIDI/audio connections use actual name, including self connection");
    model = {};
    model.saved = saved;
    model.createOk = false;
    check(renameClient(model, ports, "new").status == RenameResult::createFailed &&
              model.restored.size() == 4,
          "synth rollback preserves MIDI and stereo connections");
    model = {};
    model.saved = saved;
    model.saved.push_back({"old:OUT_L", "missing:in"});
    result = renameClient(model, ports, "new");
    check(result.status == RenameResult::partial && result.failed.size() == 1 &&
              model.restored.size() == 4,
          "partial synth reconnect retains successful MIDI and audio connections");
    std::cout << "PASS synthesizer MIDI/stereo rename model without JACK\n";
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
            QString::fromUtf8(argv[1]) + "/midi_tools/sf2_loader/mod_sf2_loader.dll";
        const HMODULE dll = LoadLibraryW(reinterpret_cast<const wchar_t *>(dllPath.utf16()));
        check(dll, "real synthesizer DLL");
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
        auto *node = create(0);
        check(node, "real synthesizer instance without JACK init");
        SynthSettings state;
        state.clientName = "synth-offline";
        state.resources = {
            {"fixture", std::string(argv[2]) + "/Develop/MidiTools/fixtures/tone.sf2", "sf2"}};
        state.channels[0].gainDb = -6;
        check(set(node, settingsJson(state).c_str()), "restore resource/mix without JACK");
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
        SynthSettings renamed;
        check(parseSettings(get(node), renamed) && renamed.resources.size() == 1 &&
                  renamed.resources[0].id == "fixture" && renamed.channels[0].gainDb == -6,
              "client rename preserves synth resources and mix");
        const QString output = QString::fromUtf8(argv[3]);
        QDir().mkpath(output);
        for (const auto locale : {"en-US", "zh-CN", "ja-JP", "ko-KR"})
        {
            check(host.manager().setLanguage(locale, &error), qPrintable(error));
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
                      .save(output + "/synth-sidebar-" + locale + ".png"),
                  "real visible window screenshot");
        }
        edit->setText("bad:name");
        QTest::mouseClick(apply, Qt::LeftButton);
        QTest::qWait(100);
        check(QByteArray(get(node)).contains("renamed-offline"), "GUI rejected invalid name");
        check(
            w->screen()->grabWindow(w->winId()).save(output + "/synth-sidebar-rejected-ko-KR.png"),
            "real rejected status screenshot");
        w->close();
        remove(node, w);
        lease.reset();
        node->shutdown();
        destroy(node);
        check(FreeLibrary(dll), "DLL unload after UI/lease");
        std::cout << "PASS synth DLL state/client rename preserves resources/mix and100% "
                     "four-language Windows sidebar, no JACK init\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
