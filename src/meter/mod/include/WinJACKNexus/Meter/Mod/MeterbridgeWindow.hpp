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
#include <QMainWindow>

class QComboBox;
class QLabel;
class QResizeEvent;
class QScrollArea;
class QShowEvent;
class QSpinBox;
class QTabWidget;
class QHBoxLayout;

namespace WinJACKNexus::Meter::Mod {

class MeterCard;
class MeterHistoryWindow;

class MeterbridgeWindow final : public QMainWindow {
public:
    explicit MeterbridgeWindow(MeterViewDataProvider& provider, QWidget* parent = nullptr);
    ~MeterbridgeWindow() override = default;

    void refresh();
    int cardCount() const noexcept;
    MeterCard* cardForInstanceId(const QString& instanceId) const;
    MeterHistoryWindow* historyWindowForInstanceId(const QString& instanceId) const;
    void openHistoryForInstanceId(const QString& instanceId);
    bool isEmptyStateVisible() const noexcept;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void addCard(const MeterViewSnapshot& snapshot);
    void removeCard(const QString& instanceId);
    void updateCardContainerGeometry();

    MeterViewDataProvider& provider_;
    QComboBox* scenarioSelector_ = nullptr;
    QSpinBox* customChannelCount_ = nullptr;
    QSpinBox* previewProbeCount_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QScrollArea* cardScrollArea_ = nullptr;
    QWidget* cardContainer_ = nullptr;
    QHBoxLayout* cardLayout_ = nullptr;
    QLabel* emptyState_ = nullptr;
    QHash<QString, MeterCard*> cards_;
    MeterHistoryWindow* historyPage_ = nullptr;
    int historyTabIndex_ = -1;
};

}