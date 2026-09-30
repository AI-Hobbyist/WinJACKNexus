/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/Audio/JackApi.hpp>
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Common/DigitalMixerState.hpp>
#include <mod_patchbay/ProjectStateCodec.hpp>
#include <WinJACKNexus/UI/TrayController.hpp>
#include "PolarMixRegression.hpp"
#include "AuxPolarSwitchRegression.hpp"
#include "MeterbridgeOpenRegression.hpp"
#include "MixerLayoutRegression.hpp"
#include "FxChainPresetRegression.hpp"
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/MixerWindow.hpp>
#include <QFontDatabase>
#include <QFontInfo>
#include <QTextLayout>
#include <QGlyphRun>
#include <QRawFont>
#include <QLabel>
#include <QLibrary>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <WinJACKNexus/UI/Controls.hpp>

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QScreen>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include <cstdio>

extern "C" int wjn_patchbay_show_digital_mixer(void);

namespace {
int failures = 0;
void check(bool condition, const char* description)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", description); ++failures; }
}

QAction* actionNamed(QMenu* menu, const QString& text)
{
    if (!menu) return nullptr;
    for (auto* action : menu->actions())
        if (action->text() == text) return action;
    return nullptr;
}

QMenu* submenu(QMenu* menu, const QString& text)
{
    const auto* action = actionNamed(menu, text);
    return action ? action->menu() : nullptr;
}

QMenu* submenu(QMenuBar* bar, const QString& text)
{
    if (!bar) return nullptr;
    for (auto* action : bar->actions())
        if (action->text() == text) return action->menu();
    return nullptr;
}

int testMixerProjectRestore(QApplication& application, const QString& settingsPath)
{
    using namespace WinJACKNexus;
    Common::DigitalMixerState state;
    state.add(Common::MixerStripKind::input, Common::MixerLayout::stereo);
    const auto projectPath = QDir(settingsPath).filePath(QStringLiteral("restore-mixer.wjnproj"));
    const QJsonObject project {
        {QStringLiteral("version"), 1},
        {QStringLiteral("app"), QStringLiteral("WinJACKNexus")},
        {QStringLiteral("clients"), QJsonArray {}},
        {QStringLiteral("ports"), QJsonArray {}},
        {QStringLiteral("connections"), QJsonArray {}},
        {QStringLiteral("layouts"), QJsonArray {}},
        {QStringLiteral("modules"), QJsonArray {QJsonObject {
            {QStringLiteral("path"), QStringLiteral("nodes/mixer/mod_digital_mixer.dll")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("client"), QStringLiteral("DigitalMixer")},
            {QStringLiteral("settings"), QJsonDocument::fromJson(state.toJson()).object()}
        }}},
        {QStringLiteral("vsts"), QJsonArray {}},
        {QStringLiteral("subgraphs"), QJsonArray {}}
    };
    QFile file(projectPath);
    check(file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(project).toJson()) > 0,
          "write a mixer project with an input channel");
    file.close();
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       QStringLiteral("WinJACKNexus"), QStringLiteral("Patchbay"));
    settings.setValue(QStringLiteral("lastProjectPath"), projectPath);
    settings.setValue(QStringLiteral("residentMixerEnabled"), true);
    auto* host = wjn_create_node(0);
    check(host && host->init(), "initialize patchbay for mixer project restore");
    if (!host) return 1;
    const auto handle = host->createWindow(nullptr);
    auto* window = qobject_cast<QMainWindow*>(QWidget::find(reinterpret_cast<WId>(handle)));
    check(window != nullptr, "open patchbay with saved mixer project");
    Audio::JackClient observer;
    check(observer.open({"MixerProjectRestoreProbe", false, false, nullptr}), "open JACK restore probe");
    for (int attempt = 0; attempt < 50; ++attempt) {
        application.processEvents();
        if (observer.isOpen() && Audio::JackApi::instance().portByName(observer.nativeHandle(), "DigitalMixer:CH1_L")) break;
        QThread::msleep(20);
    }
    check(observer.isOpen() && Audio::JackApi::instance().portByName(observer.nativeHandle(), "DigitalMixer:CH1_L"),
          "saved mixer input channel is restored on launch");
    check(wjn_patchbay_show_digital_mixer() > 0, "open restored mixer interface");
    QMainWindow* mixerWindow = nullptr;
    for (auto* widget : QApplication::topLevelWidgets())
        if (widget->windowTitle() == QStringLiteral("WinJACKNexus - 调音台"))
            mixerWindow = qobject_cast<QMainWindow*>(widget);
    auto* channels = mixerWindow ? submenu(mixerWindow->menuBar(), QStringLiteral("通道管理")) : nullptr;
    auto* inputs = submenu(channels, QStringLiteral("添加输入通道"));
    check(inputs && inputs->actions().size() > 1, "find mixer add-input action");
    if (inputs && inputs->actions().size() > 1) inputs->actions().at(1)->trigger();
    application.processEvents();
    check(observer.isOpen() && Audio::JackApi::instance().portByName(observer.nativeHandle(), "DigitalMixer:CH2_L"),
          "new mixer input channel creates JACK ports");
    auto* saveProject = window ? actionNamed(submenu(window->menuBar(), QStringLiteral("文件")),
                                             QStringLiteral("保存工程")) : nullptr;
    check(saveProject != nullptr, "find Save Project action");
    if (saveProject) saveProject->trigger();
    check(file.open(QIODevice::ReadOnly), "read saved mixer project");
    const auto saved = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    const auto savedModules = saved.value(QStringLiteral("modules")).toArray();
    int savedInputs = -1;
    for (const auto& module : savedModules) {
        const auto record = module.toObject();
        if (record.value(QStringLiteral("path")).toString().contains(QStringLiteral("mod_digital_mixer.dll"))) {
            QJsonObject mixerSettings;
            if (Patchbay::ProjectStateCodec::restoreModuleSettings(record, mixerSettings))
                savedInputs = mixerSettings.value(QStringLiteral("inputs")).toArray().size();
        }
    }
    if (savedInputs != 2)
        std::fprintf(stderr, "Saved mixer inputs: %d; module records: %lld\n",
                     savedInputs, static_cast<long long>(savedModules.size()));
    check(savedInputs == 2,
          "Save Project writes added mixer channels into the project file");
    observer.close();
    host->shutdown();
    wjn_destroy_node(host);
    host = wjn_create_node(0);
    check(host && host->init(), "restart patchbay host with saved project");
    if (host) {
        host->createWindow(nullptr);
        check(observer.open({"MixerProjectRestartProbe", false, false, nullptr}), "open JACK restart probe");
        for (int attempt = 0; attempt < 50; ++attempt) {
            application.processEvents();
            if (observer.isOpen() && Audio::JackApi::instance().portByName(observer.nativeHandle(), "DigitalMixer:CH2_L")) break;
            QThread::msleep(20);
        }
        check(observer.isOpen() && Audio::JackApi::instance().portByName(observer.nativeHandle(), "DigitalMixer:CH2_L"),
              "new mixer input channel survives patchbay restart");
        observer.close();
        host->shutdown();
        wjn_destroy_node(host);
    }
    return failures ? 1 : 0;
}
}

