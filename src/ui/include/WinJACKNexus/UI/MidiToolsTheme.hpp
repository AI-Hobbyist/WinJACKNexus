// Apply the registered node scope through existing palette/control APIs.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QTimer>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>

namespace WinJACKNexus::UI
{
inline void bindMidiToolsTheme(QWidget *root, const QString &scope)
{
    const auto apply = [root, scope] {
        Controls::Palette colors;
        const auto color = [&](const char *role, const QColor &fallback) {
            return Controls::themeTokenColor(scope, role,
                                             Controls::themeTokenColor("global", role, fallback));
        };
        colors.background = color("surface-window", root->palette().color(QPalette::Window));
        colors.panel = color("surface-panel", root->palette().color(QPalette::Base));
        colors.text = color("text-primary", root->palette().color(QPalette::WindowText));
        colors.accent = color("accent-primary", root->palette().color(QPalette::Highlight));
        colors.border = color("border-normal", root->palette().color(QPalette::Mid));
        Controls::applyPalette(root, colors);
        root->setAutoFillBackground(true);
        for (auto *widget : root->findChildren<QWidget *>())
        {
            auto childColors = colors;
            for (auto *ancestor = widget->parentWidget(); ancestor && ancestor != root;
                 ancestor = ancestor->parentWidget())
            {
                if (dynamic_cast<Controls::GroupPanel *>(ancestor))
                {
                    // Passive child surfaces and empty title strips belong to
                    // their card, rather than the outer window background.
                    childColors.background = colors.panel;
                    break;
                }
            }
            Controls::applyPalette(widget, childColors);
            if (dynamic_cast<Controls::TextLabel *>(widget) ||
                dynamic_cast<Controls::GroupPanel *>(widget) ||
                widget->property("midiToolsPassiveSurface").toBool())
            {
                widget->setAutoFillBackground(false);
                widget->setAttribute(Qt::WA_NoSystemBackground);
                widget->setAttribute(Qt::WA_StyledBackground, false);
            }
            if (dynamic_cast<Controls::ActionButton *>(widget))
            {
                widget->setProperty("surfaceColor",
                                    color("control-button-surface-normal", colors.panel));
                widget->setProperty("borderColor",
                                    color("control-button-border-normal", colors.border));
                widget->setProperty("textColor", color("control-button-text-normal", colors.text));
                widget->setProperty("accentColor",
                                    color("control-button-border-selected", colors.accent));
            }
            widget->update();
        }
    };
    // Restyling the application may polish child controls; apply the public
    // module palette after that synchronous theme update has finished.
    if (auto *core = ThemeManager::installedThemeCore())
    {
        QObject::connect(core, &ThemeCore::themeChanged, root,
                         [root, apply] { QTimer::singleShot(0, root, apply); });
    }
    QTimer::singleShot(0, root, apply);
}
} // namespace WinJACKNexus::UI
