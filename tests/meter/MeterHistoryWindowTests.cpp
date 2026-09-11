/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <WinJACKNexus/Meter/MockMeterDataProvider.hpp>
#include <WinJACKNexus/Meter/Mod/MeterCard.hpp>
#include <WinJACKNexus/Meter/Mod/MeterHistoryWindow.hpp>
#include <WinJACKNexus/Meter/Mod/MeterSettingsWidget.hpp>
#include <WinJACKNexus/Meter/Mod/MeterbridgeWindow.hpp>
#include <WinJACKNexus/UI/HistoryChartWidget.hpp>
#include <WinJACKNexus/UI/HistoryLegendWidget.hpp>
#include <WinJACKNexus/UI/MetricStatisticsWidget.hpp>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QSlider>
#include <QSplitter>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QThread>
#include <QToolButton>

#include <algorithm>
#include <cstdio>

namespace {

using WinJACKNexus::Meter::MeterHistoryViewMode;
using WinJACKNexus::Meter::MockMeterDataProvider;
using WinJACKNexus::Meter::MockMeterScenario;
using WinJACKNexus::Meter::Mod::MeterCard;
using WinJACKNexus::Meter::Mod::MeterHistoryWindow;
using WinJACKNexus::Meter::Mod::MeterbridgeWindow;
using WinJACKNexus::UI::HistoryChartAxis;

bool check(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "FAILED: %s\n", message);
    return condition;
}

bool imageHasVisiblePixels(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) != 0)
                return true;
        }
    }
    return false;
}

