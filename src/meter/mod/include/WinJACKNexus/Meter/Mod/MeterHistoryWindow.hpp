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
#pragma once

#include <WinJACKNexus/Meter/MeterViewDataProvider.hpp>

#include <QHash>
#include <QWidget>

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <vector>

class QAction;
class QCheckBox;
class QComboBox;
class QHideEvent;
class QLabel;
class QSlider;
class QSpinBox;
class QTimer;
class QShowEvent;

namespace WinJACKNexus::UI
{
class HistoryChartWidget;
class HistoryLegendWidget;
class MetricStatisticsWidget;
}

namespace WinJACKNexus::Meter::Mod
{

class MeterSettingsWidget;

class MeterHistoryWindow final : public QWidget
{
  public:
    explicit MeterHistoryWindow(MeterViewDataProvider &provider,
                                std::function<void()> requestRefresh,
                                std::function<void()> returnToBridge, QWidget *parent = nullptr);
    ~MeterHistoryWindow() override = default;

    int refreshRate() const noexcept;
    int refreshIntervalMilliseconds() const noexcept;
    void setRefreshRate(int framesPerSecond);
    void setSnapshots(const std::vector<MeterViewSnapshot> &snapshots,
                      const QString &preferredInstanceId = {});
    void setLiveSnapshots(const std::vector<MeterViewSnapshot> &snapshots);
    void setSnapshot(const MeterViewSnapshot &snapshot);
    QString instanceId() const;
    WinJACKNexus::UI::HistoryChartWidget *chart() const noexcept;
    WinJACKNexus::UI::HistoryLegendWidget *legend() const noexcept;
    WinJACKNexus::UI::MetricStatisticsWidget *statistics() const noexcept;
    MeterSettingsWidget *settingsWidget() const noexcept;

  protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

  private:
    void applyTheme();
    void refreshLocalizedDisplay();
    void refreshExportStatus();
    void applyRefreshRate(int framesPerSecond);
    struct ScanOverwritePoint final
    {
        std::int64_t timestampMilliseconds = 0;
        std::optional<double> value;
        bool discontinuity = false;
    };

    void dispatch(MeterViewCommand command);
    void updateChart();
    void updateTimeline(std::int64_t latestElapsedMilliseconds);
    void refreshHistoryData();
    void updateLegend();
    void updateStatistics(std::int64_t startMilliseconds, std::int64_t endMilliseconds);
    void updateCsvPreview();
    void updateReferenceLines();

    MeterViewDataProvider &provider_;
    std::function<void()> requestRefresh_;
    std::function<void()> returnToBridge_;
    MeterViewSnapshot snapshot_;
    std::optional<std::int64_t> scanCursorMilliseconds_;
    std::optional<std::int64_t> pausedHistoryElapsedMilliseconds_;
    std::array<std::map<std::int64_t, ScanOverwritePoint>, 8> scanOverwritePoints_;
    QString scanOverwriteInstanceId_;
    std::int64_t scanOverwriteTimeWindowMilliseconds_ = 0;
    std::int64_t scanOverwriteLatestElapsedMilliseconds_ = -1;
    std::int64_t renderedHistoryElapsedMilliseconds_ = -1;
    std::int64_t lastHistoryDataRefreshMilliseconds_ = -1;
    QHash<QString, bool> referenceLineVisibility_;
    quint64 presetRevision_ = 0;
    std::array<QCheckBox *, 8> seriesVisibilityCheckBoxes_{};
    std::array<QCheckBox *, 2> referenceLineCheckBoxes_{};
    QComboBox *channelSelector_ = nullptr;
    QComboBox *viewModeSelector_ = nullptr;
    QSpinBox *timeWindowSpinBox_ = nullptr;
    QSlider *scanCursorSlider_ = nullptr;
    QTimer *scanTimer_ = nullptr;
    QAction *pauseAction_ = nullptr;
    QAction *clearAction_ = nullptr;
    QAction *recordingAction_ = nullptr;
    QAction *exportAction_ = nullptr;
    WinJACKNexus::UI::HistoryChartWidget *chart_ = nullptr;
    WinJACKNexus::UI::HistoryLegendWidget *legend_ = nullptr;
    WinJACKNexus::UI::MetricStatisticsWidget *statistics_ = nullptr;
    MeterSettingsWidget *settingsWidget_ = nullptr;
    QCheckBox *recordingCheckBox_ = nullptr;
    QLabel *csvPathLabel_ = nullptr;
    QLabel *csvStatusLabel_ = nullptr;
    QLabel *exportStatusLabel_ = nullptr;
    int refreshRate_ = 60;
    int exportStatus_ = 0;
    QString exportDetail_;
};

}
