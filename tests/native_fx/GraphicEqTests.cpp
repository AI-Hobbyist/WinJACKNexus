/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/NativeFx/graphic_eq/Processor.hpp>
#include <WinJACKNexus/NativeFx/graphic_eq/Ui.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QFontMetrics>
#include <QImage>
#include <QKeyEvent>
#include <QLibrary>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QPointer>
#include <QTimer>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <memory>

namespace {
bool check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", message);
    std::fflush(stdout);
    return condition;
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    bool passed = true;
    passed &= check(argc > 2, "Native Fx module path is provided");
    if (argc <= 2)
        return 1;

    QLibrary module(QString::fromLocal8Bit(argv[1]));
    passed &= check(module.load(), "Native Fx module loads");
    using GetCount = std::uint32_t (*)();
    using GetName = const char* (*)(std::uint32_t);
    using GetCategory = const char* (*)(std::uint32_t);
    using CreateNode = WinJACKNexus::Node::INode* (*)(std::uint32_t);
    using DestroyNode = void (*)(WinJACKNexus::Node::INode*);
    using CreateSettingsWidget = QWidget* (*)(WinJACKNexus::Node::INode*, QWidget*);
    using DestroySettingsWidget = void (*)(WinJACKNexus::Node::INode*, QWidget*);
    using GetClientId = const char* (*)(WinJACKNexus::Node::INode*);
    using CreateSettingsView = void* (*)(WinJACKNexus::Node::INode*, void*);
    const auto getCount = reinterpret_cast<GetCount>(module.resolve("wjn_get_node_count"));
    const auto getName = reinterpret_cast<GetName>(module.resolve("wjn_get_node_name"));
    const auto getCategory = reinterpret_cast<GetCategory>(module.resolve("wjn_get_node_category"));
    const auto createNode = reinterpret_cast<CreateNode>(module.resolve("wjn_create_node"));
    const auto destroyNode = reinterpret_cast<DestroyNode>(module.resolve("wjn_destroy_node"));
    const auto createSettingsWidget = reinterpret_cast<CreateSettingsWidget>(module.resolve("wjn_create_settings_widget"));
    const auto destroySettingsWidget = reinterpret_cast<DestroySettingsWidget>(module.resolve("wjn_destroy_settings_widget"));
    const auto getClientId = reinterpret_cast<GetClientId>(module.resolve("wjn_get_node_client_id"));
    const auto createSettingsView = reinterpret_cast<CreateSettingsView>(module.resolve("wjn_create_settings_view"));
    passed &= check(getCount && getName && getCategory && createNode && destroyNode
                    && createSettingsWidget && destroySettingsWidget && getClientId && createSettingsView,
                    "Native Fx node entry points resolve");
    if (!getCount || !getName || !getCategory || !createNode || !destroyNode
        || !createSettingsWidget || !destroySettingsWidget || !getClientId || !createSettingsView)
        return 1;
    passed &= check(getCount() == 1 && QString::fromUtf8(getName(0)) == QStringLiteral("Graphic EQ")
                    && QString::fromUtf8(getCategory(0)) == QStringLiteral("Native Fx"),
                    "Graphic EQ node is exported in Native Fx category");
    auto* node = createNode(0);
    passed &= check(node != nullptr, "Graphic EQ native node loads");
    if (node != nullptr) {
        const auto info = node->info();
        passed &= check(std::strcmp(info.name, "Graphic EQ") == 0, "Graphic EQ node metadata loads");
        QWidget sidebarParent;
        auto* sidebar = createSettingsWidget(node, &sidebarParent);
        passed &= check(sidebar != nullptr && sidebar->parentWidget() == &sidebarParent
                        && sidebar->findChildren<QLineEdit*>().size() == 1
                        && sidebar->findChildren<QPushButton*>().size() == 1,
                        "Sidebar contains only the client-name editor and apply action");
        if (sidebar != nullptr) {
            auto* editor = sidebar->findChild<QLineEdit*>();
            auto* applyButton = sidebar->findChild<QPushButton*>();
            if (editor != nullptr && applyButton != nullptr) {
                editor->setText(QStringLiteral("GraphicEQ Test"));
                applyButton->click();
            }
            passed &= check(std::strcmp(getClientId(node), "GraphicEQ Test") == 0,
                            "Sidebar applies the JACK client name");
        }
        if (sidebar != nullptr)
            destroySettingsWidget(node, sidebar);
        passed &= check(createSettingsView(node, nullptr) == nullptr,
                        "Legacy sidebar view does not embed the effect window");
        const auto window = node->createWindow(nullptr);
        passed &= check(window != nullptr, "Graphic EQ opens its standalone UI without JACK connections");
        if (window != nullptr)
            node->destroyWindow(window);
        destroyNode(node);
    }