int main(int argc, char** argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
    QTemporaryDir settingsDirectory;
    check(settingsDirectory.isValid(), "isolate patchbay settings");
    if (!settingsDirectory.isValid()) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    if (argc > 1 && QByteArray(argv[1]) == "--mixer-project-restore")
        return testMixerProjectRestore(application, settingsDirectory.path());
    if (argc > 2 && QByteArray(argv[1]) == "--meterbridge-library") {
        verifyMeterbridgeOpen(application, QString::fromLocal8Bit(argv[2]), check);
        return failures ? 1 : 0;
    }

    {
        using namespace WinJACKNexus::UI;
        QLibrary adapter(QDir(application.applicationDirPath()).filePath(
            QStringLiteral("../nodes/adapter/mod_adapter.dll")));
        using Create = WinJACKNexus::Node::INode* (*)(std::uint32_t);
        using Destroy = void (*)(WinJACKNexus::Node::INode*);
        using Settings = QWidget* (*)(WinJACKNexus::Node::INode*, QWidget*);
        using Clear = void (*)(WinJACKNexus::Node::INode*, QWidget*);
        const auto create = reinterpret_cast<Create>(adapter.resolve("wjn_create_node"));
        const auto destroy = reinterpret_cast<Destroy>(adapter.resolve("wjn_destroy_node"));
        const auto settings = reinterpret_cast<Settings>(adapter.resolve("wjn_create_settings_widget"));
        const auto clear = reinterpret_cast<Clear>(adapter.resolve("wjn_destroy_settings_widget"));
        check(create && destroy && settings && clear, "load WASAPI settings exports");
        if (create && destroy && settings && clear) {
            QMainWindow host;
            host.setCentralWidget(new QWidget(&host));
            auto* dock = new Controls::DockPanel(QStringLiteral("Parameters"), Qt::RightDockWidgetArea, &host);
            auto* content = new QWidget(dock);
            auto* layout = new QVBoxLayout(content);
            layout->setContentsMargins(32, 12, 12, 12);
            layout->addStretch();
            auto* scroll = new Controls::ScrollField(dock);
            scroll->setWidgetResizable(true);
            scroll->setWidget(content);
            dock->setWidget(scroll);
            host.addDockWidget(Qt::RightDockWidgetArea, dock);
            host.resize(1200, 700);
            host.show();
            auto* node = create(0);
            auto* panel = node ? settings(node, content) : nullptr;
            check(panel != nullptr, "create actual WASAPI parameter panel");
            if (panel) {
                auto* device = panel->findChild<QComboBox*>(QStringLiteral("deviceCombo"));
                if (device) device->addItem(QString(120, QLatin1Char('W')));
                layout->insertWidget(0, panel);
            }
            application.processEvents();
            auto* grip = dock->findChild<QWidget*>(QStringLiteral("dockResizeGrip"));
            const auto verifyResize = [&] {
                host.raise();
                application.processEvents();
                const QPoint local(grip->width() / 2, 50);
                const auto global = grip->mapToGlobal(local);
                const auto handle = reinterpret_cast<HWND>(grip->winId());
                RECT nativeRect {};
                check(GetWindowRect(handle, &nativeRect), "locate the dock grip in native screen coordinates");
                const POINT native {(nativeRect.left + nativeRect.right) / 2, nativeRect.top + 50};
                const auto hit = WindowFromPoint(native);
                check(hit == handle || IsChild(handle, hit), "native hit testing reaches dock grip after WASAPI selection");
                const int oldWidth = dock->width();
                QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(grip, &press);
                QMouseEvent move(QEvent::MouseMove, local - QPoint(100, 0), global - QPoint(100, 0), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(grip, &move);
                QMouseEvent release(QEvent::MouseButtonRelease, local, global - QPoint(100, 0), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(grip, &release);
                application.processEvents();
                check(dock->width() > oldWidth + 50, "dock can be widened after WASAPI selection");
                const auto shrinkGlobal = grip->mapToGlobal(local);
                const int shrinkDelta = dock->width() - 180;
                QMouseEvent shrinkPress(QEvent::MouseButtonPress, local, shrinkGlobal, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(grip, &shrinkPress);
                QMouseEvent shrinkMove(QEvent::MouseMove, local + QPoint(shrinkDelta, 0), shrinkGlobal + QPoint(shrinkDelta, 0), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(grip, &shrinkMove);
                QMouseEvent shrinkRelease(QEvent::MouseButtonRelease, local, shrinkGlobal + QPoint(shrinkDelta, 0), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(grip, &shrinkRelease);
                application.processEvents();
                check(dock->width() <= 190, "dock can shrink to 180 px with and after WASAPI settings");
            };
            verifyResize();
            if (panel) {
                application.processEvents();
                check(scroll->horizontalScrollBar()->maximum() == 0,
                      "WASAPI form fits narrow sidebar without horizontal scrolling");
                const auto narrowWidth = panel->findChild<QComboBox*>(QStringLiteral("deviceCombo"))->width();
                for (const auto& name : {"clientNameEditor", "deviceCombo", "channelsSpinBox", "refreshButton", "applyButton"}) {
                    auto* field = panel->findChild<QWidget*>(QString::fromLatin1(name));
                    check(field && field->geometry().right() < panel->width(),
                          "WASAPI settings controls stay inside narrow form width");
                }
                host.resizeDocks({dock}, {480}, Qt::Horizontal);
                application.processEvents();
                check(panel->findChild<QComboBox*>(QStringLiteral("deviceCombo"))->width() > narrowWidth + 150,
                      "WASAPI controls expand with the sidebar width");
                clear(node, panel);
            }
            application.processEvents();
            verifyResize();
            if (node) destroy(node);
        }
    }

    verifyPreMixPolar(check);
    verifyAuxPolarSwitches(check);
    {
        const auto systemFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        const auto systemFamily = systemFont.family();
        const auto initialStyle = application.styleSheet();
        application.setStyleSheet(QStringLiteral("QLabel { font-family: 'Courier New'; }"));
        QLabel startupLabel(QStringLiteral("Startup font"));
        startupLabel.ensurePolished();
        QFont obsolete = systemFont;
        obsolete.setFamily(QStringLiteral("Times New Roman"));
        application.setFont(obsolete);
        WinJACKNexus::UI::MixerWindow startupMixer;
        check(startupMixer.font().family() == obsolete.family(), "prepare a stale application startup font");
        check(startupLabel.font().family() != systemFamily, "prepare an obsolete startup font");
        WinJACKNexus::UI::ThemeCore restored;
        check(restored.setOverride(QStringLiteral("--wjn-theme-global-font-source"),
                                   QStringLiteral("system")), "restore system-font theme before GUI binding");
        WinJACKNexus::UI::ThemeManager startupManager;
        startupManager.bindThemeCore(&restored);
        startupManager.watchApplication(&application);
        check(application.font().family() == systemFamily && startupLabel.font().family() == systemFamily,
              "initial theme binding applies system font without any theme switch");
        check(startupMixer.font().family() == systemFamily,
              "first system-font application replaces stale mixer font with the platform UI font");
        WinJACKNexus::UI::MixerWindow newlyOpenedMixer;
        check(newlyOpenedMixer.font().family() == systemFamily,
              "new mixer windows inherit the actual platform UI font at startup");
        newlyOpenedMixer.ensurePolished();
        check(newlyOpenedMixer.font().family() == systemFamily, "polished mixer preserves startup system font");
        auto paintedFont = newlyOpenedMixer.font();
        paintedFont.setPixelSize(12);
        paintedFont.setWeight(QFont::Normal);
        check(QFontInfo(paintedFont).family() == QFontInfo(systemFont).family(),
              "regular mixer text resolves to the platform UI font");
        QDir().mkpath(QStringLiteral("tests/preview/PrePush"));
        check(newlyOpenedMixer.grab().save(QStringLiteral("tests/preview/PrePush/startup-system-font-mixer.png")),
              "capture the real mixer using the startup platform font");
        application.setFont(systemFont);
        application.setStyleSheet(initialStyle);
    }

    verifyMixerLayout(application, check);
    verifyFxChainPresets(application, check);
    auto* host = wjn_create_node(0);
    check(host && host->init(), "create patchbay host without a JACK client");
    if (!host) return 1;
    const auto handle = host->createWindow(nullptr);
    auto* window = qobject_cast<QMainWindow*>(QWidget::find(reinterpret_cast<WId>(handle)));
    check(window != nullptr, "open patchbay window");
    if (!window) { host->shutdown(); wjn_destroy_node(host); return 1; }
    application.processEvents();
    {
        // The patchbay installs its QApplication QSS after the global font manager.
        // Check the resulting polished mixer, not just the earlier application font.
        const auto systemFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        WinJACKNexus::UI::MixerWindow mixer;
        mixer.ensurePolished();
        check(mixer.font().family() == systemFont.family(),
              "patchbay startup stylesheet preserves the mixer system font");
        const auto glyphFamilies = [](const QFont& font) {
            QTextLayout layout(QStringLiteral("声卡输入 中文 0.0 dB"), font);
            layout.beginLayout();
            auto line = layout.createLine();
            line.setLineWidth(1000);
            layout.endLayout();
            QStringList families;
            for (const auto& run : layout.glyphRuns())
                families.append(run.rawFont().familyName());
            families.sort();
            return families;
        };
        check(glyphFamilies(mixer.font()) == glyphFamilies(systemFont),
              "actual startup Chinese and Latin glyphs use the system UI font fallback chain");
        QDir().mkpath(QStringLiteral("tests/preview/PrePush"));
        check(mixer.grab().save(QStringLiteral("tests/preview/PrePush/real-patchbay-startup-font.png")),
              "capture mixer after the real patchbay startup stylesheet");
    }

    auto* tools = submenu(window->menuBar(), QStringLiteral("工具"));
    auto* audio = submenu(tools, QStringLiteral("音频工具"));
    auto* other = submenu(tools, QStringLiteral("其他工具"));
    check(audio && other && actionNamed(audio, QStringLiteral("电平表桥"))
          && actionNamed(other, QStringLiteral("主题管理器"))
          && actionNamed(other, QStringLiteral("扫描 VST")),
          "Tools menu groups audio and other tools");
    auto* mixer = actionNamed(audio, QStringLiteral("调音台"));
    check(mixer && mixer->isCheckable() && !mixer->isChecked(),
          "resident mixer has an unchecked menu action");
    const auto topLevel = window->menuBar()->actions();
    check(!topLevel.isEmpty() && topLevel.last()->text() == QStringLiteral("帮助")
          && actionNamed(topLevel.last()->menu(), QStringLiteral("日志跟踪"))
          && actionNamed(topLevel.last()->menu(), QStringLiteral("关于")),
          "Help with log tracing and About is the final top-level menu");
    auto* inspectorDock = window->findChild<QDockWidget*>(QStringLiteral("patchbay.inspector"));
    check(inspectorDock && dynamic_cast<WinJACKNexus::UI::Controls::ScrollField*>(inspectorDock->widget()),
          "actual patchbay inspector scrolls module forms without inheriting their minimum widths");
    bool listedMixer = false;
    for (auto* tree : window->findChildren<QTreeWidget*>())
        for (auto* item : tree->findItems(QStringLiteral("Digital Mixer"),
                 Qt::MatchExactly | Qt::MatchRecursive, 0))
            listedMixer |= !item->data(0, Qt::UserRole).toString().isEmpty();
    check(!listedMixer, "node library excludes the resident mixer");
    for (auto* tree : window->findChildren<QTreeWidget*>())
        check(tree->findItems(QStringLiteral("jackclient"),
                  Qt::MatchExactly | Qt::MatchRecursive, 0).isEmpty(),
              "node library excludes the JACK client category");

    const auto projectPath = QDir(settingsDirectory.path()).filePath(QStringLiteral("mixer.wjnproj"));
    const auto project = QJsonObject {
        {QStringLiteral("version"), 1},
        {QStringLiteral("app"), QStringLiteral("WinJACKNexus")},
        {QStringLiteral("clients"), QJsonArray {}},
        {QStringLiteral("ports"), QJsonArray {}},
        {QStringLiteral("connections"), QJsonArray {}},
        {QStringLiteral("layouts"), QJsonArray {}},
        {QStringLiteral("modules"), QJsonArray {QJsonObject {
            {QStringLiteral("path"), QStringLiteral("nodes/mixer/mod_digital_mixer.dll")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("client"), QStringLiteral("DigitalMixer")}
        }}},
        {QStringLiteral("vsts"), QJsonArray {}},
        {QStringLiteral("subgraphs"), QJsonArray {}}
    };
    QFile file(projectPath);
    check(file.open(QIODevice::WriteOnly)
          && file.write(QJsonDocument(project).toJson()) > 0, "create project with a mixer node");
    file.close();

    bool selectedFile = false;
    bool sawMixerPrompt = false;
    bool savedPrompt = false;
    QTimer dialogDriver;
    dialogDriver.setInterval(20);
    QObject::connect(&dialogDriver, &QTimer::timeout, &application, [&] {
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (!widget->isVisible()) continue;
            if (auto* dialog = qobject_cast<QFileDialog*>(widget)) {
                dialog->selectFile(projectPath);
                static_cast<QDialog*>(dialog)->accept();
                selectedFile = true;
            } else if (auto* box = qobject_cast<QMessageBox*>(widget)) {
                if (box->windowTitle() == QStringLiteral("启用调音台")) {
                    sawMixerPrompt = true;
                    QDir().mkpath(QStringLiteral("tests/preview/PrePush"));
                    savedPrompt = box->grab().toImage().save(
                        QStringLiteral("tests/preview/PrePush/patchbay-project-mixer-prompt-unmodified.png"));
                    if (auto* no = box->button(QMessageBox::No)) no->click();
                } else if (box->windowTitle() == QStringLiteral("未保存的工程")) {
                    if (auto* discard = box->button(QMessageBox::Discard)) discard->click();
                }
            }
        }
    });
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &application, [] {
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* dialog = qobject_cast<QDialog*>(widget); dialog && dialog->isVisible()) dialog->reject();
    });
    auto* openProject = actionNamed(submenu(window->menuBar(), QStringLiteral("文件")), QStringLiteral("打开工程..."));
    check(openProject != nullptr, "File menu has Open Project");
    if (openProject) {
        dialogDriver.start();
        timeout.start(8000);
        openProject->trigger();
        timeout.stop();
        dialogDriver.stop();
        check(selectedFile && sawMixerPrompt && savedPrompt && mixer && !mixer->isChecked(),
              "loading a mixer project while its tool is off asks whether to enable it");
    }

    WinJACKNexus::UI::TrayController tray;
    if (tray.initialize()) {
        QMenu themeMenu;
        themeMenu.addAction(QStringLiteral("测试主题"));
        tray.setQuickAccessThemeMenu(&themeMenu);
        tray.setThemeLoadingProgress(45);
        WinJACKNexus::UI::TrayController::setNodeGuiEntries({});
        auto* quickAction = actionNamed(tray.contextMenu(), QStringLiteral("快速入口"));
        QToolButton* themeButton = nullptr;
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* candidate = widget->findChild<QToolButton*>(QStringLiteral("quickAccessThemeButton")))
                themeButton = candidate;
        auto* themeProgress = themeButton ? themeButton->window()->findChild<QProgressBar*>(
            QStringLiteral("quickAccessThemeProgress")) : nullptr;
        check(themeProgress && themeProgress->isVisible() && themeProgress->value() == 45,
              "quick access shows theme loading progress below the buttons");
        tray.setThemeLoadingProgress(100);
        auto* fileButton = themeButton ? themeButton->window()->findChild<QToolButton*>(
            QStringLiteral("quickAccessFileButton")) : nullptr;
        auto* fileMenu = submenu(window->menuBar(), QStringLiteral("文件"));
        check(fileButton && fileButton->isEnabled() && fileButton->menu() != fileMenu
              && fileButton->popupMode() == QToolButton::InstantPopup
              && actionNamed(fileButton->menu(), QStringLiteral("打开工程...")) == openProject,
              "quick access has a native File menu sharing project actions");
        check(fileButton && !actionNamed(fileButton->menu(), QStringLiteral("关闭窗口"))
              && actionNamed(fileMenu, QStringLiteral("关闭窗口")),
              "quick access excludes Close while the patchbay menu retains it");
        auto* originalQuickFileMenu = fileButton ? fileButton->menu() : nullptr;
        auto* temporaryMenu = new QMenu;
        WinJACKNexus::UI::TrayController::setQuickAccessFileMenu(temporaryMenu);
        check(fileButton && fileButton->menu() == temporaryMenu, "File menu binding updates live bars");
        delete temporaryMenu;
        check(fileButton && !fileButton->isEnabled() && !fileButton->menu(),
              "destroyed File menu safely disables the entry");
        WinJACKNexus::UI::TrayController::setQuickAccessFileMenu(originalQuickFileMenu);
        if (fileButton) {
            QDir().mkpath(QStringLiteral("tests/preview/PrePush"));
            check(fileButton->window()->grab().save(
                QStringLiteral("tests/preview/PrePush/quick-access-file-entry.png")),
                "capture the quick access File entry");
        }
        check(themeProgress && !themeProgress->isVisible(),
              "quick access hides theme loading progress when complete");
        auto* hideButton = themeButton
            ? themeButton->window()->findChild<QToolButton*>(QStringLiteral("quickAccessHideButton"))
            : nullptr;
        auto* pinButton = themeButton
            ? themeButton->window()->findChild<QToolButton*>(QStringLiteral("quickAccessPinButton"))
            : nullptr;
        check(quickAction && quickAction->isEnabled() && themeButton
              && themeButton->isEnabled() && themeButton->menu() == &themeMenu
              && themeButton->size() == QSize(28, 28) && hideButton
              && themeButton->geometry().left() > hideButton->geometry().left(),
              "quick access keeps the final icon-only theme switch available with no loaded nodes");
        check(tray.mixerAction() && !tray.mixerAction()->isEnabled(),
              "quick access mixer starts disabled");
        WinJACKNexus::UI::TrayController::setNodeGuiEntries({{
            QStringLiteral("mixer"), QStringLiteral("Digital Mixer"),
            QStringLiteral("DigitalMixer"), QStringLiteral("调音台"), [] { return true; }
        }});
        check(tray.mixerAction()->isEnabled(), "quick access mixer follows loaded node state");
        WinJACKNexus::UI::TrayController::setNodeGuiEntries({});
        check(!tray.mixerAction()->isEnabled(), "quick access mixer clears when node is removed");
        if (themeButton && pinButton) {
            QDir().mkpath(QStringLiteral("tests/preview/PrePush"));
            const QRect icons(pinButton->geometry().left() - 3,
                              pinButton->geometry().top() - 3,
                              themeButton->geometry().right() - pinButton->geometry().left() + 7,
                              themeButton->height() + 6);
            check(themeButton->window()->grab(icons).toImage().save(
                QStringLiteral("tests/preview/PrePush/patchbay-quick-access-theme-unmodified.png")),
                "save only the quick access icon controls");
        }
        tray.shutdown();
        application.processEvents();
    } else {
        std::fprintf(stderr, "SKIP: system tray unavailable; quick access popup not verified\n");
    }

    host->shutdown();
    wjn_destroy_node(host);
    return failures ? 1 : 0;
}
