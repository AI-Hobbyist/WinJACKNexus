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
#include <optional>
#include <vector>

class QAction;
class QCheckBox;
class QComboBox;
class QHideEvent;
class QLabel;
class QSlider;
class QTableWidget;
class QTimer;
class QShowEvent;

namespace WinJACKNexus::UI {
class HistoryChartWidget;
class HistoryLegendWidget;
class MetricStatisticsWidget;
}

namespace WinJACKNexus::Meter::Mod {

class MeterSettingsWidget;

class MeterHistoryWindow final : public QWidget {
public:
    explicit MeterHistoryWindow(MeterViewDataProvider& provider,
                                std::function<void()> requestRefresh,
                                std::function<void()> returnToBridge,
                                QWidget* parent = nullptr);
    ~MeterHistoryWindow() override = default;

    void setSnapshots(const std::vector<MeterViewSnapshot>& snapshots, const QString& preferredInstanceId = {});
    void setSnapshot(const MeterViewSnapshot& snapshot);
    QString instanceId() const;
    WinJACKNexus::UI::HistoryChartWidget* chart() const noexcept;
    WinJACKNexus::UI::HistoryLegendWidget* legend() const noexcept;
    WinJACKNexus::UI::MetricStatisticsWidget* statistics() const noexcept;
    MeterSettingsWidget* settingsWidget() const noexcept;
    int markerCount() const noexcept;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void dispatch(MeterViewCommand command);
    void updateChart();
    void updateLegend();
    void updateStatistics(std::int64_t startMilliseconds, std::int64_t endMilliseconds);
    void updateMarkerTable();
    void updateCsvPreview();
    void updateReferenceLines();
    void jumpToSelectedMarker();
    void removeSelectedMarker();
    std::optional<std::int64_t> selectedMarkerElapsedMilliseconds() const;

    MeterViewDataProvider& provider_;
    std::function<void()> requestRefresh_;
    std::function<void()> returnToBridge_;
    std::vector<MeterViewSnapshot> availableSnapshots_;
    MeterViewSnapshot snapshot_;
    std::optional<std::int64_t> focusElapsedMilliseconds_;
    std::optional<std::int64_t> scanCursorMilliseconds_;
    QHash<QString, bool> referenceLineVisibility_;
    std::array<QCheckBox*, 8> seriesVisibilityCheckBoxes_ {};
    std::array<QCheckBox*, 4> referenceLineCheckBoxes_ {};
    QComboBox* channelSelector_ = nullptr;
    QComboBox* viewModeSelector_ = nullptr;
    QComboBox* timeWindowSelector_ = nullptr;
    QSlider* scanCursorSlider_ = nullptr;
    QTimer* scanTimer_ = nullptr;
    QAction* pauseAction_ = nullptr;
    QAction* clearAction_ = nullptr;
    QAction* addMarkerAction_ = nullptr;
    QAction* removeMarkerAction_ = nullptr;
    QAction* jumpMarkerAction_ = nullptr;
    QAction* recordingAction_ = nullptr;
    QAction* exportAction_ = nullptr;
    WinJACKNexus::UI::HistoryChartWidget* chart_ = nullptr;
    WinJACKNexus::UI::HistoryLegendWidget* legend_ = nullptr;
    WinJACKNexus::UI::MetricStatisticsWidget* statistics_ = nullptr;
    MeterSettingsWidget* settingsWidget_ = nullptr;
    QTableWidget* markerTable_ = nullptr;
    QCheckBox* recordingCheckBox_ = nullptr;
    QLabel* csvPathLabel_ = nullptr;
    QLabel* csvStatusLabel_ = nullptr;
    QLabel* exportStatusLabel_ = nullptr;
};

}