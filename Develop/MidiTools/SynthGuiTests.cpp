// Real Windows widgets plus real engines in memory; never opens a JACK client.
// SPDX-License-Identifier: AGPL-3.0-only
#include <QApplication>
#include <QDir>
#include <QFontInfo>
#include <QGlyphRun>
#include <QLabel>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>
#include <QTextLayout>
#include <QTextDocument>
#include <QToolTip>
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/UI/HostI18n.hpp>
#include <WinJACKNexus/UI/MidiSynthWindow.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <WinJACKNexus/UI/ThemeNamespaceRegistry.hpp>
#include <iostream>
#include <stdexcept>

namespace Core = WinJACKNexus::Common::MidiTools;
namespace UI = WinJACKNexus::UI;
void check(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
UI::MidiSynthGain *gainControl(QWidget &window, const char *name)
{
    return dynamic_cast<UI::MidiSynthGain *>(window.findChild<QWidget *>(name));
}
void editGain(UI::MidiSynthGain &gain, const QString &value)
{
    QTest::mouseClick(&gain, Qt::LeftButton, {}, QPoint(gain.width() - 20, gain.height() / 2));
    auto *editor = gain.findChild<QLineEdit *>("numericValueEditor");
    check(editor && editor->isVisible(), "public numeric readout editor");
    QTest::keyClicks(editor, value);
    QTest::keyClick(editor, Qt::Key_Return);
}
void capture(QWidget &window, const QString &path)
{
    window.raise();
    window.activateWindow();
    QTest::qWait(450);
    check(window.isVisible() && IsWindowVisible(reinterpret_cast<HWND>(window.winId())) &&
              window.devicePixelRatioF() == 1.0,
          "genuine visible native window at100%");
    for (const auto *label : window.findChildren<QLabel *>())
    {
        QTextLayout layout(label->text(), label->font());
        layout.beginLayout();
        layout.createLine();
        layout.endLayout();
        for (const auto &run : layout.glyphRuns())
        {
            for (auto glyph : run.glyphIndexes())
            {
                check(glyph != 0, "CJK system font fallback glyph");
            }
        }
    }
    const auto screenshot = window.screen()->grabWindow(window.winId());
    const auto pixels = screenshot.toImage();
    for (auto *widget : window.findChildren<QWidget *>())
    {
        auto *card = dynamic_cast<UI::Controls::GroupPanel *>(widget);
        if (card && card->title().isEmpty())
        {
            const auto aboveBorder = card->mapTo(&window, QPoint(12, 2));
            check(pixels.pixelColor(aboveBorder) == window.palette().color(QPalette::Window),
                  "empty card title must not paint background above its border");
        }
    }
    check(screenshot.save(path), "real native surface capture");
}
void captureInformationTooltip(QWidget &window, const QString &path, const QString &expected)
{
    auto *help = window.findChild<QPushButton *>("synth-resource-info");
    check(help && help->isVisible() && help->text() == "i", "visible shared information button");
    QTextDocument description;
    description.setHtml(help->toolTip());
    check(description.toPlainText().contains(expected), "complete localized information tooltip");
    window.raise();
    window.activateWindow();
    QTest::mouseMove(&window, QPoint(2, 2));
    QTest::qWait(150);
    QTest::mouseMove(help, help->rect().center());
    QPointer<QWidget> tooltip;
    check(QTest::qWaitFor(
              [&] {
                  for (auto *widget : QApplication::topLevelWidgets())
                  {
                      if (widget->windowType() == Qt::ToolTip && widget->isVisible() &&
                          qobject_cast<QLabel *>(widget) &&
                          IsWindowVisible(reinterpret_cast<HWND>(widget->winId())))
                      {
                          tooltip = widget;
                          return true;
                      }
                  }
                  return false;
              },
              3000),
          "mouse hover opens the actual native tooltip");
    QTest::qWait(150);
    check(tooltip && IsWindowVisible(reinterpret_cast<HWND>(tooltip->winId())) &&
              tooltip->devicePixelRatioF() == 1.0,
          "visible Windows tooltip at 100 percent");
    check(tooltip->screen()->grabWindow(tooltip->winId()).save(path),
          "capture actual visible tooltip surface");
    QToolTip::hideText();
    QTest::mouseMove(&window, QPoint(2, 2));
    QTest::qWait(350);
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    try
    {
        check(argc == 4 && app.platformName() == "windows",
              "Windows runtime/repo/screens required");
        UI::ThemeCore themeCore;
        check(themeCore.isValid(), qPrintable(themeCore.initializationError()));
        UI::ThemeManager::installThemeCore(&themeCore);
        UI::ThemeManager theme;
        theme.watchApplication(&app);
        const auto repository = QString::fromUtf8(argv[2]);
        const auto output = QString::fromUtf8(argv[3]);
        QDir().mkpath(output);
        const auto dllPath =
            QString::fromUtf8(argv[1]) + "/midi_tools/sf2_loader/mod_sf2_loader.dll";
        const auto library = LoadLibraryW(reinterpret_cast<const wchar_t *>(dllPath.utf16()));
        check(library, "load actual production DLL");
        UI::ThemeNamespaceRegistry::Lease themeLease;
        QString themeError;
        check(UI::ThemeNamespaceRegistry::instance().registerNodeModule(
                  [library](const char *name) {
                      return reinterpret_cast<QFunctionPointer>(GetProcAddress(library, name));
                  },
                  dllPath, &themeLease, &themeError),
              qPrintable(themeError));
        check(themeCore.refreshRegisteredNamespaces(&themeError), qPrintable(themeError));
        auto symbol = [library](const char *name) {
            const auto result = GetProcAddress(library, name);
            check(result, "actual DLL exported function");
            return result;
        };
        const auto create = reinterpret_cast<decltype(&wjn_create_node)>(symbol("wjn_create_node"));
        const auto destroy =
            reinterpret_cast<decltype(&wjn_destroy_node)>(symbol("wjn_destroy_node"));
        const auto get = reinterpret_cast<decltype(&wjn_get_node_settings_json)>(
            symbol("wjn_get_node_settings_json"));
        const auto set = reinterpret_cast<decltype(&wjn_set_node_settings_json)>(
            symbol("wjn_set_node_settings_json"));
        check(GetProcAddress(library, "wjn_show_node_gui"), "optional host standalone GUI export");
        QTemporaryDir temporary;
        UI::HostI18n host({temporary.filePath("i18n.ini"), {"en-US"}, false});
        QString error;
        check(host.initialize(repository, &error), qPrintable(error));
        auto lease =
            host.manager().registerLibrary(dllPath, UI::I18nManager::Extension::node, 0, &error);
        check(bool(lease), qPrintable(error));
        Core::SynthSettings state;
        state.resources = {
            {"fixture-sf2", (repository + "/Develop/MidiTools/fixtures/tone.sf2").toStdString(),
             "sf2"},
            {"fixture-sfz", (repository + "/Develop/MidiTools/fixtures/tone.sfz").toStdString(),
             "sfz", 0, 0, 2}};

        auto *node = create();
        check(node && set(node, Core::settingsJson(state).c_str()), "real DLL offline restore");
        const auto handle = node->createWindow(nullptr);
        auto *production = QWidget::find(reinterpret_cast<WId>(handle));
        check(production && production->isWindow(), "standalone production window");
        capture(*production, output + "/synth-production-en-US.png");
        captureInformationTooltip(*production, output + "/synth-production-info-en-US.png",
                                  host.manager().text("midi.sf2_loader", "browseHelp", "missing"));
        auto *productionGain = gainControl(*production, "synth-gain-0");
        check(productionGain, "public production gain field");
        editGain(*productionGain, "-3");
        Core::SynthSettings restored;
        check(Core::parseSettings(get(node), restored) && restored.channels[0].gainDb == -3,
              "production window commits node persistence");
        node->destroyWindow(handle);
        check(!production->isVisible(), "close hides editor");
        check(node->createWindow(nullptr) &&
                  QWidget::find(reinterpret_cast<WId>(node->createWindow(nullptr))) == production,
              "reopen retains same window and engine state");
        check(productionGain->value() == -3, "reopen retains edited gain");
        node->destroyWindow(nullptr);
        destroy(node);

        // The identical UI composition receives real offline engine audio, with no
        // production test mode or JACK driver. The test owns its memory audio pump.
        Core::SynthCore core;
        Core::SynthStatus status;
        std::array<float, 256> left{}, right{};
        check(core.submit(state), "offline engine bank request");
        auto render = [&] { core.render(256, {}, left.data(), right.data()); };
        auto wait = [&] {
            for (unsigned attempt = 0; attempt < 6000; ++attempt)
            {
                render();
                if (!core.loading())
                {
                    render();
                    return;
                }
                QTest::qWait(2);
            }
            throw std::runtime_error("offline loader deadline");
        };
        wait();
        UI::MidiSynthBindings bindings;
        unsigned edits = 0;
        bindings.edit = [&](const auto &candidate, bool preserve) {
            if (!core.submit(candidate, preserve))
            {
                return false;
            }
            state = candidate;
            ++edits;
            return true;
        };
        bindings.status = [&] {
            core.status(status);
            return status;
        };
        bindings.resources = [&] { return core.resources(); };
        bindings.loading = [&] { return core.loading(); };
        bindings.text = [&](const char *key, const char *fallback) {
            return host.manager().text("midi.sf2_loader", key, fallback);
        };
        UI::MidiSynthWindow window(state, std::move(bindings));
        host.manager().bind(&window, [&] { window.language(); });
        QTimer audio;
        UI::bindRefreshTimer(audio, &audio);
        QObject::connect(&audio, &QTimer::timeout, &audio, [&] {
            for (unsigned block = 0; block < 4; ++block)
            {
                render();
            }
        });
        audio.start();
        window.show();
        auto message = [&](std::initializer_list<uint8_t> bytes) {
            const std::vector<uint8_t> data(bytes);
            const Core::EventView event{0, data};
            core.render(256, {&event, 1}, left.data(), right.data());
        };
        message({0x90, 60, 100});
        QTest::qWait(200);
        auto *gain = gainControl(window, "synth-gain-0");
        auto *mute = window.findChild<QPushButton *>("synth-mute-0");
        auto *solo = window.findChild<QPushButton *>("synth-solo-0");
        auto *masterMute = window.findChild<QPushButton *>("synth-master-mute");
        auto *formats = window.findChild<QTabWidget *>("synth-formats");
        auto *sfz = window.findChild<QListWidget *>("synth-sfz-list");
        auto *prefer = window.findChild<QPushButton *>("synth-prefer");
        check(gain && mute && solo && masterMute && formats && sfz && prefer,
              "shared UI composition");
        const auto children = window.findChildren<QWidget *>();
        check(std::count_if(
                  children.begin(), children.end(),
                  [](QWidget *widget) { return dynamic_cast<UI::MidiSynthMeter *>(widget); }) == 18,
              "16 real channel meters plus master L/R");
        formats->setCurrentIndex(1);
        sfz->setCurrentRow(0);
        check(state.preferred.empty(), "file browsing never commits performance preference");
        auto *program = window.findChild<QSpinBox *>("synth-mapping-2");
        check(program && program->value() == 2, "persisted0-based SFZ mapping field");
        program->setValue(3);
        wait();
        check(state.resources[1].program == 3, "mapping edit saved by stable resource ID");
        QTest::mouseClick(prefer, Qt::LeftButton);
        wait();
        check(state.preferred == "fixture-sfz", "explicit preferred action");
        editGain(*gain, "-6");
        wait();
        QTest::mouseClick(solo, Qt::LeftButton);
        wait();
        const auto beforeLanguage = Core::settingsJson(state);
        const unsigned beforeEdits = edits;
        for (const char *language : {"en-US", "zh-CN", "ja-JP", "ko-KR"})
        {
            check(host.manager().setLanguage(language, &error), qPrintable(error));
            capture(window, output + "/synth-signal-" + language + ".png");
            captureInformationTooltip(window, output + "/synth-info-" + language + ".png",
                                      host.manager().text("midi.sf2_loader", "mapping", "missing"));
            auto *help = window.findChild<QPushButton *>("synth-resource-info");
            check(help->accessibleDescription().contains(
                      QString::fromStdString(state.resources[1].path)),
                  "resource details retain the complete selected path");
            check(Core::settingsJson(state) == beforeLanguage && edits == beforeEdits &&
                      formats->currentIndex() == 1 && sfz->currentRow() == 0,
                  "language preserves gain/solo/mapping/selection without edits");
        }
        check(dynamic_cast<UI::MidiSynthMeter *>(window.findChild<QWidget *>("synth-level-0"))
                      ->levelDb() > -72,
              "real signal feeds channel meter");
        QTest::mouseClick(mute, Qt::LeftButton);
        wait();
        capture(window, output + "/synth-muted-ko-KR.png");
        check(status.channels[0].peak == 0 && state.channels[0].mute && state.channels[0].solo,
              "actual mute wins over solo");
        message({0x80, 60, 0});
        QTest::mouseClick(mute, Qt::LeftButton);
        QTest::mouseClick(masterMute, Qt::LeftButton);
        wait();
        message({0xc0, 3});
        message({0x90, 60, 100});
        capture(window, output + "/synth-master-muted-ko-KR.png");
        check(status.leftPeak == 0 && status.rightPeak == 0, "actual master mute");
        QTest::mouseClick(masterMute, Qt::LeftButton);
        wait();
        const unsigned savedFrameRate = UI::ApplicationSettings::instance().frameRate();
        auto *refresh = window.findChild<QTimer *>("synth-refresh");
        for (unsigned frameRate : {30u, 60u, 120u})
        {
            UI::ApplicationSettings::instance().setFrameRate(int(frameRate));
            check(refresh && refresh->interval() == int(1000 / frameRate),
                  "public application FPS binding");
        }
        UI::ApplicationSettings::instance().setFrameRate(int(savedFrameRate));
        window.close();
        QTest::qWait(100);
        core.status(status);
        // Drain the bounded pre-close snapshots, then observe a new audio-pump
        // publication while the editor and its refresh timer remain stopped.
        QTest::qWait(50);
        const bool freshClosedStatus = core.status(status);
        check(!window.isVisible(), "closing editor hides its native window");
        check(!refresh->isActive(), "closing editor stops its refresh timer");
        check(freshClosedStatus && status.leftPeak > 0,
              "real synthesis continues with editor closed");
        window.show();
        capture(window, output + "/synth-reopened-ko-KR.png");
        check(gain->value() == -6 && solo->isChecked(), "same composition state after reopening");
        check(!window.importFile(repository + "/Develop/MidiTools/fixtures/tone.sf2"),
              "duplicate import rejected atomically by the real UI import path");
        check(window.importFile(repository + "/Develop/MidiTools/fixtures/missing.sf2"),
              "missing import has explicit failed metadata and keeps usable bank");
        wait();
        capture(window, output + "/synth-missing-ko-KR.png");
        auto *remove = window.findChild<QPushButton *>("synth-remove");
        QTest::mouseClick(remove, Qt::LeftButton);
        wait();
        check(state.resources.size() == 2 &&
                  QFileInfo::exists(repository + "/Develop/MidiTools/fixtures/tone.sf2"),
              "remove changes reference only");
        for (const char *name : {"template", "template-simple-dev"})
        {
            UI::ThemePackage package;
            check(UI::ThemePackage::loadFromDirectory(repository + "/themes/" + name, &package,
                                                      &error),
                  qPrintable(error));
            check(themeCore.setFullTheme(package, &error), qPrintable(error));
            capture(window, output + "/synth-" + name + "-ko-KR.png");
        }
        const QString knobToken =
            "--wjn-theme-module-midi-tools-sf2-loader-control-knob-indicator-normal";
        const auto inheritedKnob = themeCore.snapshot()->value(knobToken);
        check(inheritedKnob ==
                  themeCore.snapshot()->value("--wjn-theme-global-control-knob-indicator-normal"),
              "node defaults inherit the common knob theme");
        check(themeCore.setOverride(knobToken, "#325ac8", {}, &error), qPrintable(error));
        capture(window, output + "/synth-node-token-ko-KR-modified.png");
        check(themeCore.snapshot()->value(knobToken) != inheritedKnob,
              "registered node knob token can be customized independently");
        window.close();
        audio.stop();
        lease.reset();
        themeLease.reset();
        check(FreeLibrary(library), "production DLL released after editor destruction and lease");
        std::cout
            << "PASS actual DLL standalone/persistence/reopen plus real-engine "
               "signal/mute/solo/master, "
               "SFZ mapping/preference/browse/remove/import, four languages and30/60/120 FPS; "
               "platform=windows, DPR=1, systemFont="
            << QFontInfo(app.font()).family().toStdString()
            << ", visible windows and hover tooltips bounded below 48/100; no JACK init\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
