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
#include <WinJACKNexus/UI/Controls.hpp>
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
#include <QColor>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QSlider>
#include <QSplitter>
#include <QSpinBox>
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
                        && chart->series().size() == 8 && chart->minimumValue() == -72.0 && chart->maximumValue() == 12.0
                        && chart->rightMinimumValue() == 0.0 && chart->rightMaximumValue() == 84.0
                        && chart->series().back().axis == HistoryChartAxis::right
                        && chart->horizontalAxisLabel() == QStringLiteral("系统时间")
                        && chart->timeDisplayOffsetMilliseconds().has_value()
                        && chart->endTimeMilliseconds() - chart->startTimeMilliseconds() == 30000
                        && legend->isVisible() && legend->entries().size() == 8
                        && statistics->entries().size() == 7,
                    "history window projects its eight curves and seven populated default statistics with a 30-second window and LRA right axis");
    const auto lfeSeries = chart != nullptr
        ? std::find_if(chart->series().cbegin(), chart->series().cend(), [](const auto& series) {
              return series.id == QStringLiteral("lfe");
          })
        : std::vector<WinJACKNexus::UI::HistoryChartSeries>::const_iterator {};
    const auto lraSeries = chart != nullptr
        ? std::find_if(chart->series().cbegin(), chart->series().cend(), [](const auto& series) {
              return series.id == QStringLiteral("lra");
          })
        : std::vector<WinJACKNexus::UI::HistoryChartSeries>::const_iterator {};
    passed &= check(chart != nullptr && lfeSeries != chart->series().cend() && lraSeries != chart->series().cend()
                        && lfeSeries->color == QColor(QStringLiteral("#ff78c7"))
                        && lraSeries->color == QColor(QStringLiteral("#b96aff"))
                        && lfeSeries->color != lraSeries->color,
                    "history chart uses distinct LFE and LRA colors");

    auto* peakSeriesToggle = historyWindow->findChild<QCheckBox*>(QStringLiteral("meterHistory.series.peak"));
    auto* peakSeriesColor = historyWindow->findChild<QWidget*>(QStringLiteral("meterHistory.seriesColor.peak"));
    auto* integratedReferenceToggle = historyWindow->findChild<QCheckBox*>(QStringLiteral("meterHistory.reference.integrated"));
    auto* truePeakReferenceToggle = historyWindow->findChild<QCheckBox*>(QStringLiteral("meterHistory.reference.dbtp"));
    auto* recordingToggle = historyWindow->findChild<QCheckBox*>(QStringLiteral("meterHistory.recordingToggle"));
    auto* channelSelector = historyWindow->findChild<QComboBox*>(QStringLiteral("meterHistory.channelSelector"));
    auto* timeWindow = historyWindow->findChild<QSpinBox*>(QStringLiteral("meterHistory.timeWindow"));
    auto* chartSettingsButton = historyWindow->findChild<QToolButton*>(QStringLiteral("meterHistory.chartSettingsButton"));
    auto* splitter = historyWindow->findChild<QSplitter*>(QStringLiteral("meterHistory.splitter"));
    const auto secondChannelIndex = channelSelector != nullptr
        ? channelSelector->findData(QStringLiteral("meter-preview-2"))
        : -1;
    passed &= check(peakSeriesColor != nullptr && peakSeriesColor->isVisible()
                        && channelSelector != nullptr && channelSelector->width() >= 270,
                    "series colors remain visible and the channel selector has room for its label");
    passed &= check(peakSeriesToggle != nullptr && peakSeriesToggle->text() == QStringLiteral("峰值（Peak）")
                        && integratedReferenceToggle != nullptr && integratedReferenceToggle->text() == QStringLiteral("综合响度（Integrated） -23.0 LUFS")
                        && truePeakReferenceToggle != nullptr && truePeakReferenceToggle->text() == QStringLiteral("最大真峰值（dBTP） -1.0 dBTP")
                        && historyWindow->findChild<QCheckBox*>(QStringLiteral("meterHistory.reference.momentary")) == nullptr
                        && historyWindow->findChild<QCheckBox*>(QStringLiteral("meterHistory.reference.short-term")) == nullptr
                        && historyWindow->findChild<QCheckBox*>(QStringLiteral("meterHistory.reference.lra")) == nullptr
                        && historyWindow->findChild<QComboBox*>(QStringLiteral("meterSettings.globalPreset")) == nullptr
                        && recordingToggle != nullptr && recordingToggle->text() == QStringLiteral("启用实时记录到 CSV")
                        && channelSelector != nullptr && channelSelector->count() == 2 && secondChannelIndex >= 0
                        && timeWindow != nullptr && timeWindow->minimum() == 10 && timeWindow->maximum() == 3600
                        && timeWindow->value() == 30 && chartSettingsButton != nullptr && splitter != nullptr
                        && chartSettingsButton->text() == QStringLiteral("图表设置"),
                    "history workbench limits reference controls to integrated loudness and dBTP");
    if (splitter != nullptr)
        splitter->setSizes({ 1, 1000 });
    if (chartSettingsButton != nullptr)
        chartSettingsButton->click();
    QCoreApplication::processEvents();
    passed &= check(splitter != nullptr && splitter->sizes().front() >= 250,
                    "chart settings action restores access to the sidebar");
    if (peakSeriesToggle != nullptr)
        peakSeriesToggle->click();
    if (integratedReferenceToggle != nullptr)
        integratedReferenceToggle->click();
    QCoreApplication::processEvents();
    const auto snapshotWithHiddenPeak = provider.snapshots().front();
    const auto hiddenPeak = std::find(snapshotWithHiddenPeak.historyView.visibleSeriesIds.cbegin(),
                                      snapshotWithHiddenPeak.historyView.visibleSeriesIds.cend(),
                                      QStringLiteral("peak"));
    const auto hiddenIntegratedReference = std::find_if(chart->referenceLines().cbegin(), chart->referenceLines().cend(),
                                                         [](const auto& line) {
                                                             return line.label == QStringLiteral("综合响度（Integrated） -23.0 LUFS") && !line.visible;
                                                         });
    passed &= check(hiddenPeak == snapshotWithHiddenPeak.historyView.visibleSeriesIds.cend()
                        && !chart->series().front().visible
                        && hiddenIntegratedReference != chart->referenceLines().cend(),
                    "sidebar curve and reference controls update the chart deterministically");

    if (timeWindow != nullptr)
        timeWindow->setValue(3600);
    QCoreApplication::processEvents();
    passed &= check(timeWindow != nullptr && timeWindow->value() == 3600
                        && provider.snapshots().front().historyView.timeWindowMilliseconds == 3600000
                        && chart->endTimeMilliseconds() - chart->startTimeMilliseconds() == 3600000,
                    "time-window input accepts the one-hour upper limit through the preview provider");

    auto* viewModeSelector = historyWindow->findChild<QComboBox*>(QStringLiteral("meterHistory.viewMode"));
    auto* scanCursor = historyWindow->findChild<QSlider*>(QStringLiteral("meterHistory.scanCursor"));
    passed &= check(qobject_cast<WinJACKNexus::UI::Controls::ToggleField*>(peakSeriesToggle) != nullptr
                        && qobject_cast<WinJACKNexus::UI::Controls::ToggleField*>(recordingToggle) != nullptr
                        && qobject_cast<WinJACKNexus::UI::Controls::ChoiceField*>(viewModeSelector) != nullptr
                        && qobject_cast<WinJACKNexus::UI::Controls::ChoiceField*>(channelSelector) != nullptr
                        && qobject_cast<WinJACKNexus::UI::Controls::RangeSlider*>(scanCursor) != nullptr
                        && qobject_cast<WinJACKNexus::UI::Controls::IconActionButton*>(chartSettingsButton) != nullptr,
                    "history controls use the shared painted control library");
    auto* refreshRate = historyWindow->findChild<QSpinBox*>(QStringLiteral("meterHistory.refreshRate"));
    passed &= check(refreshRate == nullptr,
                    "history display omits a local refresh-rate control");
    if (viewModeSelector != nullptr)
        viewModeSelector->setCurrentIndex(1);
    QCoreApplication::processEvents();
    passed &= check(viewModeSelector != nullptr && scanCursor != nullptr
                        && provider.snapshots().front().historyView.mode == MeterHistoryViewMode::scanning
                        && scanCursor->isVisible() && !scanCursor->isEnabled() && chart->timeCursorMilliseconds().has_value(),
                    "scan mode switches through the provider contract and exposes a cursor");

    const auto scanCursorBefore = chart->timeCursorMilliseconds();
    auto liveSnapshots = provider.snapshots();
    const auto liveSnapshot = std::find_if(liveSnapshots.begin(), liveSnapshots.end(), [](const auto& snapshot) {
        return snapshot.instanceId == QStringLiteral("meter-preview-1");
    });
    if (liveSnapshot != liveSnapshots.end()) {
        liveSnapshot->history.clear();
        liveSnapshot->latestHistoryElapsedMilliseconds = scanCursorBefore.value_or(0) + 16;
    }
    historyWindow->setLiveSnapshots(liveSnapshots);
    const auto liveCursor = chart->timeCursorMilliseconds();
    passed &= check(scanCursorBefore.has_value() && liveCursor.has_value()
                        && *liveCursor == *scanCursorBefore + 16
                        && scanCursor != nullptr && scanCursor->value() == *liveCursor,
                    "lightweight live snapshots advance the scan cursor without rebuilding chart data");
    auto latestScanTimestamp = -1LL;
    bool hasLatestScanPoint = false;
    if (!chart->series().empty()) {
        const auto& firstScanSeries = chart->series().front();
        const auto latestScanPoint = std::find_if(firstScanSeries.points.crbegin(), firstScanSeries.points.crend(), [](const auto& point) {
            return point.value.has_value();
        });
        hasLatestScanPoint = latestScanPoint != firstScanSeries.points.crend();
        if (hasLatestScanPoint)
            latestScanTimestamp = latestScanPoint->timestampMilliseconds;
    }
    QThread::msleep(140);
    QCoreApplication::processEvents();
    const auto scanCursorAfter = chart->timeCursorMilliseconds();
    passed &= check(scanCursorBefore.has_value() && scanCursorAfter.has_value()
                        && hasLatestScanPoint
                        && *scanCursorBefore == latestScanTimestamp
                        && scanCursorAfter == scanCursorBefore
                        && scanCursor != nullptr && scanCursor->value() == *scanCursorBefore,
                    "scan mode keeps its cursor aligned with the latest plotted sample");

    auto* pauseAction = historyWindow->findChild<QAction*>(QStringLiteral("meterHistory.pauseAction"));
    if (pauseAction != nullptr)
        pauseAction->trigger();
    QCoreApplication::processEvents();
    const auto pausedCursor = chart->timeCursorMilliseconds();
    if (scanCursor != nullptr)
        scanCursor->setValue(scanCursor->maximum());
    QThread::msleep(140);
    QCoreApplication::processEvents();
    passed &= check(pauseAction != nullptr && provider.snapshots().front().historyView.paused
                        && scanCursor != nullptr && !scanCursor->isEnabled()
                        && chart->timeCursorMilliseconds() == pausedCursor,
                    "pausing the history view freezes the read-only scan cursor");

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
    passed &= check(image.save(QCoreApplication::applicationDirPath()
                               + QStringLiteral("/../meter-history-review.png")),
                    "history layout review screenshot is written to the existing build directory");

    auto atscSnapshot = provider.snapshots().front();
    atscSnapshot.presetId = QStringLiteral("atsc-a85");
    historyWindow->setSnapshot(atscSnapshot);
    const auto integratedReference = chart != nullptr
        ? std::find_if(chart->referenceLines().cbegin(), chart->referenceLines().cend(), [](const auto& line) {
              return line.label == QStringLiteral("综合响度（Integrated） -24.0 LUFS");
          })
        : std::vector<WinJACKNexus::UI::HistoryChartReferenceLine>::const_iterator {};
    const auto truePeakReference = chart != nullptr
        ? std::find_if(chart->referenceLines().cbegin(), chart->referenceLines().cend(), [](const auto& line) {
              return line.label == QStringLiteral("最大真峰值（dBTP） -2.0 dBTP");
          })
        : std::vector<WinJACKNexus::UI::HistoryChartReferenceLine>::const_iterator {};
    passed &= check(chart != nullptr && chart->referenceLines().size() == 2
                        && integratedReference != chart->referenceLines().cend() && integratedReference->value == -24.0
                        && truePeakReference != chart->referenceLines().cend() && truePeakReference->value == -2.0
                        && integratedReferenceToggle != nullptr && integratedReferenceToggle->text() == QStringLiteral("综合响度（Integrated） -24.0 LUFS")
                        && truePeakReferenceToggle != nullptr && truePeakReferenceToggle->text() == QStringLiteral("最大真峰值（dBTP） -2.0 dBTP"),
                    "changing the loudness preset updates the integrated-loudness and dBTP reference lines");

    provider.setPreviewProbeCount(0);
    bridge.refresh();
    QCoreApplication::processEvents();
    passed &= check(bridge.historyWindowForInstanceId(QStringLiteral("meter-preview-1")) == nullptr
                        && tabs != nullptr && !tabs->isTabEnabled(1) && tabs->currentIndex() == 0,
                    "removing all cards disables the shared history tab");
    return passed;
}

