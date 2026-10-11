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
#include <WinJACKNexus/Meter/Mod/MeterbridgeService.hpp>

#include <WinJACKNexus/Meter/LiveMeterDataProvider.hpp>
#include <WinJACKNexus/Meter/Mod/MeterbridgeWindow.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>

#include <QJsonArray>
#include <QWidget>

namespace WinJACKNexus::Meter::Mod
{

MeterbridgeService::~MeterbridgeService()
{
    shutdown();
}

bool MeterbridgeService::initialize()
{
    if (provider_ != nullptr)
        return true;
    try
    {
        provider_ = new LiveMeterDataProvider;
        return true;
    }
    catch (...)
    {
        provider_ = nullptr;
        return false;
    }
}

void MeterbridgeService::shutdown() noexcept
{
    destroyWindow();
    delete provider_;
    provider_ = nullptr;
}

void MeterbridgeService::destroyWindow() noexcept
{
    delete window_;
    window_ = nullptr;
}

QWidget *MeterbridgeService::showWindow(const QString &instanceId)
{
    if (!initialize())
        return nullptr;
    if (window_ == nullptr)
    {
        window_ = new MeterbridgeWindow(*provider_);
        window_->setAttribute(Qt::WA_DeleteOnClose, false);
        WinJACKNexus::UI::ThemeManager().applyToWidget(window_);
        if (!pendingWindowState_.isEmpty())
            window_->restoreProjectState(pendingWindowState_);
    }
    window_->showNormal();
    window_->raise();
    window_->activateWindow();
    if (!instanceId.isEmpty())
        window_->selectInstance(instanceId);
    return window_;
}

void MeterbridgeService::reloadGlobalSettings()
{
    if (provider_ != nullptr)
        provider_->reloadGlobalSettings();
}

QJsonObject MeterbridgeService::projectState() const
{
    QJsonObject result;
    if (provider_ != nullptr)
        result.insert(QStringLiteral("meters"), provider_->projectMeters());
    const auto windowState = window_ == nullptr ? pendingWindowState_ : window_->projectState();
    if (!windowState.isEmpty())
        result.insert(QStringLiteral("meterbridgeWindow"), windowState);
    return result;
}

bool MeterbridgeService::restoreProjectState(const QJsonObject &state)
{
    if (!initialize())
        return false;
    const auto meters = state.value(QStringLiteral("meters"));
    if (meters.isArray())
        provider_->restoreProjectMeters(meters.toArray());
    const auto windowState = state.value(QStringLiteral("meterbridgeWindow"));
    pendingWindowState_ = windowState.isObject() ? windowState.toObject() : QJsonObject{};
    if (window_ != nullptr && !pendingWindowState_.isEmpty())
        window_->restoreProjectState(pendingWindowState_);
    return true;
}

}