    WinJACKNexus::UI::GraphicEqWidget widget;
    passed &= check(widget.isWindow() && widget.parentWidget() == nullptr
                    && (widget.windowFlags() & Qt::Window),
                    "effect interface is an independent top-level window");
    auto titleFont = widget.font();
    titleFont.setPixelSize(23);
    titleFont.setBold(true);
    passed &= check(QFontMetrics(titleFont).horizontalAdvance(QStringLiteral("Graphic EQ 图示均衡器")) <= 263,
                    "Graphic EQ title fits before the enable switch");
    const WinJACKNexus::Common::GraphicEqParameters defaults;
    const auto flatByDefault = std::all_of(defaults.bandGainsDb.cbegin(), defaults.bandGainsDb.cend(),
                                           [](float gain) { return gain == 0.0F; });
    passed &= check(flatByDefault && defaults.outputGainDb == 0.0F,
                    "Graphic EQ factory response is flat");
    std::array<WinJACKNexus::Common::GraphicEqParameters, 8> changedSettings {};
    widget.setChangeHandler([&changedSettings](std::size_t index, const auto& parameters) {
        changedSettings[index] = parameters;
    });
    widget.show();
    app.processEvents();
    auto* refreshTimer = widget.findChild<QTimer*>(QStringLiteral("nativeFxSpectrumRefreshTimer"));
    const QPointF bandDragPoint(200, 300);
    QMouseEvent bandPress(QEvent::MouseButtonPress, bandDragPoint, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &bandPress);
    passed &= check(refreshTimer && refreshTimer->isActive(),
                    "Graphic EQ drag uses the application frame timer without spectrum data");
    auto& applicationSettings = WinJACKNexus::UI::ApplicationSettings::instance();
    const int previousFrameRate = applicationSettings.frameRate();
    applicationSettings.setFrameRate(30);
    passed &= check(refreshTimer && refreshTimer->interval() == 33,
                    "Graphic EQ drag switches to the 30 FPS interval");
    applicationSettings.setFrameRate(120);
    passed &= check(refreshTimer && refreshTimer->interval() == 8,
                    "Graphic EQ drag switches to the 120 FPS interval");
    applicationSettings.setFrameRate(previousFrameRate);
    QMouseEvent bandRelease(QEvent::MouseButtonRelease, bandDragPoint, Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &bandRelease);
    passed &= check(refreshTimer && !refreshTimer->isActive(),
                    "Graphic EQ drag timer stops when the band is released");
    QMouseEvent comboPress(QEvent::MouseButtonPress, QPointF(500, 190), Qt::LeftButton,
                           Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &comboPress);
    QMouseEvent comboRelease(QEvent::MouseButtonRelease, QPointF(500, 190), Qt::LeftButton,
                             Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &comboRelease);
    app.processEvents();
    QPointer<QMenu> presetMenu(qobject_cast<QMenu*>(QApplication::activePopupWidget()));
    passed &= check(presetMenu && presetMenu->actions().size() == 5,
                    "Preset dropdown opens with selectable entries");
    if (presetMenu && presetMenu->actions().size() == 5) {
        presetMenu->actions()[1]->trigger();
        if (presetMenu)
            presetMenu->close();
        app.processEvents();
        passed &= check(changedSettings[0].bandGainsDb == WinJACKNexus::Common::graphicEqVocalBrightGains,
                        "Preset dropdown applies the selected response");
    }
    const auto renderWidget = [&widget]() {
        QImage snapshot(widget.size(), QImage::Format_ARGB32_Premultiplied);
        snapshot.fill(Qt::transparent);
        widget.render(&snapshot);
        return snapshot;
    };
    const auto clickViewMode = [&widget, &app](qreal x) {
        const QPointF position(x, 591);
        QMouseEvent press(QEvent::MouseButtonPress, position, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, position, Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &release);
        app.processEvents();
    };
    const auto bandView = renderWidget();
    clickViewMode(397);
    const auto crossoverView = renderWidget();
    passed &= check(crossoverView != bandView,
                    "Display mode selector in the former frequency-range card switches to crossover view");
    clickViewMode(523);
    const auto curveView = renderWidget();
    passed &= check(curveView != crossoverView,
                    "Display mode selector switches to curve view");
    clickViewMode(270);

