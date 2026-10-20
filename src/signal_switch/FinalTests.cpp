/* SPDX-License-Identifier: AGPL-3.0-only */
#include "../../Develop/Node/WindowShowCounter.hpp"
#include "../patchbay/mod/src/Library.cpp"
#include <QFontDatabase>
#include <QGraphicsOpacityEffect>
#include <QScreen>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTextLayout>
#include <iostream>
#include <stdexcept>

namespace SS = WinJACKNexus::SignalSwitch;
namespace UI = WinJACKNexus::UI;
namespace C = UI::Controls;

namespace
{
unsigned checks{};
void check(bool passed, const char *description)
{
    std::cout << (passed ? "PASS " : "FAIL ") << ++checks << ' ' << description << std::endl;
    if (!passed)
        throw std::runtime_error(description);
}

void settle()
{
    QTest::qWait(200);
    QApplication::processEvents();
}

void checkGlyphs(const QString &text, const QFont &font)
{
    QTextLayout layout(text, font);
    layout.beginLayout();
    layout.createLine();
    layout.endLayout();
    bool valid = !layout.glyphRuns().isEmpty();
    for (const auto &run : layout.glyphRuns())
        for (const auto glyph : run.glyphIndexes())
            valid &= glyph != 0;
    check(valid, "system fallback resolves visible CJK text without missing glyphs");
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    WindowShowCounter shows;
    app.installEventFilter(&shows);
    try
    {
        check(app.platformName() == "windows", "normal Windows platform");
        app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
        QTemporaryDir temporary;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
        UI::I18nManager language({temporary.filePath("language.ini"), {"en-US"}, false});
        QString error;
        check(language.install(&error), "isolated production language API");
        UI::ThemeCore theme;
        check(theme.isValid(), "production theme core");
        UI::ThemeManager::installThemeCore(&theme);
        UI::ThemeManager adapter;
        adapter.bindThemeCore(&theme);
        adapter.applyToApp(&app);
        auto &service = SS::Service::instance();
        check(service.installUI(&error), "main module registers language and theme contracts");
        PatchbayCanvas graph(PatchbaySceneContext{"main/sub", "Subgraph", true});
        std::vector<std::unique_ptr<LoadedModuleNode>> nodes;
        QString monoId, stereoId, midiId;
        for (const auto *kind : {"mono", "stereo", "5_1", "7_1", "midi"})
        {
            const auto path = QCoreApplication::applicationDirPath() + "/nodes/signal_switch/" + kind +
                              "/wjn_signal_switch_" + kind + ".dll";
            auto node = LoadedModuleNode::create(path, 0, {}, WJN_NODE_MODE_VIRT, 48000, 64);
            check(bool(node), "actual host loads VIRT node with optional language and theme contracts");
            node->setContextId("main/sub");
            for (const auto &endpoint : service.endpoints)
            {
                if (endpoint.graphClient != QString::fromStdString(node->clientId()))
                    continue;
                if (QByteArray(kind) == "mono")
                    monoId = endpoint.id;
                if (QByteArray(kind) == "stereo")
                    stereoId = endpoint.id;
                if (QByteArray(kind) == "midi")
                    midiId = endpoint.id;
            }
            nodes.push_back(std::move(node));
        }
        check(service.endpoints.size() == 5 && !monoId.isEmpty() && !stereoId.isEmpty() && !midiId.isEmpty(),
              "all five node identities reach shared registry");
        Snapshot snapshot;
        std::unordered_set<std::string> visibleClients;
        for (const auto &node : nodes)
        {
            node->appendVirtualSnapshot(snapshot);
            visibleClients.insert(node->clientId());
        }
        graph.setVisibleModuleClientIds(std::move(visibleClients));
        graph.setSnapshot(snapshot);
        auto *editor = service.showEditor();
        editor->move(80, 130);
        editor->resize(660, 430);
        settle();
        check(editor->isVisible() && IsWindowVisible(reinterpret_cast<HWND>(editor->winId())) &&
                  qAbs(editor->devicePixelRatioF() - 1.0) < .001,
              "real visible editor at 100 percent scale");
        auto button = [&](const QString &id) {
            for (auto *widget : QApplication::allWidgets())
                if (widget->objectName() == "signalSwitchButton." + id)
                    return qobject_cast<C::ActionButton *>(widget);
            return static_cast<C::ActionButton *>(nullptr);
        };
        for (const auto &endpoint : service.endpoints)
            check(endpoint.desired && button(endpoint.id) && button(endpoint.id)->isChecked(),
                  "factory enabled state agrees with each visible button");
        graph.setClientAlias(nodes.back()->clientId(), QString::fromUtf8("演奏 마이크 日本語"));
        nodes.back()->synchronizeSignalSwitch(&graph);
        check(service.find(midiId)->userNamed, "graph rename records user ownership of name");
        int row = 200;
        for (auto it = service.state.cards.begin(); it != service.state.cards.end(); ++it)
        {
            if (it.key() == monoId || it.key() == stereoId)
                continue;
            it->geometry = QRect(24, row, 310, 48);
            row += 64;
        }
        service.state.cards[monoId].geometry = QRect(24, 48, 310, 48);
        service.state.cards[stereoId].geometry = QRect(24, 112, 310, 48);
        check(service.state.setGroup("stage", QString::fromUtf8("舞台 그룹"), {monoId, stereoId}),
              "custom group name accepted");
        const QString captureRoot = QString::fromUtf8(SWITCH_SOURCE_DIR) + "/Docs/SignalSwitch/S5";
        QDir().mkpath(captureRoot);
        const QList<QPair<QString, QString>> names{{"zh-CN", QString::fromUtf8("Signal Switch 单声道")},
                                                   {"en-US", "Signal Switch Mono"},
                                                   {"ja-JP", QString::fromUtf8("Signal Switch モノラル")},
                                                   {"ko-KR", QString::fromUtf8("Signal Switch 모노")}};
        for (const auto &[locale, expected] : names)
        {
            check(language.setLanguage(locale, &error), "four language hot switch through installed API");
            settle();
            check(service.label(*service.find(monoId)) == expected && button(monoId)->toolTip().contains(expected),
                  "unedited node default name follows language in editor");
            check(service.label(*service.find(midiId)) == QString::fromUtf8("演奏 마이크 日本語") &&
                      service.state.groups["stage"].name == QString::fromUtf8("舞台 그룹"),
                  "user node and group names survive language changes");
            checkGlyphs(button(monoId)->accessibleName() + button(midiId)->accessibleName(), app.font());
            check(editor->screen()->grabWindow(editor->winId()).save(captureRoot + "/" + locale + ".png"),
                  "capture real editor for language inspection");
        }
        service.state.cards[midiId].geometry.setWidth(100);
        service.notify();
        check(button(midiId)->text().contains(QChar(0x2026)) &&
                  button(midiId)->accessibleName().contains(QString::fromUtf8("演奏 마이크 日本語")),
              "long name elides while accessibility and tooltip keep full name");
        service.state.cards[stereoId].color = QColor("#5d2c83");
        service.notify();
        const auto color = [&](const QString &id) { return button(id)->property("surfaceColor").value<QColor>(); };
        const QString token = "--wjn-theme-signal-switch-control-button-surface-normal";
        check(theme.setOverride(token, "#225577", {}, &error), "module theme token override accepted");
        settle();
        check(color(monoId) == QColor("#225577") && color(stereoId) == QColor("#5d2c83"),
              "module token hot refresh preserves explicit instance color");
        check(editor->screen()->grabWindow(editor->winId()).save(captureRoot + "/theme.png"),
              "capture actual theme token and instance color presentation");
        check(theme.setFullTheme(theme.defaultTheme(), &error), "full theme reload through production API");
        settle();
        check(color(monoId) != QColor("#225577") && color(stereoId) == QColor("#5d2c83"),
              "full theme refresh updates inherited button and preserves instance override");
        service.state.cards[stereoId].color = QColor();
        service.notify();
        check(color(stereoId) == color(monoId), "removing instance override restores theme inheritance");
        service.state.cards[stereoId].color = QColor("#5d2c83");
        service.state.cards[midiId].geometry = QRect(900, 700, 210, 72);
        service.state.grid = true;
        service.state.gridX = 24;
        service.state.gridY = 32;
        check(service.setEnabled({stereoId}, false) && service.state.setExclusive("routing", {monoId, stereoId}),
              "saved exclusive relationship has one enabled member");
        service.state.window.opacity = 73;
        service.state.window.clickThrough = true;
        service.notify();
        auto *scroll = editor->findChild<QScrollArea *>();
        scroll->horizontalScrollBar()->setValue(40);
        scroll->verticalScrollBar()->setValue(50);
        settle();
        service.quickMenu()->findChild<QAction *>("signalSwitchLock")->trigger();
        settle();
        check(!editor->isVisible() && service.state.locked, "project A saved in locked presentation");
        const auto stateA = service.state.toBase64();
        auto records = [&] {
            std::vector<PatchbayProjectModule> result;
            for (const auto &node : nodes)
                result.push_back({node->modulePath(), 0, node->clientId(), node->settingsJson(), node->contextId()});
            return result;
        };
        const auto projectA = temporary.filePath("A.wjnproj");
        const auto projectB = temporary.filePath("B.wjnproj");
        check(graph.saveProject(projectA, records()), "production writer saves full project A");
        auto restore = [&](const QString &path) {
            std::vector<PatchbayProjectModule> saved;
            check(graph.loadProject(path, saved), "production reader loads project");
            service.beginRestore();
            nodes.clear();
            for (const auto &record : saved)
            {
                auto node =
                    LoadedModuleNode::create(record.modulePath, 0, record.settingsJson, WJN_NODE_MODE_VIRT, 48000, 64);
                check(bool(node), "host reconstructs saved VIRT node");
                node->setClientId(record.clientId);
                node->setContextId(record.contextId);
                nodes.push_back(std::move(node));
            }
            check(service.restore(graph.signalSwitchProjectState(), &error), "apply complete project preset");
            settle();
        };
        service.beginRestore();
        nodes.clear();
        check(service.restore({}, &error), "new project uses independent defaults");
        service.showEditor();
        editor->move(220, 190);
        editor->resize(520, 310);
        settle();
        const auto stateB = service.state.toBase64();
        check(graph.saveProject(projectB, records()), "production writer saves distinct empty project B");
        restore(projectA);
        check(service.state.toBase64() == stateA, "A restores every serialized preset without mutation");
        check(service.endpoints.size() == 5 && !service.find(stereoId)->desired && service.find(monoId)->desired,
              "saved off overrides factory default and exclusive state is retained");
        check(service.label(*service.find(midiId)) == QString::fromUtf8("演奏 마이크 日本語"),
              "custom node name persists through actual module reconstruction");
        for (const auto &endpoint : service.endpoints)
        {
            auto *overlay = button(endpoint.id)->window();
            const auto card = service.state.cards[endpoint.id];
            check(overlay->isVisible() && overlay != editor &&
                      overlay->geometry() ==
                          QRect(service.state.window.overlayOrigin + card.geometry.topLeft(), card.geometry.size()) &&
                      (GetWindowLongPtrW(reinterpret_cast<HWND>(overlay->winId()), GWL_EXSTYLE) & WS_EX_TOPMOST),
                  "restored locked buttons keep saved desktop position size and native topmost style");
        }
        check(!service.restore("not base64!", &error) && service.state.toBase64() == stateA,
              "corrupt Base64 rejected without partial preset application");
        auto unknown = service.state.toJson();
        unknown["version"] = 999;
        check(!service.restore(QString::fromLatin1(QJsonDocument(unknown).toJson().toBase64()), &error) &&
                  service.state.toBase64() == stateA,
              "unknown state version rejected without partial application");
        restore(projectB);
        check(service.endpoints.isEmpty() && service.state.toBase64() == stateB,
              "B restores own editor size position and settings with no A presets");
        unsigned overlays{};
        for (auto *widget : QApplication::topLevelWidgets())
            overlays += widget->objectName().startsWith("signalSwitchOverlay.");
        check(overlays == 0, "switching to empty project removes all pinned buttons");
        restore(projectA);
        check(service.state.toBase64() == stateA, "second A restore preserves all fields again");
        check(shows.shows <= 100, "bounded real Windows batch");
        std::cout << "WINDOW_SHOWS " << shows.shows << std::endl;
        nodes.clear();
        service.shutdownUI();
        UI::ThemeManager::installThemeCore(nullptr);
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << std::endl;
        SS::Service::instance().shutdownUI();
        UI::ThemeManager::installThemeCore(nullptr);
        return 1;
    }
}
