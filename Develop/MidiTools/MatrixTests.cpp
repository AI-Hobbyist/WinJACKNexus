// Offline processing + actual DLL settings, no JACK init/server/connection calls.
// SPDX-License-Identifier: AGPL-3.0-only
#include "JackBackend.hpp"
#include <QApplication>
#include <QDir>
#include <QFontInfo>
#include <QGlyphRun>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>
#include <QTextLayout>
#include <WinJACKNexus/Common/MidiToolsMatrix.hpp>
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/HostI18n.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <WinJACKNexus/UI/ThemeNamespaceRegistry.hpp>
#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
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
    auto p = std::make_unique<MatrixProcessor>();
    std::vector<Captured> captured;
    auto writer = [&](unsigned port, EventView e) {
        captured.push_back({port, e.frame, {e.bytes.begin(), e.bytes.end()}});
        return true;
    };
    std::array<std::vector<std::vector<uint8_t>>, 16> bytes;
    std::array<std::vector<unsigned>, 16> times;
    auto process = [&](MatrixRoutes r) {
        p->process(
            256, r, [&](unsigned c) { return unsigned(bytes[c].size()); },
            [&](unsigned c, unsigned i) { return EventView{times[c][i], bytes[c][i]}; }, writer);
    };
    for (unsigned c = 0; c < 16; ++c)
    {
        bytes[c] = {{uint8_t(0x90 + c), 60, 100}};
        times[c] = {127};
    }
    process(diagonalRoutes());
    check(captured.size() == 16, "default diagonal");
    for (unsigned c = 0; c < 16; ++c)
    {
        check(captured[c].port == c && captured[c].bytes[0] == 0x90 + c,
              "original channel preserved");
    }
    p->reset();
    captured.clear();
    for (auto &b : bytes)
    {
        b.clear();
    }
    for (auto &t : times)
    {
        t.clear();
    }
    bytes[0] = {{0xb3, 7, 70}, {0x93, 61, 100}};
    times[0] = {127, 127};
    bytes[1] = {{0x92, 62, 100}, {0x82, 62, 0}};
    times[1] = {0, 127};
    MatrixRoutes routes{};
    routes[0] = routes[1] = 3;
    process(routes);
    check(captured.size() == 8 && captured[0].bytes[1] == 62 && captured[2].bytes[1] == 7 &&
              captured[4].bytes[1] == 61 && captured[6].bytes[0] == 0x82,
          "stable frame/input/sequence sorting and fanout");
    for (auto &b : bytes)
    {
        b.clear();
    }
    for (auto &t : times)
    {
        t.clear();
    }
    bytes[0] = {{0xb3, 64, 127}};
    times[0] = {0};
    process(routes);
    captured.clear();
    bytes[0].clear();
    times[0].clear();
    routes[0] = 0;
    process(routes);
    check(captured.size() == 4, "removed two routes release owned note and pedal");
    check(captured[0].bytes == std::vector<uint8_t>({0x83, 61, 0}) &&
              captured[1].bytes == std::vector<uint8_t>({0xb3, 64, 0}),
          "source-owned release exact channel/key");
    captured.clear();
    process(routes);
    check(captured.empty(), "release ledger reclaimed");
    p->reset();
    routes = {};
    routes[0] = 1;
    bytes[0] = {{0x99, 60, 100}, {0x99, 60, 0}};
    times[0] = {0, 1};
    process(routes);
    bytes[0].clear();
    times[0].clear();
    captured.clear();
    routes[0] = 0;
    process(routes);
    check(captured.empty(), "velocity-zero noteoff clears ownership");
    bytes[0] = {{0x90, 60, 100}};
    times[0] = {0};
    routes[0] = 1;
    process(routes);
    bytes[0].clear();
    times[0].clear();
    routes[0] = 0;
    p->process(
        256, routes, [](unsigned) { return 0; }, [](unsigned, unsigned) { return EventView{}; },
        [](unsigned, EventView) { return false; });
    captured.clear();
    process(routes);
    check(captured.size() == 3 && captured[2].bytes[1] == 120,
          "failed disconnect release retried with panic");
    bytes[0].assign(256, {0x90, 60, 100});
    times[0].assign(256, 0);
    routes[0] = 1;
    process(routes);
    check(p->overflow() > 0, "bounded repeated note ledger overflow");
    bytes[0].clear();
    times[0].clear();
    captured.clear();
    process(routes);
    check(captured.size() == 3, "ledger overflow releases affected channel");
    MatrixSnapshot snapshot;
    std::atomic<bool> done = false;
    std::thread producer([&] {
        for (unsigned i = 0; i < 10000; ++i)
        {
            MatrixRoutes r;
            r.fill(i % 2 ? 0 : 0xffff);
            snapshot.publish(r);
        }
        done = true;
    });
    MatrixRoutes r{};
    while (!done.load())
    {
        if (snapshot.read(r))
        {
            check(std::all_of(r.begin(), r.end(), [&](auto row) { return row == r[0]; }) ||
                      r == diagonalRoutes(),
                  "coherent256bit snapshot");
        }
    }
    producer.join();
    const auto ports = WinJACKNexus::MidiTools::matrixPorts();
    check(ports.size() == 32 && ports[15].input && !ports[16].input, "32 fixed ports");
    RenameModel m;
    for (unsigned c = 0; c < 16; ++c)
    {
        m.saved.push_back({"peer:out", "old:" + ports[c].shortName});
        m.saved.push_back({"old:" + ports[c + 16].shortName, "peer:in"});
    }
    m.saved.push_back({"old:MIDI_OUT_16", "old:MIDI_IN_16"});
    const auto saved = m.saved;
    auto result = renameClient(m, ports, "occupied");
    check(result.actualName == "occupied-01" && m.restored.size() == 33 &&
              m.restored.back().destination == "occupied-01:MIDI_IN_16",
          "32ports actualname/self restore");
    m = {};
    m.saved = saved;
    m.createOk = false;
    check(renameClient(m, ports, "new").status == RenameResult::createFailed &&
              m.restored.size() == 33,
          "32port rollback");
    m = {};
    m.saved = saved;
    m.saved.push_back({"old:MIDI_OUT_01", "missing:in"});
    result = renameClient(m, ports, "new");
    check(result.status == RenameResult::partial && result.failed.size() == 1 &&
              m.restored.size() == 33,
          "32port partial retained");
    std::cout << "PASS matrix deterministic merge/fanout, source ledger disconnect/failed "
                 "release/overflow, coherent snapshot and32port rename\n";
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    try
    {
        check(argc == 4, "runtime root, repository root, screenshot dir required");
        check(app.platformName() == "windows", "normal Windows QPA required");
        WinJACKNexus::UI::ThemeCore themeCore;
        check(themeCore.isValid(), qPrintable(themeCore.initializationError()));
        WinJACKNexus::UI::ThemeManager::installThemeCore(&themeCore);
        WinJACKNexus::UI::ThemeManager theme;
        theme.watchApplication(&app);
        modelTests();
        const auto dllPath =
            QString::fromUtf8(argv[1]) + "/midi_tools/midi_matrix/mod_midi_matrix.dll";
        const HMODULE dll = LoadLibraryW(reinterpret_cast<const wchar_t *>(dllPath.utf16()));
        check(dll, "real matrix DLL");
        WinJACKNexus::UI::ThemeNamespaceRegistry::Lease themeLease;
        QString themeError;
        check(WinJACKNexus::UI::ThemeNamespaceRegistry::instance().registerNodeModule(
                  [dll](const char *name) {
                      return reinterpret_cast<QFunctionPointer>(GetProcAddress(dll, name));
                  },
                  dllPath, &themeLease, &themeError),
              qPrintable(themeError));
        check(themeCore.refreshRegisteredNamespaces(&themeError), qPrintable(themeError));
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
        check(node, "matrix node");
        auto state = QJsonDocument::fromJson(get(node)).object();
        check(state["routes"].toArray().size() == 256, "256 persisted bits");
        state["clientName"] = "matrix-offline";
        check(set(node, QJsonDocument(state).toJson(QJsonDocument::Compact).constData()),
              "restore without JACK");
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
        const HWND native = node->createWindow(nullptr);
        check(native, "independent matrix real window");
        QWidget *matrix = QWidget::find(reinterpret_cast<WId>(native));
        check(matrix && matrix->isWindow(), "top-level matrix window");
        QTest::qWait(650);
        check(IsWindowVisible(native) && matrix->devicePixelRatioF() == 1.0,
              "matrix visible100% Windows");
        const QString output = QString::fromUtf8(argv[3]);
        QDir().mkpath(output);
        check(matrix->screen()
                  ->grabWindow(matrix->winId())
                  .save(output + "/matrix-default-en-US.png"),
              "default diagonal capture");
        auto *cross = matrix->findChild<QPushButton *>("route-0-1");
        check(cross && !cross->isChecked(), "offdiagonal default");
        check(cross->property("surfaceColor").value<QColor>() ==
                  WinJACKNexus::UI::Controls::themeTokenColor(
                      "global", "control-button-surface-normal", QColor()),
              "matrix buttons inherit the actual default theme");
        QTest::mouseClick(cross, Qt::LeftButton);
        check(QJsonDocument::fromJson(get(node)).object()["routes"].toArray()[1].toBool(),
              "GUI route applied to persisted snapshot");
        const QByteArray routed = get(node);
        auto *restored = create();
        check(restored && set(restored, routed.constData()) && QByteArray(get(restored)) == routed,
              "nondefault256bit state roundtrip");
        destroy(restored);
        auto damaged = QJsonDocument::fromJson(routed).object();
        auto damagedBits = damaged["routes"].toArray();
        damagedBits[1] = 1;
        damaged["routes"] = damagedBits;
        check(!set(node, QJsonDocument(damaged).toJson().constData()) &&
                  QByteArray(get(node)) == routed,
              "nonboolean bit rejected atomically");
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
                  << ", window budget=3/100\n";
        auto *edit = w->findChild<QLineEdit *>("gate.clientName");
        auto *apply = w->findChild<QPushButton *>("gate.applyClientName");
        check(edit && apply, "shared public control");
        edit->setText("renamed-offline");
        QTest::mouseClick(apply, Qt::LeftButton);
        check(QByteArray(get(node)).contains("renamed-offline"), "actual widget apply");
        for (const auto locale : {"en-US", "zh-CN", "ja-JP", "ko-KR"})
        {
            check(host.manager().setLanguage(locale, &error), qPrintable(error));
            QTest::qWait(450);
            check(edit->text() == "renamed-offline" && cross->isChecked(),
                  "language preserves client/routes state");
            matrix->raise();
            matrix->activateWindow();
            QTest::qWait(150);
            check(matrix->screen()
                      ->grabWindow(matrix->winId())
                      .save(output + "/matrix-routed-" + locale + ".png"),
                  "real matrix capture");
            w->raise();
            w->activateWindow();
            QTest::qWait(150);
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
                      .save(output + "/matrix-sidebar-" + locale + ".png"),
                  "real visible window screenshot");
        }
        edit->setText("bad:name");
        QTest::mouseClick(apply, Qt::LeftButton);
        QTest::qWait(100);
        check(QByteArray(get(node)).contains("renamed-offline"), "GUI rejected invalid name");
        check(
            w->screen()->grabWindow(w->winId()).save(output + "/matrix-sidebar-rejected-ko-KR.png"),
            "real rejected status screenshot");
        matrix->close();
        check(!matrix->isVisible(), "close hides only window");
        const auto reopened = node->createWindow(nullptr);
        check(reopened && QWidget::find(reinterpret_cast<WId>(reopened)) == matrix,
              "reopen same matrix object");
        QTest::qWait(250);
        check(cross->isChecked(), "close/reopen keeps routing");
        matrix->close();
        w->close();
        remove(node, w);
        lease.reset();
        node->shutdown();
        destroy(node);
        themeLease.reset();
        check(FreeLibrary(dll), "DLL unload after UI/lease");
        std::cout << "PASS matrix DLL/routes and sidebar, four-language100% Windows captures, "
                     "close/reopen, no JACK init\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