bool testHistoryWindowWorkflow()
{
    MockMeterDataProvider provider;
    MeterbridgeWindow bridge(provider);
    bridge.resize(820, 760);
    bridge.show();
    QCoreApplication::processEvents();

    bool passed = true;
    auto* tabs = bridge.findChild<QTabWidget*>(QStringLiteral("meterbridge.tabs"));
    passed &= check(tabs != nullptr && tabs->count() == 2
                        && tabs->tabText(0) == QStringLiteral("电平表桥")
                        && tabs->tabText(1) == QStringLiteral("历史图表"),
                    "meter bridge and history chart share a Chinese two-tab window");
    auto* card = bridge.cardForInstanceId(QStringLiteral("meter-preview-1"));
    auto* historyAction = card != nullptr ? card->findChild<QAction*>(QStringLiteral("meterCard.historyAction")) : nullptr;
    if (historyAction != nullptr)
        historyAction->trigger();
    QCoreApplication::processEvents();

    auto* historyWindow = bridge.historyWindowForInstanceId(QStringLiteral("meter-preview-1"));
    passed &= check(historyWindow != nullptr && historyWindow->isVisible() && !historyWindow->isWindow()
                        && tabs != nullptr && tabs->currentWidget() == historyWindow
                        && historyWindow->instanceId() == QStringLiteral("meter-preview-1"),
                    "history action selects the shared history tab for the stable card identity");
    if (historyWindow == nullptr)
        return false;

    auto* chart = historyWindow->chart();
    auto* legend = historyWindow->legend();
    auto* statistics = historyWindow->statistics();
    passed &= check(chart != nullptr && legend != nullptr && statistics != nullptr
                        && chart->series().size() == 8 && chart->rightMinimumValue() == 0.0
                        && chart->rightMaximumValue() == 20.0 && chart->series().back().axis == HistoryChartAxis::right
                        && chart->endTimeMilliseconds() - chart->startTimeMilliseconds() == 600000
                        && legend->entries().size() == 8 && statistics->entries().size() == 7,
                    "history window projects its eight curves and seven populated default statistics with a stable ten-minute window and LRA right axis");

    auto* peakSeriesToggle = historyWindow->findChild<QCheckBox*>(QStringLiteral("meterHistory.series.peak"));
    auto* momentaryReferenceToggle = historyWindow->findChild<QCheckBox*>(QStringLiteral("meterHistory.reference.momentary"));
    auto* recordingToggle = historyWindow->findChild<QCheckBox*>(QStringLiteral("meterHistory.recordingToggle"));
    auto* channelSelector = historyWindow->findChild<QComboBox*>(QStringLiteral("meterHistory.channelSelector"));
    auto* hourRangeButton = historyWindow->findChild<QToolButton*>(QStringLiteral("meterHistory.range.3600000"));
    auto* chartSettingsButton = historyWindow->findChild<QToolButton*>(QStringLiteral("meterHistory.chartSettingsButton"));
    auto* splitter = historyWindow->findChild<QSplitter*>(QStringLiteral("meterHistory.splitter"));
    const auto secondChannelIndex = channelSelector != nullptr
        ? channelSelector->findData(QStringLiteral("meter-preview-2"))
        : -1;
    passed &= check(peakSeriesToggle != nullptr && peakSeriesToggle->text() == QStringLiteral("峰值（Peak）")
                        && momentaryReferenceToggle != nullptr && momentaryReferenceToggle->text() == QStringLiteral("瞬时（Momentary） -18 LUFS")
                        && recordingToggle != nullptr && recordingToggle->text() == QStringLiteral("启用实时记录到 CSV")
                        && channelSelector != nullptr && channelSelector->count() == 2 && secondChannelIndex >= 0
                        && hourRangeButton != nullptr && chartSettingsButton != nullptr && splitter != nullptr
                        && chartSettingsButton->text() == QStringLiteral("图表设置"),
                    "history workbench presents Chinese controls, chart actions, and two preview channels");
    if (splitter != nullptr)
        splitter->setSizes({ 1, 1000 });
    if (chartSettingsButton != nullptr)
        chartSettingsButton->click();
    QCoreApplication::processEvents();
    passed &= check(splitter != nullptr && splitter->sizes().front() >= 250,
                    "chart settings action restores access to the sidebar");
    if (peakSeriesToggle != nullptr)
        peakSeriesToggle->click();
    if (momentaryReferenceToggle != nullptr)
        momentaryReferenceToggle->click();
    QCoreApplication::processEvents();
    const auto snapshotWithHiddenPeak = provider.snapshots().front();
    const auto hiddenPeak = std::find(snapshotWithHiddenPeak.historyView.visibleSeriesIds.cbegin(),
                                      snapshotWithHiddenPeak.historyView.visibleSeriesIds.cend(),
                                      QStringLiteral("peak"));
    const auto hiddenMomentaryReference = std::find_if(chart->referenceLines().cbegin(), chart->referenceLines().cend(),
                                                        [](const auto& line) {
                                                            return line.label == QStringLiteral("瞬时（Momentary） -18 LUFS") && !line.visible;
                                                        });
    passed &= check(hiddenPeak == snapshotWithHiddenPeak.historyView.visibleSeriesIds.cend()
                        && !chart->series().front().visible
                        && hiddenMomentaryReference != chart->referenceLines().cend(),
                    "sidebar curve and reference controls update the chart deterministically");

    if (hourRangeButton != nullptr)
        hourRangeButton->click();
    QCoreApplication::processEvents();
    passed &= check(hourRangeButton != nullptr && hourRangeButton->isChecked()
                        && provider.snapshots().front().historyView.timeWindowMilliseconds == 3600000
                        && chart->endTimeMilliseconds() - chart->startTimeMilliseconds() == 3600000,
                    "one-hour range shortcut remains active through the preview provider");

    auto* viewModeSelector = historyWindow->findChild<QComboBox*>(QStringLiteral("meterHistory.viewMode"));
    auto* scanCursor = historyWindow->findChild<QSlider*>(QStringLiteral("meterHistory.scanCursor"));
    if (viewModeSelector != nullptr)
        viewModeSelector->setCurrentIndex(1);
    QCoreApplication::processEvents();
    passed &= check(viewModeSelector != nullptr && scanCursor != nullptr
                        && provider.snapshots().front().historyView.mode == MeterHistoryViewMode::scanning
                        && scanCursor->isVisible() && chart->timeCursorMilliseconds().has_value(),
                    "scan mode switches through the provider contract and exposes a cursor");

    const auto scanCursorBefore = chart->timeCursorMilliseconds();
    QThread::msleep(140);
    QCoreApplication::processEvents();
    const auto scanCursorAfter = chart->timeCursorMilliseconds();
    passed &= check(scanCursorBefore.has_value() && scanCursorAfter.has_value() && scanCursorAfter != scanCursorBefore,
                    "scan mode advances its cursor while the history window is visible");

    auto* pauseAction = historyWindow->findChild<QAction*>(QStringLiteral("meterHistory.pauseAction"));
    if (pauseAction != nullptr)
        pauseAction->trigger();
    QCoreApplication::processEvents();
    const auto pausedCursor = chart->timeCursorMilliseconds();
    QThread::msleep(140);
    QCoreApplication::processEvents();
    passed &= check(pauseAction != nullptr && provider.snapshots().front().historyView.paused
                        && chart->timeCursorMilliseconds() == pausedCursor,
                    "pausing the history view freezes the scan cursor");

    passed &= check(legend->setEntryVisible(QStringLiteral("lra"), false),
                    "history legend accepts a visibility toggle");
    QCoreApplication::processEvents();
    const auto snapshotWithHiddenLra = provider.snapshots().front();
    const auto hiddenLra = std::find(snapshotWithHiddenLra.historyView.visibleSeriesIds.cbegin(),
                                     snapshotWithHiddenLra.historyView.visibleSeriesIds.cend(),
                                     QStringLiteral("lra"));
    passed &= check(hiddenLra == snapshotWithHiddenLra.historyView.visibleSeriesIds.cend()
                        && !chart->series().back().visible,
                    "history legend visibility propagates to the chart snapshot");

    auto* addMarkerAction = historyWindow->findChild<QAction*>(QStringLiteral("meterHistory.addMarkerAction"));
    auto* removeMarkerAction = historyWindow->findChild<QAction*>(QStringLiteral("meterHistory.removeMarkerAction"));
    if (addMarkerAction != nullptr)
        addMarkerAction->trigger();
    QCoreApplication::processEvents();
    auto* markerTable = historyWindow->findChild<QTableWidget*>(QStringLiteral("meterHistory.markerTable"));
    passed &= check(markerTable != nullptr && historyWindow->markerCount() == 1
                        && provider.snapshots().front().markers.size() == 1,
                    "history marker commands retain preview marker metadata");
    if (markerTable != nullptr)
        markerTable->selectRow(0);
    if (removeMarkerAction != nullptr)
        removeMarkerAction->trigger();
    QCoreApplication::processEvents();
    passed &= check(historyWindow->markerCount() == 0 && provider.snapshots().front().markers.empty(),
                    "selected history markers can be removed through the provider contract");

    auto* exportAction = historyWindow->findChild<QAction*>(QStringLiteral("meterHistory.exportAction"));
    if (exportAction != nullptr)
        exportAction->trigger();
    QCoreApplication::processEvents();
    const auto* csvPath = historyWindow->findChild<QLabel*>(QStringLiteral("meterHistory.csvPath"));
    const auto* exportStatus = historyWindow->findChild<QLabel*>(QStringLiteral("meterHistory.exportStatus"));
    passed &= check(csvPath != nullptr && csvPath->text().contains(QStringLiteral("meter-preview-1-preview.csv"))
                        && exportStatus != nullptr && exportStatus->text().contains(QStringLiteral("不会写入文件")),
                    "CSV and PNG controls remain preview-only in UI-4");

    const auto firstHistoryWindow = historyWindow;
    if (channelSelector != nullptr && secondChannelIndex >= 0)
        channelSelector->setCurrentIndex(secondChannelIndex);
    QCoreApplication::processEvents();
    if (recordingToggle != nullptr)
        recordingToggle->click();
    QCoreApplication::processEvents();
    const auto snapshotsAfterRecording = provider.snapshots();
    const auto secondSnapshotAfterRecording = std::find_if(snapshotsAfterRecording.cbegin(), snapshotsAfterRecording.cend(), [](const auto& snapshot) {
        return snapshot.instanceId == QStringLiteral("meter-preview-2");
    });
    passed &= check(secondSnapshotAfterRecording != snapshotsAfterRecording.cend()
                        && historyWindow->instanceId() == QStringLiteral("meter-preview-2")
                        && csvPath != nullptr && csvPath->text().contains(QStringLiteral("meter-preview-2-preview.csv"))
                        && !snapshotsAfterRecording.front().recordingEnabled && secondSnapshotAfterRecording->recordingEnabled,
                    "history channel selector projects and dispatches commands for the selected second preview channel");

    if (historyAction != nullptr)
        historyAction->trigger();
    QCoreApplication::processEvents();
    passed &= check(bridge.historyWindowForInstanceId(QStringLiteral("meter-preview-1")) == firstHistoryWindow
                        && tabs != nullptr && tabs->currentWidget() == firstHistoryWindow
                        && firstHistoryWindow->instanceId() == QStringLiteral("meter-preview-1"),
                    "reopening history reuses the shared page and restores the selected card channel");

    provider.setScenario(MockMeterScenario::customChannels);
    bridge.refresh();
    QCoreApplication::processEvents();
    auto* settings = historyWindow->settingsWidget();
    auto* channelCount = historyWindow->findChild<QSpinBox*>(QStringLiteral("meterSettings.channelCount"));
    auto* threshold = historyWindow->findChild<QDoubleSpinBox*>(QStringLiteral("meterSettings.silenceThreshold"));
    passed &= check(settings != nullptr && settings->channelLayoutEditable() && channelCount != nullptr && threshold != nullptr,
                    "history settings enables the custom-channel editor only for custom layouts");
    if (channelCount != nullptr)
        channelCount->setValue(4);
    QCoreApplication::processEvents();
    passed &= check(provider.snapshots().front().channels.size() == 4,
                    "custom channel count updates through the settings command interface");

    auto* channelName = historyWindow->findChild<QLineEdit*>(QStringLiteral("meterSettings.channelName.1"));
    if (channelName != nullptr) {
        channelName->setText(QStringLiteral("Music R"));
        QMetaObject::invokeMethod(channelName, "editingFinished");
    }
    if (threshold != nullptr)
        threshold->setValue(-64.0);
    QCoreApplication::processEvents();
    passed &= check(channelName != nullptr && provider.snapshots().front().channels[1].displayName == QStringLiteral("Music R")
                        && provider.snapshots().front().globalSettings.silenceThresholdDbfs == -64.0F,
                    "custom channel and global settings remain in the deterministic preview state");

    bridge.resize(1200, 800);
    bridge.show();
    QCoreApplication::processEvents();
    QImage image(historyWindow->size() * 2, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(2.0);
    image.fill(Qt::transparent);
    historyWindow->render(&image);
    passed &= check(imageHasVisiblePixels(image), "history window renders a non-empty high-DPI UI surface");

    provider.setPreviewProbeCount(0);
    bridge.refresh();
    QCoreApplication::processEvents();
    passed &= check(bridge.historyWindowForInstanceId(QStringLiteral("meter-preview-1")) == nullptr
                        && tabs != nullptr && !tabs->isTabEnabled(1) && tabs->currentIndex() == 0,
                    "removing all cards disables the shared history tab");
    return passed;
}

}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    if (!testHistoryWindowWorkflow())
        return 1;
    std::puts("Meterbridge UI-4 tests passed");
    return 0;
}