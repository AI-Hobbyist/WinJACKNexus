/* SPDX-License-Identifier: AGPL-3.0-only */
#include "../../Develop/Node/WindowShowCounter.hpp"
#include "../patchbay/mod/src/Library.cpp"
#include <QFontDatabase>
#include <QScreen>
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
    QTest::qWait(180);
    QApplication::processEvents();
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
        check(app.platformName() == "windows", "normal Windows Qt backend");
        app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
        QTemporaryDir temporary;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
        UI::I18nManager language({temporary.filePath("language.ini"), {"en-US"}, false});
        QString error;
        check(language.install(&error), "isolated language manager");
        UI::ThemeCore theme;
        check(theme.isValid(), "production theme core");
        UI::ThemeManager::installThemeCore(&theme);
        UI::ThemeManager themeAdapter;
        themeAdapter.bindThemeCore(&theme);
        themeAdapter.applyToApp(&app);
        auto &service = SS::Service::instance();
        check(service.installUI(&error),
              error.isEmpty() ? "module language and theme registration" : qPrintable(error));
        UI::TrayController tray;
        tray.initializeFallback();
        check(tray.switchAction() && tray.switchAction()->menu() == service.quickMenu(),
              "quick access native menu bound to actual switch action");
        const auto library = scanNodeModules();
        unsigned discovered = 0;
        for (const auto &node : library)
        {
            if (node.category == "Control Deck" && node.name.startsWith("Signal Switch ") &&
                node.capabilities == 3 && node.features == 0)
                ++discovered;
        }
        check(discovered == 5, "production node library discovers five signal switches in Control Deck");
        QWidget graphWindow;
        auto *layout = new QVBoxLayout(&graphWindow);
        auto *graph = new PatchbayCanvas(PatchbaySceneContext{"main/sub", "Subgraph", true}, &graphWindow);
        layout->addWidget(graph);
        graphWindow.resize(700, 400);
        graphWindow.move(720, 140);
        graphWindow.show();
        std::vector<std::unique_ptr<LoadedModuleNode>> nodes;
        for (const auto *kind : {"stereo", "midi"})
        {
            const auto path = QCoreApplication::applicationDirPath() + "/nodes/control_deck/" + kind +
                              "/wjn_signal_switch_" + kind + ".dll";
            auto node = LoadedModuleNode::create(path, 0, {}, WJN_NODE_MODE_VIRT, 48000, 64);
            check(bool(node), "production host loads and prepares VIRT switch without JACK server");
            node->setContextId("main/sub");
            nodes.push_back(std::move(node));
        }
        Snapshot snapshot;
        std::unordered_set<std::string> visibleClients;
        for (const auto &node : nodes)
        {
            node->appendVirtualSnapshot(snapshot);
            visibleClients.insert(node->clientId());
        }
        graph->setVisibleModuleClientIds(std::move(visibleClients));
        graph->setSnapshot(snapshot);
        service.synchronize = [&] {
            for (const auto &node : nodes)
                node->synchronizeSignalSwitch(graph);
        };
        service.edited = [graph] { graph->markProjectDirty(); };
        auto *editor = service.showEditor();
        check(editor != nullptr, "independent editor opens");
        editor->move(60, 160);
        editor->resize(620, 360);
        settle();
        check(editor->isVisible() && IsWindowVisible(reinterpret_cast<HWND>(editor->winId())),
              "real visible Windows editor");
        check(qAbs(editor->devicePixelRatioF() - 1.0) < 0.001, "100 percent scale only");
        check(editor->findChildren<C::ActionButton *>().size() == 2, "one shared button per live node");
        const auto id = service.endpoints.begin()->id;
        auto *button = editor->findChild<C::ActionButton *>("signalSwitchButton." + id);
        check(button && button->isChecked(), "new node button reflects enabled default");
        QTest::mouseClick(button, Qt::LeftButton);
        settle();
        check(!service.find(id)->desired && graph->isProjectDirty(), "button closes signal and marks project dirty");
        graph->setClientAlias(nodes.front()->clientId(), QString::fromUtf8("话筒 마이크"));
        nodes.front()->synchronizeSignalSwitch(graph);
        settle();
        const auto renamed = QString::fromUtf8("话筒 마이크");
        bool synchronized = false;
        for (const auto &endpoint : service.endpoints)
            synchronized |= endpoint.name == renamed;
        check(synchronized, "graph rename synchronizes editor button identity");
        auto *endpoint = service.find(id);
        check(endpoint && endpoint->rename(QString::fromUtf8("演奏 버튼")), "button rename uses host graph alias path");
        settle();
        service.synchronize();
        check(service.find(id)->name == QString::fromUtf8("演奏 버튼"), "rename remains after host resynchronization");
        const auto geometry = editor->geometry();
        service.state.window.clickThrough = true;
        service.state.window.opacity = 65;
        const auto savedState = service.state.toBase64();
        std::vector<PatchbayProjectModule> records;
        for (const auto &node : nodes)
            records.push_back({node->modulePath(), 0, node->clientId(), node->settingsJson(), node->contextId()});
        const auto project = temporary.filePath("signal-switch.wjnproj");
        check(graph->saveProject(project, records), "production project writer saves switch presets");
        QFile file(project);
        check(file.open(QIODevice::ReadOnly), "read saved project");
        const auto savedRoot = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        check(savedRoot["signalSwitchStateBase64"].toString() == savedState,
              "all module presets encoded as Base64 in project");
        std::vector<PatchbayProjectModule> loaded;
        check(graph->loadProject(project, loaded) && loaded.size() == 2,
              "production project reader restores node records");
        service.beginRestore();
        nodes.clear();
        for (const auto &record : loaded)
        {
            auto node =
                LoadedModuleNode::create(record.modulePath, 0, record.settingsJson, WJN_NODE_MODE_VIRT, 48000, 64);
            check(bool(node), "restore module settings before processing");
            node->setClientId(record.clientId);
            node->setContextId(record.contextId);
            nodes.push_back(std::move(node));
        }
        check(service.restore(graph->signalSwitchProjectState(), &error),
              "restore complete module state after node identities");
        settle();
        check(service.find(id) && !service.find(id)->desired, "saved disabled state survives enabled factory default");
        check(editor->geometry() == geometry && service.state.window.opacity == 65 && service.state.window.clickThrough,
              "editor position size and button presets restored");
        editor->close();
        check(!editor->isVisible() && service.endpoints.size() == 2, "closing editor preserves nodes and signal state");
        auto *open = service.quickMenu()->findChild<QAction *>("signalSwitchOpenEditor");
        check(open != nullptr, "explicit open editor dropdown entry");
        open->trigger();
        settle();
        check(editor->isVisible(), "quick access command reopens editor");
        check(language.setLanguage("zh-CN", &error), "module language hot switch");
        settle();
        check(open->text() == QString::fromUtf8("打开编辑器"), "native menu text follows installed i18n API");
        QTextLayout glyphs(QString::fromUtf8("开关 日本語 한국어"), app.font());
        glyphs.beginLayout();
        glyphs.createLine();
        glyphs.endLayout();
        bool rendered = !glyphs.glyphRuns().isEmpty();
        for (const auto &run : glyphs.glyphRuns())
            for (auto glyph : run.glyphIndexes())
                rendered &= glyph != 0;
        check(rendered, "system font fallback resolves CJK glyphs without tofu");
        QDir().mkpath(QString::fromUtf8(SWITCH_SOURCE_DIR) + "/Docs/SignalSwitch/S3");
        const auto capture = editor->screen()->grabWindow(editor->winId());
        check(!capture.isNull() &&
                  capture.save(QString::fromUtf8(SWITCH_SOURCE_DIR) + "/Docs/SignalSwitch/S3/editor.png"),
              "capture actual visible editor window");
        nodes.pop_back();
        settle();
        check(editor->findChildren<C::ActionButton *>().size() == 1, "deleting node removes corresponding button");
        check(shows.shows <= 100, "bounded real-window batch");
        std::cout << "WINDOW_SHOWS " << shows.shows << std::endl;
        service.synchronize = {};
        service.edited = {};
        nodes.clear();
        service.shutdownUI();
        graphWindow.close();
        tray.shutdown();
        UI::ThemeManager::installThemeCore(nullptr);
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << std::endl;
        SS::Service::instance().synchronize = {};
        SS::Service::instance().shutdownUI();
        UI::ThemeManager::installThemeCore(nullptr);
        return 1;
    }
}
