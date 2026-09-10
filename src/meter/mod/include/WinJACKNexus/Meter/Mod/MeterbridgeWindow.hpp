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
class QScrollArea;
class QSpinBox;
class QHBoxLayout;

namespace WinJACKNexus::Meter::Mod {

class MeterCard;

class MeterbridgeWindow final : public QMainWindow {
public:
    explicit MeterbridgeWindow(MeterViewDataProvider& provider, QWidget* parent = nullptr);
    ~MeterbridgeWindow() override = default;

    void refresh();
    int cardCount() const noexcept;
    MeterCard* cardForInstanceId(const QString& instanceId) const;
    bool isEmptyStateVisible() const noexcept;

private:
    void addCard(const MeterViewSnapshot& snapshot);
    void removeCard(const QString& instanceId);
    void updateCardContainerGeometry();

    MeterViewDataProvider& provider_;
    QComboBox* scenarioSelector_ = nullptr;
    QSpinBox* customChannelCount_ = nullptr;
    QSpinBox* previewProbeCount_ = nullptr;
    QScrollArea* cardScrollArea_ = nullptr;
    QWidget* cardContainer_ = nullptr;
    QHBoxLayout* cardLayout_ = nullptr;
    QLabel* emptyState_ = nullptr;
    QHash<QString, MeterCard*> cards_;
};

}