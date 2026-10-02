/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/Audio/JackApi.hpp>
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Common/DigitalMixerState.hpp>
#include <mod_patchbay/ProjectStateCodec.hpp>
#include <WinJACKNexus/UI/TrayController.hpp>
#include "StandardChannelNamesRegression.hpp"
#include "HiddenSelChannelsRegression.hpp"
#include "SubMixRegression.hpp"
#include "MixerOverviewRegression.hpp"
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
#include <QPainter>
#include <QImage>
#include <QLabel>
#include <QKeyEvent>
#include <QHelpEvent>
#include <QToolTip>
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
extern "C" int wjn_patchbay_show_digital_mixer_view(int view);

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
        QLibrary mixer(QDir(application.applicationDirPath()).filePath(QStringLiteral("../nodes/mixer/mod_digital_mixer.dll")));
        using Show=int (*)(WinJACKNexus::Node::INode*,int);
        const auto show=reinterpret_cast<Show>(mixer.resolve("wjn_digital_mixer_show_view"));
        check(show&&show(nullptr,0)==0&&show(nullptr,1)==0&&show(nullptr,2)==0,"optional mixer view export rejects null nodes without JACK");
        check(wjn_patchbay_show_digital_mixer_view(-1)==-1&&wjn_patchbay_show_digital_mixer_view(3)==-1,
            "patchbay view bridge rejects unsupported view identifiers");
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
                POINT native {(nativeRect.left + nativeRect.right) / 2, nativeRect.top + 50};
                const auto nativeParent = GetParent(handle);
                ScreenToClient(nativeParent, &native);
                const auto hit = ChildWindowFromPointEx(nativeParent, native, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED);
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
                const auto verifyLabels = [&] {
                    for (const auto& text : {"Client name", "Device", "JACK channels"}) {
                        QLabel* label = nullptr;
                        for (auto* candidate : panel->findChildren<QLabel*>())
                            if (candidate->text() == QString::fromLatin1(text)) label = candidate;
                        check(label && label->isVisible() && label->width() > 0
                              && label->height() >= label->fontMetrics().height(),
                              "WASAPI form labels remain visible at narrow and wide sidebar widths");
                        if (label) {
                            const auto pixels = label->grab().toImage().convertToFormat(QImage::Format_RGB32);
                            bool varied = false;
                            for (int y = 0; y < pixels.height() && !varied; ++y)
                                for (int x = 0; x < pixels.width(); ++x)
                                    if (pixels.pixel(x, y) != pixels.pixel(0, 0)) { varied = true; break; }
                            check(varied, "WASAPI labels actually paint text pixels");
                        }
                    }
                };
                verifyLabels();
                for (const auto& name : {"clientNameEditor", "deviceCombo", "channelsSpinBox", "refreshButton", "applyButton"}) {
                    auto* field = panel->findChild<QWidget*>(QString::fromLatin1(name));
                    check(field && field->geometry().right() < panel->width(),
                          "WASAPI settings controls stay inside narrow form width");
                }
                host.resizeDocks({dock}, {480}, Qt::Horizontal);
                application.processEvents();
                check(panel->findChild<QComboBox*>(QStringLiteral("deviceCombo"))->width() > narrowWidth + 150,
                      "WASAPI controls expand with the sidebar width");
                verifyLabels();
                clear(node, panel);
            }
            application.processEvents();
            verifyResize();
            if (node) destroy(node);
        }
    }

    {
        auto state = std::make_shared<WinJACKNexus::Common::AuxMixerState>();
        WinJACKNexus::UI::AuxSettingsWindow aux(state);
        const auto source = QStringLiteral("aux-list-controls");
        aux.setSource(source, source);
        aux.resize(1360, 813);
        aux.show();
        auto* bus = state->busForSource(source, aux.selectedBusId());
        check(bus && !bus->sends.empty(), "AUX list numeric regression has channel sends");
        if (bus && !bus->sends.empty()) {
            int commits = 0;
            aux.setStateChangedCallback([&] { ++commits; });
            const QPointF readout(190, 125);
            QMouseEvent click(QEvent::MouseButtonPress, readout, readout, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&aux, &click);
            auto* editor = aux.findChild<QLineEdit*>(QStringLiteral("numericValueEditor"));
            check(editor && editor->isVisible(), "AUX list readout opens the shared numeric editor");
            if (editor) {
                editor->setText(QStringLiteral("-8.5"));
                QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                QApplication::sendEvent(editor, &enter);
                for (const auto& send : bus->sends)
                    check(std::abs(send.levelDb + 8.5) < .001, "AUX list numeric editing updates every channel");
            }
            const QPointF knob(190, 98);
            QMouseEvent press(QEvent::MouseButtonPress, knob, knob, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&aux, &press);
            QMouseEvent reset(QEvent::MouseButtonDblClick, knob, knob, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&aux, &reset);
            QMouseEvent move(QEvent::MouseMove, knob - QPointF(0, 20), knob, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&aux, &move);
            for (const auto& send : bus->sends)
                check(std::abs(send.levelDb) < .001, "AUX list double-click restores 0 dB and stops dragging");
            check(commits >= 2, "AUX list edits publish state changes");
        }
    }

    {
        WinJACKNexus::Common::DigitalMixerState state;
        const auto source = state.add(WinJACKNexus::Common::MixerStripKind::input, WinJACKNexus::Common::MixerLayout::stereo);
        state.strip(source)->name = QStringLiteral("浏览器");
        const auto target = state.add(WinJACKNexus::Common::MixerStripKind::aux, WinJACKNexus::Common::MixerLayout::stereo);
        state.strip(target)->name = QStringLiteral("声卡输出1");
        const auto removed = state.add(WinJACKNexus::Common::MixerStripKind::aux, WinJACKNexus::Common::MixerLayout::stereo);
        const auto last = state.add(WinJACKNexus::Common::MixerStripKind::aux, WinJACKNexus::Common::MixerLayout::surround71);
        state.strip(last)->name = QStringLiteral("耳机环绕");
        state.remove(removed);
        WinJACKNexus::UI::MixerWindow mixer(&state, [&state](const auto& next, QString*) { state = next; return true; });
        mixer.show();
        for (auto* action : mixer.menuBar()->actions())
            if (action->text() == QStringLiteral("查看大屏") && action->menu()) action->menu()->actions().front()->trigger();
        auto* tab = mixer.findChild<QPushButton*>(QStringLiteral("selTabAUX"));
        check(tab != nullptr, "open actual mixer AUX tab for route naming");
        if (tab) tab->click();
        WinJACKNexus::UI::AuxSettingsWindow* aux = nullptr;
        for (auto* widget : mixer.findChildren<QWidget*>())
            if (auto* candidate = dynamic_cast<WinJACKNexus::UI::AuxSettingsWindow*>(widget)) aux = candidate;
        check(aux != nullptr, "actual mixer creates its AUX route list");
        if (aux) {
            application.processEvents(); aux->grab();
            check(aux->accessibleDescription().contains(QStringLiteral("浏览器 (立体声) → 声卡输出1 (立体声)"))
                  && aux->accessibleDescription().contains(QStringLiteral("浏览器 (立体声) → 耳机环绕 (7.1)"))
                  && !aux->accessibleDescription().contains(QStringLiteral("AUX 2")),
                  "rendered AUX routes use custom source and target names despite numbering gaps");
            if (auto* name = mixer.findChild<QLineEdit*>(QStringLiteral("selChannelName"))) {
                name->setText(QStringLiteral("浏览器改名"));
                QMetaObject::invokeMethod(name, "editingFinished", Qt::DirectConnection);
                application.processEvents(); aux->grab();
                check(aux->accessibleDescription().contains(QStringLiteral("浏览器改名 (立体声)")),
                      "renaming a source updates cached AUX route labels");
            }
        }
        auto shared = std::make_shared<WinJACKNexus::Common::AuxMixerState>();
        auto& cached = shared->busesForSource(QStringLiteral("cached-name"));
        const auto id = cached.front().id;
        cached.front().sends.front().levelDb = -9;
        shared->bus(id)->name = QStringLiteral("目标改名");
        auto* refreshed = shared->busForSource(QStringLiteral("cached-name"), id);
        check(refreshed->name == QStringLiteral("目标改名") && refreshed->sends.front().levelDb == -9,
              "target renames synchronize cached labels while retaining send parameters");
    }

    {
        using namespace WinJACKNexus::Common;
        DigitalMixerState model;
        const auto source = model.add(MixerStripKind::input, MixerLayout::stereo);
        model.strip(source)->mixToMain = true;
        std::array<float, 512> audio;
        audio.fill(.25F);
        std::array<const float*, 2> inputs {audio.data(), audio.data()};
        std::array<std::array<float, 512>, 4> result {};
        std::array<float*, 4> outputs;
        for (int i=0;i<4;++i) outputs[i]=result[i].data();
        for (auto mode : {MixerListenMode::off, MixerListenMode::pfl, MixerListenMode::afl}) {
            model.strip(source)->listenMode=mode;
            DigitalMixerProcessor processor;
            check(processor.configure(model, 512) && processor.process(inputs, outputs, 512),
                  "process monitor routing without JACK");
            const auto& levels=processor.levels();
            check(!levels.empty() && levels.back().postPeak > 0,
                  "MIX independently reaches MAIN");
            check(levels.size()>1 && (mode==MixerListenMode::off
                ? levels[levels.size()-2].postPeak==0 : levels[levels.size()-2].postPeak>0),
                  "monitor is silent for MIX alone and only receives PFL or AFL");
        }
        auto shared = std::make_shared<AuxMixerState>();
        const auto targetId = shared->buses().front().id;
        const auto longName = QStringLiteral("超长的自定义耳机监听输出通道名称用于检查省略显示");
        shared->bus(targetId)->name = longName;
        WinJACKNexus::UI::AuxSettingsWindow aux(shared);
        aux.setSource(QStringLiteral("tooltip"), QStringLiteral("浏览器完整源名称"));
        aux.resize(1360,813); aux.show();
        aux.grab();
        const QPoint point(120,90);
        QHelpEvent help(QEvent::ToolTip,point,aux.mapToGlobal(point));
        QApplication::sendEvent(&aux,&help);
        check(QToolTip::text().contains(longName) && QToolTip::text().contains(QStringLiteral("浏览器完整源名称")),
              "AUX tooltip shows full custom source and target names");
        QToolTip::hideText();
    }

    {
        using namespace WinJACKNexus::Common;
        for (bool listenAux : {false, true}) {
            DigitalMixerState model;
            const auto input = model.add(MixerStripKind::input, MixerLayout::stereo);
            const auto target = listenAux ? model.add(MixerStripKind::aux, MixerLayout::stereo) : input;
            if (listenAux) {
                model.strip(input)->auxSendsEnabled = true;
                model.setSend(input, {target, true, true, defaultMixerPanner(MixerLayout::stereo, MixerLayout::stereo)});
            }
            model.strip(target)->inputGainDb = 6;
            std::array<float, 512> audio;
            audio.fill(.1F);
            std::array<const float*, 2> inputs {audio.data(), audio.data()};
            std::array<std::array<float, 512>, 6> result {};
            std::array<float*, 6> pointers;
            for (int i = 0; i < 6; ++i) pointers[i] = result[i].data();
            for (auto mode : {MixerListenMode::pfl, MixerListenMode::afl}) {
                for (bool mute : {false, true}) {
                    model.strip(target)->listenMode = mode;
                    model.strip(target)->mute = mute;
                    model.strip(target)->faderPosition = mixerUnityFaderPosition;
                    DigitalMixerProcessor processor;
                    check(processor.configure(model, 512), "configure input/AUX pre-FX PFL regression");
                    auto effectedId = target;
                    processor.setEffectProcessor(&effectedId,
                        [](void* context, std::uint64_t id, std::uint32_t, float* samples, std::size_t frames) noexcept {
                            if (id != *static_cast<std::uint64_t*>(context)) return;
                            for (std::size_t i = 0; i < frames; ++i) samples[i] *= .25F;
                        });
                    check(processor.process(inputs, std::span(pointers.data(), processor.outputPortCount()), 512),
                          "process actual input/AUX FX before monitor routing");
                    const auto monitor = processor.levels()[processor.levels().size() - 2].postPeak;
                    const auto expected = mode == MixerListenMode::pfl ? .1 * std::pow(10., 6. / 20.)
                        : mute ? 0. : .025 * std::pow(10., 6. / 20.);
                    check(std::abs(monitor - expected) < .0001,
                          "PFL retains gain and bypasses FX/mute while AFL follows FX and mute");
                }
            }
            model.strip(target)->listenMode = MixerListenMode::pfl;
            model.strip(target)->faderPosition = 0;
            DigitalMixerProcessor silentFader;
            check(silentFader.configure(model,512)
                  && silentFader.process(inputs,std::span(pointers.data(),silentFader.outputPortCount()),512)
                  && silentFader.levels()[silentFader.levels().size()-2].postPeak > .19F,
                  "input/AUX PFL remains audible with fader fully down");
        }
    }

    {
        using namespace WinJACKNexus;
        auto state=std::make_shared<Common::AuxMixerState>();
        UI::AuxSettingsWindow aux(state);
        const auto key=QStringLiteral("aux-channel-meters");
        aux.setSource(key,key,0,Common::AuxChannelLayout::surround71);
        auto* bus=state->busForSource(key,aux.selectedBusId());
        bus->enabled=true;
        aux.setChainEnabled(true);
        std::array<int,8> reads {};
        aux.setMeterReadCallback([](bool) {return 1.F;});
        aux.setChannelMeterReadCallback([&](bool, std::size_t channel) {
            ++reads[channel];
            return channel==0?.5F:channel==2?.125F:0.F;
        });
        QEventLoop loop;
        QTimer::singleShot(150,&loop,&QEventLoop::quit);
        loop.exec();
        check(bus->sends.size()==8 && reads[0]>0 && reads[7]>0,
              "AUX meters request all eight source channel peaks independently");
        check(bus->sends[0].meterDb > bus->sends[2].meterDb+8
              && bus->sends[1].meterDb <= -71 && bus->sends[7].meterDb <= -71,
              "AUX L and C show different levels while silent channels stay silent");
        aux.setChannelMeterReadCallback({});
        Common::DigitalMixerState model;
        const auto source=model.add(Common::MixerStripKind::input,Common::MixerLayout::surround71);
        model.add(Common::MixerStripKind::aux,Common::MixerLayout::surround71);
        int channelReads=0;
        UI::MixerWindow mixer(&model,[&](const auto& value,QString*){model=value;return true;},
            [](std::uint64_t,bool){return 1.F;}, {},
            [&](std::uint64_t id,bool,std::size_t channel){if(id==source&&channel<8)++channelReads;return channel==0?.5F:0.F;});
        mixer.show();
        for(auto* action:mixer.menuBar()->actions())
            if(action->text()==QStringLiteral("查看大屏") && action->menu())action->menu()->actions().front()->trigger();
        if(auto* tab=mixer.findChild<QPushButton*>(QStringLiteral("selTabAUX")))tab->click();
        const auto before=channelReads;
        QEventLoop refresh;
        QTimer::singleShot(150,&refresh,&QEventLoop::quit);
        refresh.exec();
        check(channelReads > before,"actual mixer AUX view calls its per-channel provider");
    }

    {
        // Endpoint labels must stay centered on their ticks, even outside the track.
        for (const bool above : {false, true}) {
            for (const bool maximum : {false, true}) {
                QImage image(360, 100, QImage::Format_ARGB32);
                image.fill(Qt::black);
                WinJACKNexus::UI::Controls::MeterStyle style;
                style.ticksAbove = above;
                style.tickBandPixels = 24;
                style.labelColor = Qt::white;
                style.tickColor = Qt::green;
                style.ticks = {{maximum ? 12.0 : -72.0,
                    maximum ? QStringLiteral("+12") : QStringLiteral("-72")}};
                QPainter painter(&image);
                WinJACKNexus::UI::Controls::drawMeter(painter, QRectF(30, 20, 300, 60),
                    -72.0, std::nullopt, style);
                painter.end();
                int left = image.width(), right = -1;
                const int top = above ? 20 : 64;
                const int bottom = above ? 36 : 78;
                for (int y = top; y < bottom; ++y) {
                    for (int x = 0; x < image.width(); ++x) {
                        const QColor pixel = image.pixelColor(x, y);
                        if (pixel.red() > 100 && pixel.green() > 100 && pixel.blue() > 100) {
                            left = std::min(left, x);
                            right = std::max(right, x);
                        }
                    }
                }
                check(right >= left && std::abs((left + right) * .5 - (maximum ? 330 : 30)) <= 2,
                    "horizontal meter endpoint text centers align with endpoint ticks");
            }
        }
    }
    verifySubMix(check);
    verifyMixerOverview(check);
    verifyHiddenSelChannels(check);
    verifyStandardChannelNames(check);
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
        auto* mixerButton=themeButton?themeButton->window()->findChild<QToolButton*>(QStringLiteral("quickAccessMixerButton")):nullptr;
        check(mixerButton&&mixerButton->menu()&&mixerButton->popupMode()==QToolButton::InstantPopup
            &&mixerButton->menu()->actions().size()==3
            &&mixerButton->menu()->actions()[0]->text()==QStringLiteral("调音台")
            &&mixerButton->menu()->actions()[1]==tray.mixerDetailsAction()
            &&mixerButton->menu()->actions()[2]==tray.mixerOverviewAction(),"quick access mixer native menu exposes three independent views");
        std::array<int,3> openedViews{};
        QObject::connect(tray.mixerAction(),&QAction::triggered,[&] {++openedViews[0];});
        QObject::connect(tray.mixerDetailsAction(),&QAction::triggered,[&] {++openedViews[1];});
        QObject::connect(tray.mixerOverviewAction(),&QAction::triggered,[&] {++openedViews[2];});
        check(tray.mixerAction() && !tray.mixerAction()->isEnabled(),
              "quick access mixer starts disabled");
        WinJACKNexus::UI::TrayController::setNodeGuiEntries({{
            QStringLiteral("mixer"), QStringLiteral("Digital Mixer"),
            QStringLiteral("DigitalMixer"), QStringLiteral("调音台"), [] { return true; }
        }});
        check(tray.mixerAction()->isEnabled(), "quick access mixer follows loaded node state");
        if(mixerButton)for(auto* action:mixerButton->menu()->actions())action->trigger();
        check(openedViews==std::array<int,3>{1,1,1},"quick menu actions dispatch each view exactly once");
        WinJACKNexus::UI::TrayController::setNodeGuiEntries({});
        check(!tray.mixerAction()->isEnabled()&&!tray.mixerDetailsAction()->isEnabled()&&!tray.mixerOverviewAction()->isEnabled(), "all mixer views disable when node is removed");
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
