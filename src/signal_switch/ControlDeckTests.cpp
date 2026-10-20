/* SPDX-License-Identifier: AGPL-3.0-only */
#include "../../Develop/Node/WindowShowCounter.hpp"
#include "../patchbay/mod/src/Library.cpp"
#include "Sound.hpp"
#include <QFontDatabase>
#include <QGraphicsOpacityEffect>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>
#include <QTextLayout>
#include <WinJACKNexus/UI/ConsoleControls.hpp>
#include <WinJACKNexus/UI/FxLevelMeterWidget.hpp>
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
    QTest::qWait(160);
}
bool until(const std::function<bool()> &ready)
{
    QElapsedTimer clock;
    clock.start();
    while (!ready() && clock.elapsed() < 15000)
        QTest::qWait(20);
    return ready();
}
HWND childWindow(DWORD pid)
{
    struct Search
    {
        DWORD pid;
        HWND handle{};
    } search{pid};
    EnumWindows(
        [](HWND handle, LPARAM data) -> BOOL {
            auto &search = *reinterpret_cast<Search *>(data);
            DWORD pid{};
            GetWindowThreadProcessId(handle, &pid);
            if (pid == search.pid && IsWindowVisible(handle))
            {
                search.handle = handle;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.handle;
}
struct Foreground
{
    QProcess process;
    Foreground()
    {
        process.setProcessChannelMode(QProcess::ForwardedChannels);
        process.start(QCoreApplication::applicationFilePath(), {"--foreground"});
        check(process.waitForStarted(), "start owned independent foreground test process");
    }
    ~Foreground()
    {
        // This is our helper, never the user's running application/audio process.
        if (const auto handle = childWindow(DWORD(process.processId())))
            PostMessageW(handle, WM_CLOSE, 0, 0);
        if (!process.waitForFinished(3000))
        {
            process.kill();
            process.waitForFinished();
        }
    }
};

struct AudioBlock
{
    std::array<std::array<float, 64>, 8> input{}, output{};
    std::array<float *, 8> inPointers{};
    std::array<float *, 8> outPointers{};
    std::array<WjnNodeAudioBufferV1, 8> inputs{};
    std::array<WjnNodeAudioBufferV1, 8> outputs{};
    WjnNodeProcessV1 block{};
    AudioBlock(unsigned channels, bool source = false)
    {
        block.structSize = sizeof(block);
        block.frames = 64;
        for (unsigned channel = 0; channel < channels; ++channel)
        {
            input[channel].fill(float(channel + 1) / 10);
            inPointers[channel] = input[channel].data();
            outPointers[channel] = output[channel].data();
            inputs[channel] = {channel + 1, 1, &inPointers[channel]};
            outputs[channel] = {source ? channel + 1 : channels + channel + 1, 1, &outPointers[channel]};
        }
        block.audioInputs = inputs.data();
        block.audioInputCount = source ? 0 : channels;
        block.audioOutputs = outputs.data();
        block.audioOutputCount = channels;
    }
};
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    if (app.arguments().contains("--hotkey-probe"))
    {
        check(app.platformName() == "windows", "hotkey regression uses Windows backend");
        QWidget window;
        window.setWindowTitle("Control Deck native shortcut regression");
        window.resize(360, 120);
        window.show();
        settle();
        check(window.isVisible() && window.devicePixelRatioF() == 1, "visible 100 percent regression window");
        SS::Sound sound;
        SS::Service &service = SS::Service::instance();
        WinJACKNexus::Common::SignalSwitch::Gate gate;
        SS::Endpoint endpoint;
        endpoint.id = "native-shortcut-regression";
        endpoint.gate = &gate;
        endpoint.sound = &sound;
        service.attach(&sound, endpoint);
        check(sound.load(QString::fromUtf8(SWITCH_SOURCE_DIR) + "/build/ControlDeck-fixtures/tone.wav"),
              "load sound for actual trigger assertion");
        check(until([&] {
                  sound.poll();
                  return !sound.loading();
              }) &&
                  sound.ready(),
              "regression sound ready");
        QString error;
        check(service.setShortcut(endpoint.id, "Ctrl+Alt+F9", &error), "native filter and first hotkey installed");
        check(PostThreadMessageW(GetCurrentThreadId(), WM_HOTKEY, 0x4000, 0),
              "dispatch real Windows thread-level hotkey message");
        settle();
        float left[64]{}, right[64]{};
        sound.render(left, right, 64, 48000);
        check(std::any_of(std::begin(left), std::end(left), [](float value) { return value != 0; }),
              "thread hotkey triggers decoded audio without a return-pointer crash");
        service.detach(&sound);
        service.updateShortcuts();
        window.close();
        return 0;
    }
    if (app.arguments().contains("--foreground"))
    {
        QWidget window;
        window.setWindowTitle("Control Deck foreground shortcut test");
        window.setGeometry(1000, 220, 320, 220);
        window.show();
        QTimer::singleShot(20000, &app, &QApplication::quit);
        QObject::connect(&app, &QApplication::lastWindowClosed, &app, &QApplication::quit);
        return app.exec();
    }
    WindowShowCounter shows;
    app.installEventFilter(&shows);
    try
    {
        check(app.platformName() == "windows", "real Windows Qt backend");
        check(GetModuleHandleW(L"control_deck.dll") != nullptr, "actual main module is control_deck.dll");
        QTemporaryDir temporary;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
        UI::I18nManager language({temporary.filePath("language.ini"), {"en-US"}, false});
        QString error;
        check(language.install(&error), "production i18n API installs");
        UI::ThemeCore theme;
        UI::ThemeManager::installThemeCore(&theme);
        UI::ThemeManager adapter;
        adapter.bindThemeCore(&theme);
        adapter.applyToApp(&app);
        auto &service = SS::Service::instance();
        check(service.installUI(&error), "Control Deck resource contracts install");
        const auto discovered = scanNodeModules();
        check(std::count_if(discovered.begin(), discovered.end(),
                            [](const auto &node) {
                                return node.category == "Control Deck" &&
                                       node.modulePath.contains("/nodes/control_deck/");
                            }) == 11 &&
                  std::none_of(discovered.begin(), discovered.end(),
                               [](const auto &node) { return node.modulePath.contains("/nodes/signal_switch/"); }),
              "discovery shows eleven Control Deck nodes without legacy folder duplicates");
        std::map<QString, std::unique_ptr<LoadedModuleNode>> nodes;
        QMap<QString, QString> ids;
        for (const auto *kind : {"mono", "sound", "peak", "gain_mono", "gain_stereo", "gain_5_1", "gain_7_1"})
        {
            const auto path = QCoreApplication::applicationDirPath() + "/nodes/control_deck/" + kind +
                              "/wjn_signal_switch_" + kind + ".dll";
            auto legacyPath = path;
            legacyPath.replace("/nodes/control_deck/", "/nodes/signal_switch/");
            auto node = LoadedModuleNode::create(legacyPath, 0, {}, WJN_NODE_MODE_VIRT, 48000, 64);
            check(bool(node), "actual host loads headless dual-mode module offline");
            check(node->modulePath() == path && node->category() == "Control Deck",
                  "legacy project path resolves to renamed Control Deck module");
            node->setContextId("main/sub");
            const auto processor = node->virtualProcessor();
            QLibrary module(path);
            check(module.load(), "open module ABI for main/subgraph capability validation");
            const auto metadata =
                reinterpret_cast<int32_t (*)(WjnNodeMetadataV4 *)>(module.resolve("wjn_get_node_metadata_v4"));
            const auto create =
                reinterpret_cast<WinJACKNexus::Node::INode *(*)(uint32_t)>(module.resolve("wjn_create_node_v4"));
            const auto destroy =
                reinterpret_cast<void (*)(WinJACKNexus::Node::INode *)>(module.resolve("wjn_destroy_node"));
            WjnNodeMetadataV4 info{sizeof(info)};
            check(metadata && create && destroy && metadata(&info) && info.capabilities == 3 && info.features == 0,
                  "module explicitly advertises JACK and VIRT without a GUI feature");
            auto *jack = create(WJN_NODE_MODE_JACK);
            check(jack && !jack->createWindow(nullptr),
                  "main-graph factory also creates a headless node without starting JACK");
            destroy(jack);
            ids[kind] = service.endpoints[processor.instance].id;
            check(!static_cast<WinJACKNexus::Node::INode *>(processor.instance)->createWindow(nullptr),
                  "node has no own GUI");
            nodes[kind] = std::move(node);
        }
        for (const auto *kind : {"gain_mono", "gain_stereo", "gain_5_1", "gain_7_1"})
        {
            const auto processor = nodes[kind]->virtualProcessor();
            const unsigned channels = processor.api->portCount(processor.instance) / 2;
            check(channels == (QByteArray(kind) == "gain_mono"     ? 1u
                               : QByteArray(kind) == "gain_stereo" ? 2u
                               : QByteArray(kind) == "gain_5_1"    ? 6u
                                                                   : 8u),
                  "gain version has exact channel count");
            AudioBlock audio(channels);
            processor.api->process(processor.instance, &audio.block);
            check(audio.output == audio.input, "gain node defaults to unity for every channel");
            check(service.setGainRange(ids[kind], -30, 18) && service.setGain(ids[kind], 6.020599913),
                  "set independent gain range and dB value");
            processor.api->process(processor.instance, &audio.block);
            bool correct = true;
            for (unsigned channel = 0; channel < channels; ++channel)
                for (unsigned frame = 0; frame < 64; ++frame)
                    correct &= std::abs(audio.output[channel][frame] - audio.input[channel][frame] * 2) < .00001f;
            check(correct, "one knob applies correct gain to all channels without changing mapping");
            check(!service.setGainRange(ids[kind], 10, -10) && !service.setGain(ids[kind], 19),
                  "reject invalid range or out-of-range gain");
        }
        const auto peakProcessor = nodes["peak"]->virtualProcessor();
        check(peakProcessor.api->portCount(peakProcessor.instance) == 1, "meter exposes exactly one input port");
        WjnNodePortV1 meterPort{sizeof(meterPort)};
        check(peakProcessor.api->port(peakProcessor.instance, 0, &meterPort) && meterPort.id == 1 &&
                  meterPort.direction == WJN_NODE_PORT_INPUT && meterPort.type == WJN_NODE_PORT_AUDIO &&
                  meterPort.channels == 1 && QByteArray(meterPort.name) == "IN_MONO" &&
                  !peakProcessor.api->port(peakProcessor.instance, 1, &meterPort),
              "mono meter descriptor has no output port");
        AudioBlock meterAudio(1);
        meterAudio.block.audioOutputCount = 0;
        meterAudio.block.audioOutputs = nullptr;
        meterAudio.input[0].fill(4);
        peakProcessor.api->process(peakProcessor.instance, &meterAudio.block);
        check(meterAudio.input[0][0] == 4 && service.find(ids["peak"])->peak->load() == 4,
              "input-only mono meter captures peaks without requiring an output buffer");
        auto *editor = service.showEditor();
        editor->setGeometry(60, 110, 900, 650);
        service.state.cards[ids["mono"]].geometry = QRect(20, 30, 280, 48);
        service.state.cards[ids["sound"]].geometry = QRect(20, 100, 280, 48);
        service.state.cards[ids["peak"]].geometry = QRect(20, 175, 380, 140);
        int column = 0;
        for (const auto *kind : {"gain_mono", "gain_stereo", "gain_5_1", "gain_7_1"})
            service.state.cards[ids[kind]].geometry = QRect(20 + column++ * 210, 355, 200, 150);
        service.notify();
        check(editor->isVisible() && IsWindowVisible(reinterpret_cast<HWND>(editor->winId())) &&
                  editor->devicePixelRatioF() == 1,
              "visible real editor at 100 percent only");
        auto *meter = dynamic_cast<UI::FxLevelMeterWidget *>(editor->findChild<QWidget *>("controlDeck.peakMeter"));
        peakProcessor.api->process(peakProcessor.instance, &meterAudio.block);
        service.notify();
        check(meter && meter->inputDb() == 12 && meter->property("wjn.singleChannel").toBool(),
              "public single-channel meter clamps display to +12 dB");
        auto *sound = service.find(ids["sound"])->sound;
        const auto fixtures = QString::fromUtf8(SWITCH_SOURCE_DIR) + "/build/ControlDeck-fixtures";
        for (const auto *format : {"wav", "aiff", "mp3", "ogg", "mod"})
        {
            check(sound->load(fixtures + "/tone." + format), "start asynchronous BASS file decode");
            check(until([&] {
                      service.pollSounds();
                      return !sound->loading();
                  }),
                  "decode completes outside RT callback");
            if (!sound->error().isEmpty())
                std::cerr << qPrintable(sound->error()) << std::endl;
            check(sound->ready() && sound->error().isEmpty(), "BASS stream or MOD loader decodes supported format");
            const auto processor = nodes["sound"]->virtualProcessor();
            AudioBlock audio(2, true);
            processor.api->process(processor.instance, &audio.block);
            check(std::all_of(audio.output[0].begin(), audio.output[0].end(), [](float value) { return value == 0; }),
                  "loading an effect never autoplays");
            service.activate(ids["sound"]);
            processor.api->process(processor.instance, &audio.block);
            check(std::any_of(audio.output[0].begin(), audio.output[0].end(), [](float value) { return value != 0; }),
                  "click command triggers real stereo decoded samples");
            const auto start = audio.output;
            service.activate(ids["sound"]);
            processor.api->process(processor.instance, &audio.block);
            check(start == audio.output, "retrigger restarts from beginning");
            sound->stop();
        }
        check(service.setShortcut(ids["sound"], "Ctrl+Alt+F9", &error), "register real Windows global shortcut");
        const bool reserved = RegisterHotKey(nullptr, 0x3991, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F10);
        check(!service.setShortcut(ids["sound"], "Ctrl+Alt+F10", &error) &&
                  service.find(ids["sound"])->shortcut == "Ctrl+Alt+F9",
              "OS shortcut conflict keeps previous assignment and active registration");
        if (reserved)
            UnregisterHotKey(nullptr, 0x3991);
        check(!service.setShortcut(ids["sound"], "Ctrl+Alt+F9, Ctrl+F8", &error) &&
                  service.find(ids["sound"])->shortcut == "Ctrl+Alt+F9",
              "invalid multistroke shortcut preserves previous registration");
        {
            Foreground lower;
            check(until([&] { return childWindow(DWORD(lower.process.processId())) != nullptr; }),
                  "independent lower window is visible");
            const auto window = childWindow(DWORD(lower.process.processId()));
            check(SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE),
                  "keep owned foreground helper exposed for native input");
            SetForegroundWindow(window);
            settle();
            if (GetForegroundWindow() != window)
            {
                RECT rectangle{};
                check(GetWindowRect(window, &rectangle), "locate owned test title bar");
                SetCursorPos(rectangle.left + 80, rectangle.top + 12);
                INPUT clicks[2]{};
                clicks[0].type = clicks[1].type = INPUT_MOUSE;
                clicks[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                clicks[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
                check(SendInput(2, clicks, sizeof(INPUT)) == 2,
                      "activate owned lower window with real title-bar click");
                settle();
            }
            check(GetForegroundWindow() == window, "other process is genuinely foreground during shortcut test");
            INPUT keys[6]{};
            const WORD virtualKeys[]{VK_CONTROL, VK_MENU, VK_F9, VK_F9, VK_MENU, VK_CONTROL};
            for (int index = 0; index < 6; ++index)
            {
                keys[index].type = INPUT_KEYBOARD;
                keys[index].ki.wVk = virtualKeys[index];
                keys[index].ki.dwFlags = index >= 3 ? KEYEVENTF_KEYUP : 0;
            }
            check(SendInput(6, keys, sizeof(INPUT)) == 6, "native shortcut keyboard input delivered");
            settle();
            const auto processor = nodes["sound"]->virtualProcessor();
            AudioBlock audio(2, true);
            processor.api->process(processor.instance, &audio.block);
            check(std::any_of(audio.output[0].begin(), audio.output[0].end(), [](float value) { return value != 0; }),
                  "global shortcut triggers actual sound while another application has focus");
        }
        QTimer feed;
        UI::bindRefreshTimer(feed, editor);
        QObject::connect(&feed, &QTimer::timeout, editor,
                         [&] { peakProcessor.api->process(peakProcessor.instance, &meterAudio.block); });
        feed.start();
        const auto captureRoot = QString::fromUtf8(SWITCH_SOURCE_DIR) + "/Docs/ControlDeck";
        QDir().mkpath(captureRoot);
        const QList<QPair<QString, QString>> titles{{"zh-CN", QString::fromUtf8("控制面板")},
                                                    {"en-US", "Control Deck"},
                                                    {"ja-JP", QString::fromUtf8("コントロールデッキ")},
                                                    {"ko-KR", QString::fromUtf8("컨트롤 덱")}};
        for (const auto &[locale, title] : titles)
        {
            check(language.setLanguage(locale, &error), "four language catalog hot switch");
            settle();
            check(editor->windowTitle() == title && service.quickMenu()->title() == title,
                  "panel and quick menu use translated Control Deck name");
            QTextLayout layout(title + service.label(*service.find(ids["peak"])) +
                               service.label(*service.find(ids["gain_mono"])));
            layout.setFont(app.font());
            layout.beginLayout();
            layout.createLine();
            layout.endLayout();
            bool valid = !layout.glyphRuns().isEmpty();
            for (const auto &run : layout.glyphRuns())
                for (auto glyph : run.glyphIndexes())
                    valid &= glyph != 0;
            check(valid, "CJK names resolve through system font fallback");
            const auto capture = editor->screen()->grabWindow(editor->winId()).toImage();
            for (auto *control : editor->findChildren<QWidget *>("controlDeck.gainKnob"))
            {
                const auto inside = control->mapTo(editor, QPoint(2, 2));
                const auto outside = control->mapTo(editor, QPoint(-3, 2));
                const auto ratio = capture.devicePixelRatio();
                check(capture.pixelColor(inside * ratio) == capture.pixelColor(outside * ratio),
                      "real knob corner inherits card background without a black rectangle");
            }
            check(capture.save(captureRoot + "/" + locale + ".png"), "capture visible controls in each language");
        }
        feed.stop();
        const auto direction = [&](const char *key) {
            bool triggered = false;
            QTimer action;
            action.setSingleShot(true);
            QObject::connect(&action, &QTimer::timeout, editor, [&] {
                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                if (!menu)
                    return;
                if (auto *item = menu->findChild<QAction *>(QString("signalSwitch.") + key))
                {
                    item->trigger();
                    triggered = true;
                }
                menu->close();
            });
            action.start(100);
            auto *card = editor->findChild<C::ActionButton *>("signalSwitchButton." + ids["peak"]);
            QMetaObject::invokeMethod(card, "customContextMenuRequested", Qt::DirectConnection,
                                      Q_ARG(QPoint, QPoint(5, 5)));
            check(triggered, "real context menu routes meter orientation command");
        };
        direction("vertical");
        check(service.find(ids["peak"])->peakVertical && meter->positionForDb(-72) > meter->positionForDb(12),
              "vertical scale runs from -72 at bottom to +12 at top");
        direction("horizontal");
        check(!service.find(ids["peak"])->peakVertical && meter->positionForDb(-72) < meter->positionForDb(12),
              "horizontal scale runs from -72 at left to +12 at right");
        direction("vertical");
        service.state.cards[ids["peak"]].geometry = QRect(430, 60, 160, 270);
        service.notify();
        settle();
        check(editor->screen()->grabWindow(editor->winId()).save(captureRoot + "/vertical.png"),
              "capture visible portrait mono peak meter");
        service.state.window.opacity = 67;
        service.notify();
        check(editor->windowOpacity() == 1 &&
                  qobject_cast<QGraphicsOpacityEffect *>(meter->parentWidget()->graphicsEffect())->opacity() == .67,
              "control opacity includes meter container without fading editor background");
        PatchbayCanvas graph(PatchbaySceneContext{"main/sub", "Subgraph", true});
        std::vector<PatchbayProjectModule> records;
        for (const auto &[kind, node] : nodes)
            records.push_back({node->modulePath(), 0, node->clientId(), node->settingsJson(), node->contextId()});
        const auto project = temporary.filePath("control-deck.wjnproj");
        const auto state = service.state.toBase64();
        check(graph.saveProject(project, records), "production project writer encodes control presets");
        std::vector<PatchbayProjectModule> loaded;
        check(graph.loadProject(project, loaded), "production project reader decodes control presets");
        service.beginRestore();
        nodes.clear();
        for (const auto &record : loaded)
        {
            auto node =
                LoadedModuleNode::create(record.modulePath, 0, record.settingsJson, WJN_NODE_MODE_VIRT, 48000, 64);
            check(bool(node), "production host reconstructs control and saved settings");
            node->setClientId(record.clientId);
            node->setContextId(record.contextId);
            nodes[record.modulePath] = std::move(node);
        }
        check(service.restore(graph.signalSwitchProjectState(), &error) && service.state.toBase64() == state,
              "layout and control opacity restore from project Base64");
        check(service.find(ids["gain_7_1"])->gainMinimum == -30 && service.find(ids["gain_7_1"])->gainMaximum == 18 &&
                  std::abs(service.find(ids["gain_7_1"])->gainDb - 6.020599913) < 1e-8,
              "gain value and custom dB range persist in node settings");
        check(service.find(ids["sound"])->shortcut == "Ctrl+Alt+F9" &&
                  service.find(ids["sound"])->sound->path().endsWith("tone.mod"),
              "sound path and global shortcut persist in node settings");
        check(service.find(ids["peak"])->peakVertical, "meter orientation persists in node Base64 settings");
        check(shows.shows + 1 <= 100, "bounded batch including independent helper window");
        std::cout << "WINDOW_SHOWS " << shows.shows + 1 << std::endl;
        nodes.clear();
        service.shutdownUI();
        UI::ThemeManager::installThemeCore(nullptr);
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << exception.what() << std::endl;
        SS::Service::instance().shutdownUI();
        UI::ThemeManager::installThemeCore(nullptr);
        return 1;
    }
}
