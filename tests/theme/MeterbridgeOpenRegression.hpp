/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <QApplication>
#include <QLibrary>
#include <QWidget>

namespace MeterbridgeRegression
{
// Layout used by already distributed modules. Keep additional implementation
// state behind ThemeManager's existing shared pointer instead of growing it.
struct StableThemeManagerLayout : QObject
{
    WinJACKNexus::UI::ThemePalette basePalette;
    WinJACKNexus::UI::ThemePalette palette;
    QString themeName, sourcePath, sourceText, styleSheet;
    QHash<QString, QString> variables;
    QPointer<WinJACKNexus::UI::ThemeCore> core;
    QMetaObject::Connection coreConnection, previewConnection;
    std::shared_ptr<void> fontState;
    bool applicationOwner = false;
};
static_assert(sizeof(WinJACKNexus::UI::ThemeManager) == sizeof(StableThemeManagerLayout),
              "ThemeManager must remain compatible with modules allocating it by value");
}

template <class Check>
void verifyMeterbridgeOpen(QApplication &application, const QString &path, Check check)
{
    WinJACKNexus::UI::ThemeCore core;
    WinJACKNexus::UI::ThemeManager::installThemeCore(&core);
    WinJACKNexus::UI::ThemeManager manager;
    manager.watchApplication(&application);
    QLibrary library(path);
    check(library.load(), "load the runtime meterbridge DLL");
    using Create = WinJACKNexus::Node::INode *(*)(std::uint32_t);
    using Destroy = void (*)(WinJACKNexus::Node::INode *);
    using Show = int (*)(const char *);
    auto create = reinterpret_cast<Create>(library.resolve("wjn_create_node"));
    auto destroy = reinterpret_cast<Destroy>(library.resolve("wjn_destroy_node"));
    auto show = reinterpret_cast<Show>(library.resolve("wjn_meterbridge_show_for_instance"));
    check(create && destroy && show, "resolve actual meterbridge menu and node entry points");
    if (create && destroy && show)
    {
        auto *node = create(0);
        check(node && node->init(), "initialize the runtime meterbridge");
        if (node)
        {
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                check(show(nullptr) == 1, "open meterbridge through the menu export");
                auto handle = node->createWindow(nullptr);
                auto *window = QWidget::find(reinterpret_cast<WId>(handle));
                check(window != nullptr, "meterbridge provides a real top-level window");
                application.processEvents();
                if (window)
                {
                    check(!window->grab().isNull(), "paint the opened meterbridge");
                    window->close();
                }
                check(show(nullptr) == 1, "reopen the closed meterbridge");
                application.processEvents();
                node->destroyWindow(handle);
            }
            node->shutdown();
            destroy(node);
        }
    }
    WinJACKNexus::UI::ThemeManager::installThemeCore(nullptr);
}