    auto spectrumAnalyzer = std::make_unique<WinJACKNexus::Common::GraphicEqSpectrumAnalyzer>();
    spectrumAnalyzer->setSampleRate(48000);
    std::array<WinJACKNexus::Common::GraphicEqProcessor, 8> spectrumProcessors;
    std::array<WinJACKNexus::Common::GraphicEqParameters, 8> spectrumParameters {};
    spectrumParameters[0].bandGainsDb[17] = 6.0F;
    for (std::size_t channel = 0; channel < spectrumProcessors.size(); ++channel) {
        spectrumProcessors[channel].prepare(48000.0);
        spectrumProcessors[channel].setParameters(spectrumParameters[channel]);
    }
    const auto feedSpectrumSamples = [&](std::size_t sampleCount) {
        for (std::size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
            std::array<float, WinJACKNexus::Common::graphicEqSpectrumChannelCount> inputSamples {};
            std::array<float, WinJACKNexus::Common::graphicEqSpectrumChannelCount> outputSamples {};
            const auto time = static_cast<float>(sampleIndex) / 48000.0F;
            inputSamples[0] = 0.1F * std::sin(2.0F * 3.14159265358979323846F * 1000.0F * time);
            inputSamples[1] = 0.12F * std::sin(2.0F * 3.14159265358979323846F * 5000.0F * time);
            for (std::size_t channel = 0; channel < spectrumProcessors.size(); ++channel)
                outputSamples[channel] = spectrumProcessors[channel].process(inputSamples[channel]);
            spectrumAnalyzer->pushSamples(inputSamples, outputSamples);
        }
    };
    feedSpectrumSamples(WinJACKNexus::Common::graphicEqSpectrumFftSize * 2);
    WinJACKNexus::Common::GraphicEqSpectrumFrame analyzedFrame {};
    const bool hasAnalyzedFrame = spectrumAnalyzer->tryPopLatest(analyzedFrame);
    WinJACKNexus::Common::GraphicEqSpectrumPair leftSpectrum {};
    WinJACKNexus::Common::GraphicEqSpectrumPair allSpectrum {};
    WinJACKNexus::Common::calculateGraphicEqSpectrum(analyzedFrame, 0, leftSpectrum);
    WinJACKNexus::Common::calculateGraphicEqSpectrum(
        analyzedFrame, WinJACKNexus::Common::graphicEqSpectrumAllChannels, allSpectrum);
    std::size_t leftPeak = 0;
    std::size_t allPeak = 0;
    for (std::size_t bin = 1; bin < leftSpectrum.inputDb.size(); ++bin) {
        if (leftSpectrum.inputDb[bin] > leftSpectrum.inputDb[leftPeak]) leftPeak = bin;
        if (allSpectrum.inputDb[bin] > allSpectrum.inputDb[allPeak]) allPeak = bin;
    }
    passed &= check(hasAnalyzedFrame && leftSpectrum.activeChannelCount == 1
                    && allSpectrum.activeChannelCount == 2,
                    "All includes only signal-bearing channels while L stays channel-specific");
    passed &= check(leftPeak >= 52 && leftPeak <= 56
                    && leftSpectrum.outputDb[leftPeak] > leftSpectrum.inputDb[leftPeak] + 3.0F
                    && allPeak >= 74 && allPeak <= 78
                    && allSpectrum.inputDb[allPeak] > leftSpectrum.inputDb[allPeak] + 6.0F,
                    "input reference and post-EQ spectra preserve each channel's signal");
    widget.setParameters(spectrumParameters);
    widget.setSpectrumFrameReader([&spectrumAnalyzer](auto& frame) {
        return spectrumAnalyzer->tryPopLatest(frame);
    });
    feedSpectrumSamples(WinJACKNexus::Common::graphicEqSpectrumFftSize);
    QEventLoop spectrumWait;
    QTimer::singleShot(100, &spectrumWait, &QEventLoop::quit);
    spectrumWait.exec();
    const auto allChannelsImage = renderWidget();
    const QRect graphArea(50, 246, 578, 248);
    QMouseEvent selectLeft(QEvent::MouseButtonPress, QPointF(138, 88), Qt::LeftButton,
                           Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &selectLeft);
    const auto leftChannelImage = renderWidget();
    passed &= check(allChannelsImage.copy(graphArea) != leftChannelImage.copy(graphArea),
                    "All and L display different spectra for different channel signals");
    QMouseEvent selectRight(QEvent::MouseButtonPress, QPointF(218, 88), Qt::LeftButton,
                            Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &selectRight);
    const auto rightChannelImage = renderWidget();
    passed &= check(leftChannelImage.copy(graphArea) != rightChannelImage.copy(graphArea),
                    "selected channel switches the graph to that channel only");
    passed &= check(rightChannelImage != allChannelsImage,
                    "input reference and EQ output render in the live graph background");
    const auto spectrumPreviewPath = QFileInfo(QString::fromLocal8Bit(argv[2])).dir()
        .filePath(QStringLiteral("GraphicEq-spectrum-preview.png"));
    passed &= check(allChannelsImage.save(spectrumPreviewPath),
                    "Post-EQ spectrum preview saved for visual QA");
    widget.setSpectrumFrameReader({});
    widget.hide();
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    widget.render(&image);
    passed &= check(!image.isNull() && image.pixelColor(10, 10).alpha() > 0, "Graphic EQ interface renders");
    passed &= check(image.save(QString::fromLocal8Bit(argv[2])), "Graphic EQ preview saved for visual QA");
    std::array<WinJACKNexus::Common::GraphicEqParameters, 8> curvedParameters {};
    for (auto& parameters : curvedParameters)
        parameters.bandGainsDb = WinJACKNexus::Common::graphicEqVocalBrightGains;
    widget.setParameters(curvedParameters);
    QImage curveImage(widget.size(), QImage::Format_ARGB32_Premultiplied);
    curveImage.fill(Qt::transparent);
    widget.render(&curveImage);
    const auto curvePreviewPath = QFileInfo(QString::fromLocal8Bit(argv[2])).dir()
        .filePath(QStringLiteral("GraphicEq-curve-preview.png"));
    passed &= check(curveImage.save(curvePreviewPath),
                    "Unfilled Vocal Bright curve preview saved for visual QA");

