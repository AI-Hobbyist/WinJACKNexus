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

class QDoubleSpinBox;
class QGroupBox;
class QLineEdit;
class QSpinBox;
class QTableWidget;

namespace WinJACKNexus::Meter::Mod {

class MeterSettingsWidget final : public QFrame {
public:
    explicit MeterSettingsWidget(MeterViewDataProvider& provider,
                                 std::function<void()> requestRefresh,
                                 QWidget* parent = nullptr);
    ~MeterSettingsWidget() override = default;

    void setSnapshot(const MeterViewSnapshot& snapshot);
    QString instanceId() const;
    bool channelLayoutEditable() const noexcept;

private:
    void dispatch(MeterViewCommand command);
    void rebuildChannelTable(const MeterViewSnapshot& snapshot);

    MeterViewDataProvider& provider_;
    std::function<void()> requestRefresh_;
    QString instanceId_;
    bool channelLayoutEditable_ = false;
    QDoubleSpinBox* silenceThresholdSpinBox_ = nullptr;
    QDoubleSpinBox* silenceDurationSpinBox_ = nullptr;
    QLineEdit* logRootDirectoryEdit_ = nullptr;
    QGroupBox* customChannelsGroup_ = nullptr;
    QSpinBox* customChannelCountSpinBox_ = nullptr;
    QTableWidget* channelTable_ = nullptr;
};

}