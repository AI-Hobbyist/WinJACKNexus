/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>

#include <QColor>
#include <QString>

namespace WinJACKNexus::UI
{

inline QColor selThemeAccent(const QString &kind)
{
    const QColor fallback = kind == QStringLiteral("master")    ? QColor("#ff5353")
                            : kind == QStringLiteral("submix")  ? QColor("#37d879")
                            : kind == QStringLiteral("monitor") ? QColor("#ffe15a")
                            : kind == QStringLiteral("aux")     ? QColor("#33b6ff")
                                                                : QColor("#9aa8b7");
    const QColor fader = Controls::themeTokenColor(
        QStringLiteral("mixer"), QStringLiteral("fader-%1-thumb").arg(kind), fallback);
    return Controls::themeTokenColor(QStringLiteral("sel-main"),
                                     QStringLiteral("control-accent-%1").arg(kind), fader);
}

inline bool selThemeRoleUsesDefault(const QString &scope, const QString &role)
{
    const auto *core = ThemeManager::installedThemeCore();
    if (!core)
        return true;
    const QString name = QStringLiteral("--wjn-theme-") + scope + '-' + role;
    static const ThemeRegistry registry = [] {
        ThemeRegistry result;
        ThemeRegistry::builtIn(&result);
        return result;
    }();
    if (!registry.contains(name))
        return false;
    const QString declared = core->declaredValue(name).trimmed();
    if (!declared.isEmpty() && declared != registry.token(name).defaultValue.trimmed())
        return false;
    static const ThemeCore builtInCore;
    const auto current = core->snapshot();
    const auto builtIn = builtInCore.snapshot();
    return current && builtIn && current->value(name) == builtIn->value(name);
}

inline QColor selThemedAccent(const QString &scope, const QString &role, const QColor &accent,
                              const QColor &fallback)
{
    const QColor themed = Controls::themeTokenColor(scope, role, fallback);
    return selThemeRoleUsesDefault(scope, role) ? accent : themed;
}

inline QColor selThemedAccentSurface(const QString &scope, const QString &role,
                                     const QColor &accent, const QColor &fallback, int defaultAlpha)
{
    if (!selThemeRoleUsesDefault(scope, role))
        return Controls::themeTokenColor(scope, role, fallback);
    QColor result = accent;
    result.setAlpha(defaultAlpha);
    return result;
}

}