    WinJACKNexus::UI::GraphicEqWidget controlWidget;
    std::array<WinJACKNexus::Common::GraphicEqParameters, 8> editedSettings {};
    controlWidget.setChangeHandler([&editedSettings](std::size_t index, const auto& parameters) {
        editedSettings[index] = parameters;
    });
    controlWidget.show();
    app.processEvents();
    const auto clickControl = [&](const QPointF& position) {
        QMouseEvent press(QEvent::MouseButtonPress, position, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&controlWidget, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, position, Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&controlWidget, &release);
        app.processEvents();
    };
    const auto enterNumeric = [&](const QString& text) {
        auto* editor = controlWidget.findChild<QLineEdit*>();
        if (!editor || !editor->isVisible()) return false;
        editor->setText(text);
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(editor, &enter);
        app.processEvents();
        return true;
    };
    constexpr qreal mainPanelTop = 146.0;
    clickControl({50, 300});
    clickControl({450, mainPanelTop + 477});
    const bool bandEditorOpened = enterNumeric(QStringLiteral("4.5"));
    passed &= check(bandEditorOpened && std::all_of(editedSettings.cbegin(), editedSettings.cend(),
                    [](const auto& parameters) { return parameters.bandGainsDb[0] == 4.5F; }),
                    "Selected Graphic EQ band accepts an exact value in All mode");
    QMouseEvent bandDoubleClick(QEvent::MouseButtonDblClick, QPointF(50, 300), Qt::LeftButton,
                                Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&controlWidget, &bandDoubleClick);
    passed &= check(std::all_of(editedSettings.cbegin(), editedSettings.cend(),
                    [](const auto& parameters) { return parameters.bandGainsDb[0] == 0.0F; }),
                    "Graphic EQ band double-click restores its default gain");
    clickControl({110, mainPanelTop + 478});
    const bool outputEditorOpened = enterNumeric(QStringLiteral("-6.0"));
    passed &= check(outputEditorOpened && std::all_of(editedSettings.cbegin(), editedSettings.cend(),
                    [](const auto& parameters) { return parameters.outputGainDb == -6.0F; }),
                    "Graphic EQ output gain accepts an exact value in All mode");
    QMouseEvent knobDoubleClick(QEvent::MouseButtonDblClick,
                                QPointF(119, mainPanelTop + 442), Qt::LeftButton,
                                Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&controlWidget, &knobDoubleClick);
    passed &= check(std::all_of(editedSettings.cbegin(), editedSettings.cend(),
                    [](const auto& parameters) { return parameters.outputGainDb == 0.0F; }),
                    "Graphic EQ output knob double-click restores unity gain");
    return passed ? 0 : 1;
}
