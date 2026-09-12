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

#include <QJsonObject>
#include <QString>

class QWidget;

namespace WinJACKNexus::Meter {
class LiveMeterDataProvider;
}

namespace WinJACKNexus::Meter::Mod {

class MeterbridgeWindow;

class MeterbridgeService final {
public:
    MeterbridgeService() = default;
    ~MeterbridgeService();

    MeterbridgeService(const MeterbridgeService&) = delete;
    MeterbridgeService& operator=(const MeterbridgeService&) = delete;

    bool initialize();
    void shutdown() noexcept;
    void destroyWindow() noexcept;
    QWidget* showWindow(const QString& instanceId = {});
    void reloadGlobalSettings();
    QJsonObject projectState() const;
    bool restoreProjectState(const QJsonObject& state);

private:
    WinJACKNexus::Meter::LiveMeterDataProvider* provider_ = nullptr;
    MeterbridgeWindow* window_ = nullptr;
    QJsonObject pendingWindowState_;
};

}