/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <WinJACKNexus/UI/Controls.hpp>

#include <QColor>
#include <QString>

namespace WinJACKNexus::UI {

inline QColor selThemeAccent(const QString& kind)
{
    const QColor fallback = kind == QStringLiteral("master") ? QColor("#ff5353")
        : kind == QStringLiteral("submix") ? QColor("#37d879")
        : kind == QStringLiteral("monitor") ? QColor("#ffe15a")
        : kind == QStringLiteral("aux") ? QColor("#33b6ff") : QColor("#9aa8b7");
    const QColor fader = Controls::themeTokenColor(QStringLiteral("mixer"),
        QStringLiteral("fader-%1-thumb").arg(kind), fallback);
    return Controls::themeTokenColor(QStringLiteral("sel-main"),
        QStringLiteral("control-accent-%1").arg(kind), fader);
}

}
