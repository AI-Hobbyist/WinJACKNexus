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

#include <WinJACKNexus/UI/Export.hpp>

#include <QObject>

class QTimer;

namespace WinJACKNexus::UI {

class WINJACK_UI_API ApplicationSettings final : public QObject {
    Q_OBJECT

public:
    static ApplicationSettings& instance();

    int frameRate() const noexcept;
    void setFrameRate(int framesPerSecond);
    int peakValueRefreshMilliseconds() const noexcept;
    void setPeakValueRefreshMilliseconds(int milliseconds);
    double peakHoldSeconds() const noexcept;
    void setPeakHoldSeconds(double seconds);
    quint32 processPriorityClass() const noexcept;
    bool setProcessPriorityClass(quint32 priorityClass, QString* error = nullptr);

Q_SIGNALS:
    void frameRateChanged(int framesPerSecond);
    void peakValueRefreshMillisecondsChanged(int milliseconds);
    void peakHoldSecondsChanged(double seconds);
    void processPriorityClassChanged(quint32 priorityClass);

private:
    ApplicationSettings();

    int frameRate_ = 60;
    int peakValueRefreshMilliseconds_ = 50;
    double peakHoldSeconds_ = 1.0;
    quint32 processPriorityClass_ = 0x8000; // ABOVE_NORMAL_PRIORITY_CLASS
};

// Keep continuously refreshed views in step with the application FPS setting.
WINJACK_UI_API void bindRefreshTimer(QTimer& timer, QObject* owner);

}
