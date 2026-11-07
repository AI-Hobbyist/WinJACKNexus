// SPDX-License-Identifier: AGPL-3.0-only
#include "../../Develop/Node/WindowShowCounter.hpp"
#include "NodePaths.hpp"
#include "Service.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLabel>
#include <QLibrary>
#include <QMenu>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <WinJACKNexus/Common/LoudnessEngine.hpp>
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/UI/AudioAnalysisWidget.hpp>
#include <WinJACKNexus/UI/MetricReadout.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

namespace SS = WinJACKNexus::ControlDeck;
namespace Common = WinJACKNexus::Common;
namespace UI = WinJACKNexus::UI;
int fail(int line)
{
    std::printf("FAIL AnalysisTests.cpp:%d\n", line);
    return 1;
}
bool wait(const std::function<bool()> &condition)
{
    for (unsigned retry = 0; retry < 150; ++retry)
    {
        if (condition())
            return true;
        QTest::qWait(10);
    }
    return condition();
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (app.platformName() != "windows")
        return 2;
    app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    WindowShowCounter shows;
    app.installEventFilter(&shows);
    QTemporaryDir temporary;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "WinJACKNexus", "Meterbridge");
    settings.setValue("silenceThresholdDbfs", -65.0);
    settings.setValue("silenceDurationSeconds", 0.1);
    settings.setValue("logRootDirectory", temporary.filePath("global-csv"));
    settings.sync();
    UI::ThemeCore theme;
    if (!theme.isValid())
        return 2;
    UI::ThemeManager::installThemeCore(&theme);
    UI::ThemeManager manager;
    manager.applyToApp(&app);
    auto &service = SS::Service::instance();
    {
        std::array<float, 1024> tone{}, inverted{};
        for (unsigned sample = 0; sample < tone.size(); ++sample)
        {
            tone[sample] = float(std::sin(2 * std::numbers::pi * 8 * sample / 1024));
            inverted[sample] = -tone[sample];
        }
        std::array<std::array<double, 512>, 8> powers{};
        powers[0] = Common::analysisSpectrum(tone);
        powers[1] = Common::analysisSpectrum(inverted);
        if (std::abs(Common::analysisMeanPower(powers, 2, 8) - powers[0][8]) > 1e-10 || powers[0][8] < 0.9)
            return fail(__LINE__);
        auto capture = std::make_shared<Common::AnalysisCapture>(Common::AnalysisKind::oscilloscope, 1);
        std::vector<float> largeBlock(8192, 0.25f);
        const float *input = largeBlock.data();
        capture->push(&input, unsigned(largeBlock.size()), 48000);
        if (capture->samples.size() != 8192 || capture->revision.load() != 0)
            return fail(__LINE__);
        std::puts("PASS common FFT, anti-phase power aggregation and supported 8192-frame capture");
        const auto gray = Common::spectrogramPaletteColor(Common::SpectrogramPalette::grayscale, 0.25);
        const auto fire = Common::spectrogramPaletteColor(Common::SpectrogramPalette::fire, 0.75);
        const auto rainbow = Common::spectrogramPaletteColor(Common::SpectrogramPalette::rainbow, 0);
        if (gray != std::array{0.25, 0.25, 0.25} || fire[0] <= fire[1] || fire[1] <= fire[2] ||
            std::abs(rainbow[0] - 23.0 / 255) > 1e-12 || Common::spectrogramColorAmount(-72) != 0 ||
            Common::spectrogramColorAmount(-36) != 0.5 || Common::spectrogramColorAmount(0) != 1 ||
            Common::spectrogramColorAmount(12) != 1)
            return fail(__LINE__);
        std::array<std::array<double, 512>, 8> overloaded{};
        overloaded[0][8] = 4;
        if (std::abs(Common::analysisOverloadDb(overloaded, 2, false) - 10 * std::log10(2)) > 1e-12 ||
            std::abs(Common::analysisOverloadDb(overloaded, 2, true) - 10 * std::log10(4)) > 1e-12)
            return fail(__LINE__);
    }
    for (const auto *type :
         {"oscilloscope", "spectrogram", "polar", "spectrum_bars", "spectrum_curve", "loudness"})
        for (const auto *layout : {"mono", "stereo", "5_1", "7_1"})
        {
            const auto kind = QString::fromLatin1(type) + '_' + layout;
            std::printf("CASE %s\n", kind.toUtf8().constData());
            std::fflush(stdout);
            const unsigned channels = QString(layout) == "mono"     ? 1
                                      : QString(layout) == "stereo" ? 2
                                      : QString(layout) == "5_1"    ? 6
                                                                    : 8;
            QLibrary library(app.applicationDirPath() + "/nodes/control_deck/" +
                             SS::moduleRelativeFolder(kind) + "/wjn_signal_switch_" + kind + ".dll");
            if (!library.load())
            {
                std::puts(library.errorString().toUtf8().constData());
                return fail(__LINE__);
            }
            const auto create = reinterpret_cast<WinJACKNexus::Node::INode *(*)(uint32_t)>(
                library.resolve("wjn_create_node_v4"));
            const auto destroy =
                reinterpret_cast<void (*)(WinJACKNexus::Node::INode *)>(library.resolve("wjn_destroy_node"));
            const auto getApi = reinterpret_cast<const WjnNodeVirtualApiV1 *(*)()>(
                library.resolve("wjn_get_node_virtual_api_v1"));
            const auto get = reinterpret_cast<const char *(*)(WinJACKNexus::Node::INode *)>(
                library.resolve("wjn_get_node_settings_json"));
            const auto set = reinterpret_cast<int (*)(WinJACKNexus::Node::INode *, const char *)>(
                library.resolve("wjn_set_node_settings_json"));
            if (!create || !destroy || !getApi || !get || !set)
                return fail(__LINE__);
            auto *node = create(WJN_NODE_MODE_VIRT);
            const auto *api = getApi();
            WjnNodePrepareV1 prepare{sizeof(prepare), 256, 48000, nullptr};
            if (!node || !api || api->portCount(node) != channels || !api->prepare(node, &prepare))
                return fail(__LINE__);
            for (unsigned channel = 0; channel < channels; ++channel)
            {
                WjnNodePortV1 port{sizeof(port)};
                if (!api->port(node, channel, &port) || port.direction != WJN_NODE_PORT_INPUT)
                    return fail(__LINE__);
            }
            auto source = service.analysis(service.endpoints[node].id);
            if (!source || source->channels != channels || source->separate.load() ||
                source->overlay.load() ||
                source->spectrogramPalette.load() != Common::SpectrogramPalette::rainbow)
                return fail(__LINE__);
            auto *editor = service.showEditor();
            editor->resize(1000, 800);
            service.state.cards[service.endpoints[node].id].geometry = QRect(15, 15, 800, 620);
            service.notify();
            if (!QTest::qWaitForWindowExposed(editor))
                return fail(__LINE__);
            auto *plot = dynamic_cast<UI::AudioAnalysisWidget *>(
                editor->findChild<QWidget *>("controlDeck.analysisPlot"));
            if (!plot)
                return fail(__LINE__);
            std::array<std::array<float, 256>, 8> samples{};
            std::array<float *, 8> pointers{};
            std::array<WjnNodeAudioBufferV1, 8> inputs{};
            for (unsigned channel = 0; channel < channels; ++channel)
            {
                pointers[channel] = samples[channel].data();
                inputs[channel] = {channel + 1, 1, &pointers[channel]};
            }
            WjnNodeProcessV1 block{};
            block.structSize = sizeof(block);
            block.frames = 256;
            block.audioInputCount = channels;
            block.audioInputs = inputs.data();
            std::uint64_t time = 0;
            std::unique_ptr<Common::LoudnessEngine> reference;
            if (source->kind == Common::AnalysisKind::loudness)
            {
                reference = std::make_unique<Common::LoudnessEngine>();
                const auto referenceLayout = channels == 1   ? Common::MeterChannelLayout::mono()
                                             : channels == 2 ? Common::MeterChannelLayout::stereo()
                                             : channels == 6 ? Common::MeterChannelLayout::fivePointOne()
                                                             : Common::MeterChannelLayout::sevenPointOne();
                if (!reference->prepare({48000, true}, referenceLayout))
                    return fail(__LINE__);
            }
            const auto feed = [&](unsigned blocks, bool silent = false, double amplitude = 0.25)
            {
                for (unsigned count = 0; count < blocks; ++count)
                {
                    for (unsigned channel = 0; channel < channels; ++channel)
                        for (unsigned index = 0; index < 256; ++index)
                            samples[channel][index] =
                                silent ? 0
                                       : float(amplitude * std::sin(2 * std::numbers::pi * (channel + 1) *
                                                                    375 * (time + index) / 48000));
                    api->process(node, &block);
                    if (reference)
                    {
                        std::array<const float *, 8> referenceInputs{};
                        for (unsigned channel = 0; channel < channels; ++channel)
                            referenceInputs[channel] = samples[channel].data();
                        reference->process(referenceInputs.data(), channels, 256);
                    }
                    time += 256;
                    QTest::qWait(6);
                }
            };
            if (!editor->findChildren<QComboBox *>().isEmpty())
                return fail(__LINE__);
            feed(source->kind == Common::AnalysisKind::loudness ? 90 : 12);
            if (source->kind == Common::AnalysisKind::loudness)
            {
                if (!wait([&] { return source->loudnessSnapshot().momentaryLufs.valid; }))
                    return fail(__LINE__);
                if (!wait(
                        [&]
                        {
                            return source->loudnessSnapshot().processedFrames ==
                                   reference->snapshot().processedFrames;
                        }))
                    return fail(__LINE__);
                const auto measured = source->loudnessSnapshot();
                const auto expected = reference->snapshot();
                if (std::abs(measured.momentaryLufs.value - expected.momentaryLufs.value) > 1e-6 ||
                    std::abs(measured.truePeakDbtp.value - expected.truePeakDbtp.value) > 1e-6)
                    return fail(__LINE__);
                reference.reset();
                const auto beforeSilence = source->loudnessSnapshot().processedFrames;
                editor->hide();
                feed(30, true);
                feed(2);
                if (!wait([&] { return source->loudnessSnapshot().processedFrames < beforeSilence; }))
                    return fail(__LINE__);
                const auto countCsv = [](const QString &directory)
                {
                    unsigned count = 0;
                    QDirIterator files(directory, {"*.csv"}, QDir::Files, QDirIterator::Subdirectories);
                    while (files.hasNext())
                    {
                        files.next();
                        ++count;
                    }
                    return count;
                };
                const auto beforeGlobalCsv = countCsv(temporary.filePath("global-csv"));
                const auto beforeIndependentCsv = countCsv(temporary.filePath("independent-csv"));
                source->csvEnabled = true;
                source->csvUseGlobal = true;
                service.notify(true);
                feed(30);
                if (!wait([&] { return countCsv(temporary.filePath("global-csv")) > beforeGlobalCsv; }))
                    return fail(__LINE__);
                source->csvUseGlobal = false;
                source->csvDirectory = temporary.filePath("independent-csv");
                service.notify(true);
                feed(30);
                if (!wait([&] { return countCsv(source->csvDirectory) > beforeIndependentCsv; }))
                    return fail(__LINE__);
                feed(20);
                if (channels == 8)
                {
                    service.showEditor();
                    if (!QTest::qWaitForWindowExposed(editor))
                        return fail(__LINE__);
                    QTest::qWait(80);
                    unsigned readoutCount = 0, visibleReadouts = 0;
                    for (auto *child : plot->findChildren<QWidget *>())
                        if (auto *readout = dynamic_cast<UI::MetricReadout *>(child))
                        {
                            ++readoutCount;
                            visibleReadouts += readout->isVisible();
                        }
                    if (readoutCount != 8 || visibleReadouts != 5)
                        return fail(__LINE__);
                    if (!editor->screen()
                             ->grabWindow(editor->winId())
                             .save(app.applicationDirPath() + "/../../control-deck-loudness-numeric.png"))
                        return fail(__LINE__);
                }
                source->numeric.store(false);
                source->loudnessPreset = "netease-cloud-music";
                service.notify(true);
                service.showEditor();
                if (!QTest::qWaitForWindowExposed(editor))
                    return fail(__LINE__);
                if (source->metricMask.load() != 0x1f)
                    return fail(__LINE__);
                bool menuChecked = false;
                QTimer::singleShot(40, editor,
                                   [&]
                                   {
                                       auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                                       if (!menu)
                                           return;
                                       while (auto *parent = qobject_cast<QMenu *>(menu->parentWidget()))
                                           menu = parent;
                                       menuChecked = true;
                                       auto *vertical =
                                           menu->findChild<QAction *>("controlDeck.arrangement.0");
                                       auto *horizontal =
                                           menu->findChild<QAction *>("controlDeck.arrangement.1");
                                       menuChecked &= vertical && vertical->isChecked() && horizontal;
                                       if (horizontal)
                                           horizontal->trigger();
                                       for (unsigned index = 0; index < 8; ++index)
                                       {
                                           auto *action = menu->findChild<QAction *>(
                                               QStringLiteral("controlDeck.metric.%1").arg(index));
                                           menuChecked &= action && action->isChecked() == (index < 5);
                                       }
                                       for (auto *action : menu->actions())
                                           menuChecked &= action->objectName() != "controlDeck.exclusive" &&
                                                          action->objectName() != "controlDeck.independent";
                                       if (auto *action = menu->findChild<QAction *>("controlDeck.metric.5"))
                                           action->setChecked(true);
                                       menu->close();
                                   });
                QMetaObject::invokeMethod(plot, "customContextMenuRequested", Qt::DirectConnection,
                                          Q_ARG(QPoint, QPoint(5, 5)));
                QTest::qWait(200);
                if (!menuChecked || source->metricMask.load() != 0x3f || source->verticalArrangement.load())
                {
                    std::printf("MENU checked=%d mask=%u vertical=%d\n", menuChecked,
                                source->metricMask.load(), source->verticalArrangement.load());
                    return fail(__LINE__);
                }
            }
            else
            {
                if (!wait([&] { return plot->rmsLevels()[0] > 0.1; }))
                    return fail(__LINE__);
                if (source->kind != Common::AnalysisKind::oscilloscope &&
                    source->kind != Common::AnalysisKind::polar)
                    if (std::abs(plot->dominantFrequency(0) - 375) > 48)
                        return fail(__LINE__);
            }
            for (bool separate : {false, true})
            {
                source->separate.store(separate);
                source->logarithmic.store(separate);
                feed(QString(layout) == "7_1" ? 60 : 6);
                if (QString(layout) == "7_1")
                {
                    const auto path = app.applicationDirPath() + "/../../control-deck-" +
                                      QString::fromLatin1(type) +
                                      (separate ? "-separate.png" : "-combined.png");
                    if (!editor->screen()->grabWindow(editor->winId()).save(path))
                        return fail(__LINE__);
                }
            }
            if (QString(layout) == "7_1" && source->kind != Common::AnalysisKind::polar)
            {
                source->separate.store(true);
                if (source->kind == Common::AnalysisKind::loudness)
                    source->numeric.store(true);
                for (unsigned arrangement = 0; arrangement < 3; ++arrangement)
                {
                    service.state.cards[service.endpoints[node].id].geometry.setWidth(arrangement == 1 ? 880
                                                                                                       : 800);
                    service.notify();
                    source->verticalArrangement.store(arrangement == 0);
                    source->layoutRows.store(arrangement == 2 ? 2 : 0);
                    source->layoutColumns.store(arrangement == 2 ? 4 : 0);
                    feed(20);
                    if (!editor->screen()
                             ->grabWindow(editor->winId())
                             .save(app.applicationDirPath() + "/../../control-deck-" +
                                   QString::fromLatin1(type) + "-arrangement-" +
                                   QString::number(arrangement) + ".png"))
                        return fail(__LINE__);
                }
                if (source->kind == Common::AnalysisKind::loudness)
                    source->numeric.store(false);
                if (source->kind == Common::AnalysisKind::spectrogram ||
                    source->kind == Common::AnalysisKind::spectrumBars ||
                    source->kind == Common::AnalysisKind::spectrumCurve)
                {
                    source->layoutRows.store(8);
                    source->layoutColumns.store(8);
                    feed(6);
                    if (!editor->screen()
                             ->grabWindow(editor->winId())
                             .save(app.applicationDirPath() + "/../../control-deck-" +
                                   QString::fromLatin1(type) + "-compact-axes-hidden.png"))
                        return fail(__LINE__);
                    source->layoutRows.store(2);
                    source->layoutColumns.store(4);
                }
            }
            if (source->kind == Common::AnalysisKind::spectrogram)
            {
                bool paletteChosen = false;
                QTimer::singleShot(40, editor,
                                   [&]
                                   {
                                       auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                                       if (!menu)
                                           return;
                                       while (auto *parent = qobject_cast<QMenu *>(menu->parentWidget()))
                                           menu = parent;
                                       auto *rainbow =
                                           menu->findChild<QAction *>("controlDeck.spectrogramPalette.0");
                                       auto *custom =
                                           menu->findChild<QAction *>("controlDeck.spectrogramPalette.4");
                                       paletteChosen = rainbow && rainbow->isChecked() && custom;
                                       if (custom)
                                           custom->trigger();
                                       menu->close();
                                   });
                QMetaObject::invokeMethod(plot, "customContextMenuRequested", Qt::DirectConnection,
                                          Q_ARG(QPoint, QPoint(5, 5)));
                QTest::qWait(200);
                if (!paletteChosen || source->spectrogramPalette.load() != Common::SpectrogramPalette::custom)
                    return fail(__LINE__);
                if (channels == 8)
                    for (unsigned palette = 0; palette < 5; ++palette)
                    {
                        source->separate.store(false);
                        source->spectrogramPalette.store(static_cast<Common::SpectrogramPalette>(palette));
                        feed(20);
                        if (!editor->screen()
                                 ->grabWindow(editor->winId())
                                 .save(app.applicationDirPath() + "/../../control-deck-spectrogram-palette-" +
                                       QString::number(palette) + ".png"))
                            return fail(__LINE__);
                    }
            }
            if (source->kind == Common::AnalysisKind::spectrumCurve)
            {
                bool overlayChosen = false;
                QTimer::singleShot(40, editor,
                                   [&]
                                   {
                                       auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                                       if (!menu)
                                           return;
                                       while (auto *parent = qobject_cast<QMenu *>(menu->parentWidget()))
                                           menu = parent;
                                       if (auto *action =
                                               menu->findChild<QAction *>("controlDeck.analysisMode.2"))
                                       {
                                           action->trigger();
                                           overlayChosen = true;
                                       }
                                       menu->close();
                                   });
                QMetaObject::invokeMethod(plot, "customContextMenuRequested", Qt::DirectConnection,
                                          Q_ARG(QPoint, QPoint(5, 5)));
                QTest::qWait(200);
                if (!overlayChosen || !source->overlay.load() || source->separate.load())
                    return fail(__LINE__);
                feed(20);
                if (channels == 8 &&
                    !editor->screen()
                         ->grabWindow(editor->winId())
                         .save(app.applicationDirPath() + "/../../control-deck-spectrum-curve-overlay.png"))
                    return fail(__LINE__);
            }
            if (source->kind == Common::AnalysisKind::spectrogram)
            {
                auto *badge = editor->findChild<QLabel *>("controlDeck.overloadBadge");
                feed(8, false, 4.0);
                if (!badge || !wait([&] { return plot->overloadDb() > 2 && badge->isVisible(); }))
                    return fail(__LINE__);
                if (badge->palette().color(QPalette::WindowText) != QColor("#f04452"))
                    return fail(__LINE__);
                if (channels == 8 &&
                    !editor->screen()
                         ->grabWindow(editor->winId())
                         .save(app.applicationDirPath() + "/../../control-deck-spectrogram-overload.png"))
                    return fail(__LINE__);
                if (channels == 8)
                {
                    const QString token = "--wjn-theme-global-analysis-overload-text";
                    if (!theme.setOverride(token, "#00ff00", {}, nullptr) ||
                        !wait([&]
                              { return badge->palette().color(QPalette::WindowText) == QColor("#00ff00"); }))
                        return fail(__LINE__);
                    theme.removeOverride(token);
                }
                feed(8);
                if (!wait([&] { return plot->overloadDb() == 0 && !badge->isVisible(); }))
                    return fail(__LINE__);
            }
            const auto saved = QByteArray(get(node));
            const auto savedSeparate = source->separate.load();
            const auto savedOverlay = source->overlay.load();
            const auto savedPalette = source->spectrogramPalette.load();
            const auto savedRows = source->layoutRows.load();
            const auto savedColumns = source->layoutColumns.load();
            const auto savedVertical = source->verticalArrangement.load();
            source->layoutRows.store(0);
            source->layoutColumns.store(0);
            source->verticalArrangement.store(!savedVertical);
            source->overlay.store(!savedOverlay);
            source->separate.store(!savedSeparate);
            source->spectrogramPalette.store(Common::SpectrogramPalette::fire);
            if (!set(node, saved.constData()))
                return fail(__LINE__);
            if (source->layoutRows.load() != savedRows || source->layoutColumns.load() != savedColumns ||
                source->verticalArrangement.load() != savedVertical)
                return fail(__LINE__);
            if (source->separate.load() != savedSeparate || source->overlay.load() != savedOverlay ||
                !source->logarithmic.load() || source->spectrogramPalette.load() != savedPalette)
                return fail(__LINE__);
            if (source->kind == Common::AnalysisKind::loudness &&
                (!source->csvEnabled || source->csvUseGlobal || source->numeric.load() ||
                 source->loudnessPreset != "netease-cloud-music" || source->metricMask.load() != 0x3f))
                return fail(__LINE__);
            api->stop(node);
            destroy(node);
            source.reset();
            std::printf("PASS %s: ports, real callback analysis, modes/state and real Windows rendering\n",
                        kind.toUtf8().constData());
        }
    service.shutdownUI();
    if (shows.shows > 100)
        return fail(__LINE__);
    std::printf("WINDOW_SHOWS %d\n", shows.shows);
    std::puts("PASS all 24 analyzer/loudness layouts; silence rearm/resume reset, CSV while hidden, "
              "global/independent destinations; no JACK server");
    return 0;
}
