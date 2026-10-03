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
#include <WinJACKNexus/UI/ApplicationSettings.hpp>

#include <QSettings>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace WinJACKNexus::UI {

namespace {

constexpr int defaultFrameRate = 60;

int normalizedFrameRate(int framesPerSecond) noexcept
{
    switch (framesPerSecond) {
    case 30:
    case 60:
    case 120:
        return framesPerSecond;
    default:
        return defaultFrameRate;
    }
}

}

ApplicationSettings& ApplicationSettings::instance()
{
    static auto* settings = new ApplicationSettings;
    return *settings;
}

ApplicationSettings::ApplicationSettings()
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("WinJACKNexus"), QStringLiteral("Application"));
    frameRate_ = normalizedFrameRate(settings.value(QStringLiteral("frameRate"), defaultFrameRate).toInt());
    peakValueRefreshMilliseconds_ = std::clamp(
        settings.value(QStringLiteral("peakValueRefreshMilliseconds"), 50).toInt(), 8, 1000);
    const auto hold = settings.value(QStringLiteral("peakHoldSeconds"), 1.0).toDouble();
    peakHoldSeconds_ = std::isfinite(hold) ? std::clamp(hold, 0.0, 10.0) : 1.0;
}

int ApplicationSettings::frameRate() const noexcept
{
    return frameRate_;
}

void ApplicationSettings::setFrameRate(int framesPerSecond)
{
    const auto normalizedRate = normalizedFrameRate(framesPerSecond);
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("WinJACKNexus"), QStringLiteral("Application"));
    settings.setValue(QStringLiteral("frameRate"), normalizedRate);
    if (frameRate_ == normalizedRate)
        return;
    frameRate_ = normalizedRate;
    Q_EMIT frameRateChanged(frameRate_);
}

int ApplicationSettings::peakValueRefreshMilliseconds() const noexcept
{
    return peakValueRefreshMilliseconds_;
}

void ApplicationSettings::setPeakValueRefreshMilliseconds(int milliseconds)
{
    const auto normalized = std::clamp(milliseconds, 8, 1000);
    if (peakValueRefreshMilliseconds_ == normalized) return;
    peakValueRefreshMilliseconds_ = normalized;
    QSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("WinJACKNexus"), QStringLiteral("Application"))
        .setValue(QStringLiteral("peakValueRefreshMilliseconds"), normalized);
    Q_EMIT peakValueRefreshMillisecondsChanged(normalized);
}

double ApplicationSettings::peakHoldSeconds() const noexcept
{
    return peakHoldSeconds_;
}

void ApplicationSettings::setPeakHoldSeconds(double seconds)
{
    const auto normalized = std::isfinite(seconds) ? std::clamp(seconds, 0.0, 10.0) : 1.0;
    if (peakHoldSeconds_ == normalized) return;
    peakHoldSeconds_ = normalized;
    QSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("WinJACKNexus"), QStringLiteral("Application"))
        .setValue(QStringLiteral("peakHoldSeconds"), normalized);
    Q_EMIT peakHoldSecondsChanged(normalized);
}

void bindRefreshTimer(QTimer& timer, QObject* owner)
{
    auto& settings = ApplicationSettings::instance();
    timer.setTimerType(Qt::PreciseTimer);
    timer.setInterval(1000 / settings.frameRate());
    QObject::connect(&settings, &ApplicationSettings::frameRateChanged, owner,
                     [&timer](int framesPerSecond) {
                         timer.setInterval(1000 / framesPerSecond);
                     });
}

}
