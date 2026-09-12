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
    QSettings settings(QStringLiteral("WinJACKNexus"), QStringLiteral("Application"));
    frameRate_ = normalizedFrameRate(settings.value(QStringLiteral("frameRate"), defaultFrameRate).toInt());
}

int ApplicationSettings::frameRate() const noexcept
{
    return frameRate_;
}

void ApplicationSettings::setFrameRate(int framesPerSecond)
{
    const auto normalizedRate = normalizedFrameRate(framesPerSecond);
    QSettings settings(QStringLiteral("WinJACKNexus"), QStringLiteral("Application"));
    settings.setValue(QStringLiteral("frameRate"), normalizedRate);
    if (frameRate_ == normalizedRate)
        return;
    frameRate_ = normalizedRate;
    Q_EMIT frameRateChanged(frameRate_);
}

}