bool testScanningUsesCurrentBuffer()
{
    constexpr auto timeWindowMilliseconds = std::int64_t { 60000 };
    constexpr auto sampleIntervalMilliseconds = std::int64_t { 100 };

    MockMeterDataProvider provider;
    provider.setScenario(MockMeterScenario::longHistory);
    MeterbridgeWindow bridge(provider);
    bridge.resize(820, 760);
    bridge.show();
    QCoreApplication::processEvents();

    auto* card = bridge.cardForInstanceId(QStringLiteral("meter-preview-1"));
    auto* historyAction = card != nullptr ? card->findChild<QAction*>(QStringLiteral("meterCard.historyAction")) : nullptr;
    if (historyAction != nullptr)
        historyAction->trigger();
    QCoreApplication::processEvents();

    auto* historyWindow = bridge.historyWindowForInstanceId(QStringLiteral("meter-preview-1"));
    if (!check(historyWindow != nullptr, "long-history preview opens a history window"))
        return false;

    auto* timeWindow = historyWindow->findChild<QSpinBox*>(QStringLiteral("meterHistory.timeWindow"));
    auto* viewModeSelector = historyWindow->findChild<QComboBox*>(QStringLiteral("meterHistory.viewMode"));
    if (timeWindow != nullptr)
        timeWindow->setValue(60);
    if (viewModeSelector != nullptr)
        viewModeSelector->setCurrentIndex(1);
    QCoreApplication::processEvents();

    auto* chart = historyWindow->chart();
    const auto expectedPointCount = static_cast<std::size_t>(timeWindowMilliseconds / sampleIntervalMilliseconds);
    const auto latestElapsed = provider.snapshots().front().history.back().elapsedMilliseconds;
    const auto expectedCursor = latestElapsed % timeWindowMilliseconds;
    const auto allSeriesUseCurrentBuffer = chart != nullptr
        && std::all_of(chart->series().cbegin(), chart->series().cend(), [expectedPointCount, expectedCursor](const auto& series) {
               return series.preservePointOrder
                   && series.points.size() == expectedPointCount
                   && std::all_of(series.points.cbegin(), series.points.cend(), [expectedCursor](const auto& point) {
                          return point.value.has_value() && point.timestampMilliseconds >= 0
                              && point.timestampMilliseconds <= expectedCursor;
                      });
           });
    const auto latestChartPoint = chart != nullptr && !chart->series().empty() && !chart->series().front().points.empty()
        ? chart->series().front().points.back().timestampMilliseconds
        : -1;
    const auto snapshot = provider.snapshots().front();
    const auto statistic = std::find_if(historyWindow->statistics()->entries().cbegin(), historyWindow->statistics()->entries().cend(), [](const auto& entry) {
        return entry.id == QStringLiteral("peak");
    });
    const auto expectedPeak = snapshot.history.back().metrics.peakDbfs;
    const auto statisticsUseCurrentBuffer = statistic != historyWindow->statistics()->entries().cend()
        && expectedPeak.has_value() && statistic->currentValue.has_value()
        && std::abs(*statistic->currentValue - static_cast<double>(*expectedPeak)) < 0.001;
    bool passed = check(timeWindow != nullptr && viewModeSelector != nullptr && chart != nullptr
                            && snapshot.historyView.mode == MeterHistoryViewMode::scanning
                            && snapshot.historyView.timeWindowMilliseconds == timeWindowMilliseconds
                            && chart->timeCursorMilliseconds() == expectedCursor
                            && latestChartPoint == expectedCursor
                            && allSeriesUseCurrentBuffer && statisticsUseCurrentBuffer,
                        "scanning projects only the current history window and aligns it with the newest sample");

    auto legacyTimestampSnapshot = snapshot;
    for (auto& point : legacyTimestampSnapshot.history)
        point.systemTimeMilliseconds.reset();
    historyWindow->setSnapshot(legacyTimestampSnapshot);
    passed &= check(chart != nullptr && chart->timeDisplayOffsetMilliseconds().has_value(),
                    "history windows keep X-axis labels on system time for legacy samples without wall-clock timestamps");
    historyWindow->setSnapshot(snapshot);

    auto wrapSnapshot = snapshot;
    for (const auto elapsedMilliseconds : { timeWindowMilliseconds * 10,
                                             timeWindowMilliseconds * 10 + sampleIntervalMilliseconds }) {
        WinJACKNexus::Meter::MeterHistoryPoint point;
        point.elapsedMilliseconds = elapsedMilliseconds;
        point.metrics.peakDbfs = -12.0F;
        wrapSnapshot.history.push_back(std::move(point));
    }
    historyWindow->setSnapshot(wrapSnapshot);
    const auto wrappedPeakSeries = chart != nullptr
        ? std::find_if(chart->series().cbegin(), chart->series().cend(), [](const auto& series) {
              return series.id == QStringLiteral("peak");
          })
        : std::vector<WinJACKNexus::UI::HistoryChartSeries>::const_iterator {};
    const auto wrappedStart = wrappedPeakSeries != chart->series().cend()
        ? std::find_if(wrappedPeakSeries->points.cbegin(), wrappedPeakSeries->points.cend(), [](const auto& point) {
              return point.timestampMilliseconds == 0;
          })
        : std::vector<WinJACKNexus::UI::HistoryChartPoint>::const_iterator {};
    const auto wrappedNext = wrappedPeakSeries != chart->series().cend()
        ? std::find_if(wrappedPeakSeries->points.cbegin(), wrappedPeakSeries->points.cend(), [sampleIntervalMilliseconds](const auto& point) {
              return point.timestampMilliseconds == sampleIntervalMilliseconds;
          })
        : std::vector<WinJACKNexus::UI::HistoryChartPoint>::const_iterator {};
    const auto retainedTail = wrappedPeakSeries != chart->series().cend()
        ? std::find_if(wrappedPeakSeries->points.cbegin(), wrappedPeakSeries->points.cend(), [timeWindowMilliseconds, sampleIntervalMilliseconds](const auto& point) {
              return point.timestampMilliseconds == timeWindowMilliseconds - sampleIntervalMilliseconds;
          })
        : std::vector<WinJACKNexus::UI::HistoryChartPoint>::const_iterator {};
    passed &= check(chart != nullptr && wrappedPeakSeries != chart->series().cend()
                        && chart->timeCursorMilliseconds() == sampleIntervalMilliseconds
                        && wrappedPeakSeries->points.size() == expectedPointCount
                        && std::all_of(wrappedPeakSeries->points.cbegin(), wrappedPeakSeries->points.cend(), [](const auto& point) {
                               return point.value.has_value();
                           })
                        && wrappedStart != wrappedPeakSeries->points.cend() && wrappedStart->value == std::optional<double> { -12.0 }
                        && wrappedNext != wrappedPeakSeries->points.cend() && wrappedNext->value == std::optional<double> { -12.0 }
                        && retainedTail != wrappedPeakSeries->points.cend() && retainedTail->value.has_value(),
                    "a new scan sweep overwrites reached slots while retaining a continuous curve");

    auto sparseSnapshot = wrapSnapshot;
    auto sparsePoint = sparseSnapshot.history.back();
    sparsePoint.elapsedMilliseconds += sampleIntervalMilliseconds * 6;
    sparsePoint.metrics.peakDbfs = -30.0F;
    sparseSnapshot.history.push_back(sparsePoint);
    historyWindow->setSnapshot(sparseSnapshot);
    const auto interpolatedPeakSeries = chart != nullptr
        ? std::find_if(chart->series().cbegin(), chart->series().cend(), [](const auto& series) {
              return series.id == QStringLiteral("peak");
          })
        : std::vector<WinJACKNexus::UI::HistoryChartSeries>::const_iterator {};
    const auto interpolatedPoint = interpolatedPeakSeries != chart->series().cend()
        ? std::find_if(interpolatedPeakSeries->points.cbegin(), interpolatedPeakSeries->points.cend(), [](const auto& point) {
              return point.timestampMilliseconds == 400;
          })
        : std::vector<WinJACKNexus::UI::HistoryChartPoint>::const_iterator {};
    passed &= check(chart != nullptr && interpolatedPeakSeries != chart->series().cend()
                        && chart->timeCursorMilliseconds() == sampleIntervalMilliseconds * 7
                        && interpolatedPoint != interpolatedPeakSeries->points.cend()
                        && interpolatedPoint->value.has_value()
                        && std::abs(*interpolatedPoint->value - (-21.0)) < 0.001,
                    "sparse history samples fill crossed scan slots instead of interleaving retained values");

    auto pausedSnapshot = sparseSnapshot;
    pausedSnapshot.historyView.paused = true;
    historyWindow->setSnapshot(pausedSnapshot);
    auto advancedPoint = pausedSnapshot.history.back();
    advancedPoint.elapsedMilliseconds += sampleIntervalMilliseconds;
    advancedPoint.metrics.peakDbfs = -1.0F;
    auto advancedSnapshot = pausedSnapshot;
    advancedSnapshot.history.push_back(advancedPoint);
    historyWindow->setSnapshot(advancedSnapshot);
    const auto pausedPeakSeries = chart != nullptr
        ? std::find_if(chart->series().cbegin(), chart->series().cend(), [](const auto& series) {
              return series.id == QStringLiteral("peak");
          })
        : std::vector<WinJACKNexus::UI::HistoryChartSeries>::const_iterator {};
    const auto pausedStatistic = std::find_if(historyWindow->statistics()->entries().cbegin(), historyWindow->statistics()->entries().cend(), [](const auto& entry) {
        return entry.id == QStringLiteral("peak");
    });
    passed &= check(chart != nullptr && pausedPeakSeries != chart->series().cend()
                        && chart->timeCursorMilliseconds() == sampleIntervalMilliseconds * 7
                        && pausedPeakSeries->points.size() == expectedPointCount
                        && pausedStatistic != historyWindow->statistics()->entries().cend()
                        && pausedStatistic->currentValue == std::optional<double> { -30.0 },
                    "pausing freezes scan curves and statistics at the same sample as the cursor");

    advancedSnapshot.historyView.paused = false;
    historyWindow->setSnapshot(advancedSnapshot);
    const auto resumedPeakSeries = chart != nullptr
        ? std::find_if(chart->series().cbegin(), chart->series().cend(), [](const auto& series) {
              return series.id == QStringLiteral("peak");
          })
        : std::vector<WinJACKNexus::UI::HistoryChartSeries>::const_iterator {};
    const auto resumedPoint = resumedPeakSeries != chart->series().cend()
        ? std::find_if(resumedPeakSeries->points.cbegin(), resumedPeakSeries->points.cend(), [sampleIntervalMilliseconds](const auto& point) {
              return point.timestampMilliseconds == sampleIntervalMilliseconds * 8;
          })
        : std::vector<WinJACKNexus::UI::HistoryChartPoint>::const_iterator {};
    passed &= check(chart != nullptr && resumedPeakSeries != chart->series().cend()
                        && chart->timeCursorMilliseconds() == sampleIntervalMilliseconds * 8
                        && resumedPeakSeries->points.size() == expectedPointCount
                        && resumedPoint != resumedPeakSeries->points.cend()
                        && resumedPoint->value == std::optional<double> { -1.0 },
                    "resuming advances the scan cursor and curves together to new samples");
    return passed;
}

}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    if (!testHistoryWindowWorkflow())
        return 1;
    if (!testScanningUsesCurrentBuffer())
        return 1;
    std::puts("Meterbridge UI-4 tests passed");
    return 0;
}
