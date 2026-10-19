// SPDX-License-Identifier: AGPL-3.0-only
#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QGlyphRun>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibrary>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTextLayout>
#include <QTimer>
#include <WinJACKNexus/Common/ClassicMixer.hpp>
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/ClassicMixerWindow.hpp>
#include <WinJACKNexus/UI/ConsoleControls.hpp>
#include <WinJACKNexus/UI/I18nManager.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <WinJACKNexus/UI/ThemeNamespaceRegistry.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace CM = WinJACKNexus::Common::ClassicMixer;
namespace UI = WinJACKNexus::UI;
namespace C = UI::Controls;
QString mixerModule(int layout)
{
    switch (layout)
    {
    case 8:
        return QString::fromUtf8(CLASSIC_MIXER_DLL_8);
    case 16:
        return QString::fromUtf8(CLASSIC_MIXER_DLL_16);
    default:
        return QString::fromUtf8(CLASSIC_MIXER_DLL_24);
    }
}
int checks = 0;
void check(bool value, const char *message)
{
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
class Shows : public QObject
{
  public:
    int count = 0;
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::Show)
        {
            if (auto *widget = qobject_cast<QWidget *>(object); widget && widget->isWindow())
            {
                ++count;
                if (count > 100)
                    throw std::runtime_error("window show budget exceeded");
            }
        }
        return false;
    }
};
template <typename T> T *control(QWidget *root, const QString &id)
{
    auto *widget = root->findChild<QWidget *>(id);
    auto *result = dynamic_cast<T *>(widget);
    check(result != nullptr, "production shared control found");
    return result;
}
void glyphs(const QString &text, const QFont &font)
{
    QTextLayout layout(text, font);
    layout.beginLayout();
    auto line = layout.createLine();
    line.setLineWidth(2000);
    layout.endLayout();
    for (const auto &run : layout.glyphRuns())
        for (const auto glyph : run.glyphIndexes())
            check(glyph != 0, "system fallback has no tofu glyphs");
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    try
    {
        check(app.platformName() == "windows", "real Windows QPA required");
        check(qEnvironmentVariable("QT_SCALE_FACTOR") == "1", "100 percent scale configured");
        app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
        Shows shows;
        app.installEventFilter(&shows);
        QTemporaryDir temporary;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
        UI::I18nManager languages({temporary.filePath("language.ini"), {"en-US"}, false});
        QString error;
        check(languages.install(&error), "install language manager");
        UI::ThemeCore core;
        check(core.isValid(), "theme core valid");
        UI::ThemeManager::installThemeCore(&core);
        UI::ThemeManager theme;
        theme.bindThemeCore(&core);
        theme.applyToApp(&app);
        for (int layout : {8, 16, 24})
        {
            const QString path = mixerModule(layout);
            QLibrary library(path);
            check(library.load(), "actual GUI DLL loads");
            UI::ThemeNamespaceRegistry::Lease themeLease;
            if (!UI::ThemeNamespaceRegistry::instance().registerNodeModule(
                    [&library](const char *name) { return library.resolve(name); }, path,
                    &themeLease, &error))
                std::cerr << error.toStdString() << '\n';
            check(!themeLease.isEmpty(), "actual node theme descriptor registers");
            auto languageLease =
                languages.registerLibrary(path, UI::I18nManager::Extension::node, 0, &error);
            if (!languageLease)
                std::cerr << error.toStdString() << '\n';
            check(bool(languageLease), "actual node catalogs register");
            const auto create = reinterpret_cast<decltype(&wjn_create_node_v4)>(
                library.resolve("wjn_create_node_v4"));
            const auto destroy =
                reinterpret_cast<decltype(&wjn_destroy_node)>(library.resolve("wjn_destroy_node"));
            const auto get = reinterpret_cast<decltype(&wjn_get_node_settings_json)>(
                library.resolve("wjn_get_node_settings_json"));
            const auto set = reinterpret_cast<decltype(&wjn_set_node_settings_json)>(
                library.resolve("wjn_set_node_settings_json"));
            const auto api = reinterpret_cast<decltype(&wjn_get_node_virtual_api_v1)>(
                library.resolve("wjn_get_node_virtual_api_v1"))();
            const auto sidebar = reinterpret_cast<decltype(&wjn_create_settings_widget)>(
                library.resolve("wjn_create_settings_widget"));
            const auto destroySidebar = reinterpret_cast<decltype(&wjn_destroy_settings_widget)>(
                library.resolve("wjn_destroy_settings_widget"));
            const auto evidence =
                QDir::current().absoluteFilePath("build/classic-mixer-split-evidence");
            QDir().mkpath(evidence);
            const QString prefix = "--wjn-theme-module-winjacknexus-classic-mixer-";
            check(core.snapshot()->value(prefix + "fader-input-thumb") == "#a6adb4",
                  "gray input default");
            check(core.snapshot()->value(prefix + "fader-master-thumb") == "#e35252",
                  "red master default");
            check(core.snapshot()->value(prefix + "fader-monitor-thumb") == "#e4c44c",
                  "yellow monitor default");
            auto *node = create(WJN_NODE_MODE_VIRT);
            auto state = CM::State::create(layout);
            state.layoutChosen = true;
            state.inputs[0].pan = -1;
            state.inputs[2].pan = 1;
            auto json = state.toJson();
            json.insert("clientName", "VisibleMixer");
            check(set(node, QJsonDocument(json).toJson().constData()),
                  "restore layout before window");
            WjnNodePrepareV1 preparation{sizeof(preparation), 256, 48000, nullptr};
            check(api->prepare(node, &preparation), "offline GUI instance prepare");
            std::array<float, 256> source{};
            std::array<std::array<float, 256>, 8> rendered{};
            source.fill(0.1f);
            float *sourcePointer = source.data();
            WjnNodeAudioBufferV1 audioInput{1, 1, &sourcePointer};
            std::array<float *, 8> renderedPointers{};
            std::array<WjnNodeAudioBufferV1, 8> audioOutputs{};
            for (uint32_t channel = 0; channel < 8; ++channel)
            {
                renderedPointers[channel] = rendered[channel].data();
                audioOutputs[channel] = {101 + channel, 1, &renderedPointers[channel]};
            }
            WjnNodeProcessV1 block{};
            block.structSize = sizeof(block);
            block.frames = 256;
            block.audioInputCount = 1;
            block.audioInputs = &audioInput;
            block.audioOutputCount = 8;
            block.audioOutputs = audioOutputs.data();
            api->process(node, &block);
            QTimer signal;
            QObject::connect(&signal, &QTimer::timeout, &app, [&] { api->process(node, &block); });
            signal.start(5); // Offline audio clock, independent of the view repaint cadence.

            auto *window = QWidget::find(reinterpret_cast<WId>(node->createWindow(nullptr)));
            check(window && window->isWindow(), "standalone real window");
            const auto available = window->screen()->availableGeometry();
            window->resize(std::min(1360, available.width() - 40),
                           std::min(940, available.height() - 60));
            window->move(available.topLeft() + QPoint(10, 10));
            check(QTest::qWaitForWindowExposed(window), "window genuinely exposed");
            QTest::qWait(150);
            check(std::abs(window->devicePixelRatioF() - 1) < 0.01,
                  "actual window 100 percent DPR");
            const int count = static_cast<int>(state.inputs.size());
            int faders = 0, separators = 0;
            for (auto *child : window->findChildren<QWidget *>())
            {
                if (dynamic_cast<C::ConsoleFader *>(child))
                    ++faders;
                if (dynamic_cast<C::ConsoleSeparator *>(child))
                    ++separators;
            }
            check(faders == count + 2, "exact fader count");
            check(separators == count + 1, "divider between every strip");
            auto *inputFader = control<C::ConsoleFader>(window, "classicMixer.strip.0.fader");
            auto *stereoFader = control<C::ConsoleFader>(
                window, QStringLiteral("classicMixer.strip.%1.fader").arg(count - 1));
            auto *master = control<C::ConsoleFader>(
                window, QStringLiteral("classicMixer.strip.%1.fader").arg(count));
            auto *monitor = control<C::ConsoleFader>(
                window, QStringLiteral("classicMixer.strip.%1.fader").arg(count + 1));
            check(inputFader->displayedLevelDb() > -21 && inputFader->displayedLevelDb() < -19,
                  "real DLL measurement reaches visible mono meter");
            check(inputFader->meterChannels() == 1 && stereoFader->meterChannels() == 2 &&
                      master->meterChannels() == 2 && monitor->meterChannels() == 2,
                  "mono/stereo meter counts");
            check(inputFader->mapTo(window, QPoint{}).y() == master->mapTo(window, QPoint{}).y() &&
                      master->mapTo(window, QPoint{}).y() == monitor->mapTo(window, QPoint{}).y(),
                  "all fader rows aligned");
            check(inputFader->height() == master->height() && master->height() == monitor->height(),
                  "all fader travel lengths aligned");
            auto *scroll = control<C::ScrollField>(window, "classicMixer.inputScroll");
            const auto masterPosition = master->mapTo(window, QPoint{});
            scroll->horizontalScrollBar()->setValue(scroll->horizontalScrollBar()->maximum());
            QTest::qWait(50);
            check(master->mapTo(window, QPoint{}) == masterPosition &&
                      scroll->horizontalScrollBar()->maximum() > 0,
                  "input scroll keeps master fixed");
            scroll->horizontalScrollBar()->setValue(0);
            auto *gain = control<C::ConsoleKnob>(window, "classicMixer.strip.0.gain");
            gain->setFocus();
            QTest::keyClick(gain, Qt::Key_Up);
            check(QJsonDocument::fromJson(get(node))
                          .object()
                          .value("inputs")
                          .toArray()[0]
                          .toObject()
                          .value("gain")
                          .toDouble() > 0,
                  "knob keyboard commits production parameters");
            auto *pan = control<C::ConsoleKnob>(window, "classicMixer.strip.0.pan");
            pan->setFocus();
            QTest::keyClick(pan, Qt::Key_Return);
            auto *panEditor = pan->findChild<QLineEdit *>("numericValueEditor");
            check(panEditor && panEditor->isVisible() && panEditor->text().toDouble() == -100,
                  "shared pan editor displays signed percent");
            panEditor->setText("-50");
            QTest::keyClick(panEditor, Qt::Key_Return);
            check(QJsonDocument::fromJson(get(node))
                          .object()
                          .value("inputs")
                          .toArray()[0]
                          .toObject()
                          .value("pan")
                          .toDouble() == -0.5,
                  "shared percent edit commits normalized pan to real DLL");
            QTest::qWait(20);
            QTest::keyClick(pan, Qt::Key_Return);
            panEditor = pan->findChild<QLineEdit *>("numericValueEditor");
            check(panEditor && panEditor->isVisible(), "shared pan editor reopens");
            panEditor->setText("-100");
            QTest::keyClick(panEditor, Qt::Key_Return);
            QTest::qWait(20);
            auto *afl = control<C::ActionButton>(window, "classicMixer.strip.0.afl");
            auto *pfl = control<C::ActionButton>(window, "classicMixer.strip.0.pfl");
            afl->click();
            pfl->click();
            check(!afl->isChecked() && pfl->isChecked(), "visible mutual exclusion");
            auto *point = control<C::ChoiceField>(window, "classicMixer.meterPoint");
            check(point->currentData().toBool(), "default post fader");
            point->setCurrentIndex(0);
            check(!QJsonDocument::fromJson(get(node)).object().value("postMeter").toBool(),
                  "meter selector persists");
            auto *name = control<C::TextField>(window, "classicMixer.strip.0.name");
            for (const auto &language : QStringList{"en-US", "zh-CN", "ja-JP", "ko-KR"})
            {
                name->setFocus();
                name->setText(QString::fromUtf8("未提交 보컬 ボーカル"));
                const auto before = QJsonDocument::fromJson(get(node)).object();
                check(languages.setLanguage(language, &error), "hot language switch");
                check(name->text() == QString::fromUtf8("未提交 보컬 ボーカル"),
                      "language preserves name draft");
                check(QJsonDocument::fromJson(get(node)).object() == before,
                      "language does not submit parameters");
                glyphs(window->windowTitle() + name->text(), window->font());
                const QString expectedTitle =
                    language == "zh-CN"   ? QString::fromUtf8("经典调音台")
                    : language == "ja-JP" ? QString::fromUtf8("クラシックミキサー")
                    : language == "ko-KR" ? QString::fromUtf8("클래식 믹서")
                                          : "Classic Mixer";
                check(window->windowTitle().startsWith(expectedTitle),
                      "independent translated window title");
                QTest::qWait(100);
                check(window->screen()
                          ->grabWindow(window->winId())
                          .save(evidence + QStringLiteral("/%1-%2.png").arg(layout).arg(language)),
                      "capture real visible window");
            }
            name->setText("Restored");
            QTest::keyClick(name, Qt::Key_Return);
            auto *presetName = control<C::TextField>(window, "classicMixer.presetName");
            presetName->setText(QString::fromUtf8("现场 프리셋"));
            control<C::ActionButton>(window, "classicMixer.newPreset")->click();
            check(QJsonDocument::fromJson(get(node)).object().value("presets").toArray().size() ==
                      1,
                  "production embedded preset save");
            const auto savedGain = QJsonDocument::fromJson(get(node))
                                       .object()
                                       .value("inputs")
                                       .toArray()[0]
                                       .toObject()
                                       .value("gain")
                                       .toDouble();
            gain->setFocus();
            QTest::keyClick(gain, Qt::Key_Up);
            control<C::ActionButton>(window, "classicMixer.applyPreset")->click();
            check(QJsonDocument::fromJson(get(node))
                          .object()
                          .value("inputs")
                          .toArray()[0]
                          .toObject()
                          .value("gain")
                          .toDouble() == savedGain,
                  "production preset restores edited gain");
            inputFader->setFocus();
            QTest::keyClick(inputFader, Qt::Key_End);
            check(QJsonDocument::fromJson(get(node))
                          .object()
                          .value("inputs")
                          .toArray()[0]
                          .toObject()
                          .value("fader")
                          .toDouble() == CM::silenceDb,
                  "shared fader true-zero keyboard endpoint");
            QTest::keyClick(inputFader, Qt::Key_Home);
            control<C::ChoiceField>(window, "classicMixer.strip.0.pre0")->setCurrentIndex(1);
            check(QJsonDocument::fromJson(get(node))
                      .object()
                      .value("inputs")
                      .toArray()[0]
                      .toObject()
                      .value("pre")
                      .toArray()[0]
                      .toBool(),
                  "production Pre switch independent");
            control<C::ActionButton>(window, "classicMixer.savePreset")->click();
            check(QJsonDocument::fromJson(get(node)).object().value("presets").toArray().size() ==
                      1,
                  "preset overwrite retains single record");
            presetName->setText("Temporary");
            control<C::ActionButton>(window, "classicMixer.newPreset")->click();
            control<C::ActionButton>(window, "classicMixer.deletePreset")->click();
            check(QJsonDocument::fromJson(get(node)).object().value("presets").toArray().size() ==
                      1,
                  "delete only selected embedded preset");
            const auto snapshot = QByteArray(get(node));
            for (int fps : {30, 60, 120})
            {
                UI::ApplicationSettings::instance().setFrameRate(fps);
                QTest::qWait(40);
                check(UI::ApplicationSettings::instance().frameRate() == fps, "shared FPS setting");
            }
            check(core.setOverride(prefix + "fader-master-thumb", "#24a7db", {}, &error),
                  "live theme override");
            QTest::qWait(100);
            check(C::themeTokenColor("module-winjacknexus-classic-mixer", "fader-master-thumb",
                                     {}) == QColor("#24a7db"),
                  "public painter resolves live role");
            check(QByteArray(get(node)) == snapshot, "theme/FPS do not mutate audio settings");
            core.removeOverride(prefix + "fader-master-thumb");
            QWidget sidebarRoot;
            auto *panel = sidebar(node, &sidebarRoot);
            check(panel && panel->findChildren<QLineEdit *>().size() == 1 &&
                      panel->findChildren<QComboBox *>().isEmpty(),
                  "sidebar edits only node name");
            destroySidebar(node, panel);
            node->destroyWindow(nullptr);
            check(!window->isVisible(), "editor closes");
            auto *reopened = QWidget::find(reinterpret_cast<WId>(node->createWindow(nullptr)));
            check(reopened == window && QByteArray(get(node)) == snapshot,
                  "reopen retains instance and parameters");
            node->destroyWindow(nullptr);
            QTest::qWait(40);
            check(std::abs(rendered[0][10]) > 0.01f, "closing editor leaves offline DSP running");
            signal.stop();
            api->stop(node);
            destroy(node);
            check(shows.count <= 100, "bounded real window batch");
            languageLease.reset();
            themeLease.reset();
            check(library.unload(), "GUI module unload after windows and leases");
        }
        UI::ThemeManager::installThemeCore(nullptr);
        std::cout << "PASS " << checks << " Windows 100% GUI checks; window shows=" << shows.count
                  << '\n';
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
