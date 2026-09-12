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

#include <QFrame>

#include <functional>

class QAction;
class QComboBox;
class QLabel;
class QToolButton;

namespace WinJACKNexus::UI {
class MeterLevelBar;
class MetricReadout;
}

namespace WinJACKNexus::Meter::Mod {

class MeterCard final : public QFrame {
public:
    explicit MeterCard(MeterViewDataProvider& provider,
                       std::function<void()> requestRefresh,
                       std::function<void(const QString&)> openHistory,
                       QWidget* parent = nullptr);
    ~MeterCard() override = default;

    void setSnapshot(const MeterViewSnapshot& snapshot);
    void setDisplayIndex(int index);
    void setAvailableHeight(int height);
    void setRefreshRate(int framesPerSecond);
    QString instanceId() const;
    QString clientName() const;
    QString presetId() const;

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void dispatch(MeterViewCommandType type, std::optional<bool> enabled = std::nullopt, const QString& presetId = {});
    void configurePresetSelector();

    MeterViewDataProvider& provider_;
    std::function<void()> requestRefresh_;
    std::function<void(const QString&)> openHistory_;
    QString instanceId_;
    QString clientName_;
    QString presetId_;
    QLabel* numberLabel_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QLabel* layoutLabel_ = nullptr;
    QLabel* stateLabel_ = nullptr;
    QComboBox* presetSelector_ = nullptr;
    WinJACKNexus::UI::MeterLevelBar* levelBar_ = nullptr;
    WinJACKNexus::UI::MetricReadout* peakReadout_ = nullptr;
    WinJACKNexus::UI::MetricReadout* rmsReadout_ = nullptr;
    WinJACKNexus::UI::MetricReadout* truePeakReadout_ = nullptr;
    WinJACKNexus::UI::MetricReadout* momentaryReadout_ = nullptr;
    WinJACKNexus::UI::MetricReadout* shortTermReadout_ = nullptr;
    WinJACKNexus::UI::MetricReadout* integratedReadout_ = nullptr;
    WinJACKNexus::UI::MetricReadout* lraReadout_ = nullptr;
    QAction* resetAction_ = nullptr;
    QAction* recordingAction_ = nullptr;
    QAction* historyAction_ = nullptr;
    QAction* automaticResetAction_ = nullptr;
    QAction* locateAction_ = nullptr;
    QToolButton* resetButton_ = nullptr;
    QToolButton* recordingButton_ = nullptr;
    QToolButton* historyButton_ = nullptr;
};

}