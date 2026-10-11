// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QStringList>
#include <WinJACKNexus/UI/HostI18n.hpp>
#include <Windows.h>

namespace WinJACKNexus::App
{
inline QStringList deprecatedMultiNodePaths(const QString &root)
{
    QStringList paths;
    QDirIterator files(root, {"*.dll"}, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext())
    {
        const QFileInfo file(files.next());
        const auto name = file.fileName().toLower();
        bool deprecated = name == "mod_adapter.dll" || name == "mod_meterbridge_probe.dll";
        const auto path = file.absoluteFilePath();
        const HMODULE library = LoadLibraryW(reinterpret_cast<const wchar_t *>(path.utf16()));
        if (library)
        {
            // This retired export is queried only to diagnose legacy modules.
            using LegacyCount = std::uint32_t (*)(void);
            const auto count =
                reinterpret_cast<LegacyCount>(GetProcAddress(library, "wjn_get_node_count"));
            deprecated = deprecated || (count && count() > 1);
            FreeLibrary(library);
        }
        if (deprecated)
            paths.append(QDir::toNativeSeparators(path));
    }
    paths.removeDuplicates();
    paths.sort(Qt::CaseInsensitive);
    return paths;
}

inline void showDeprecatedNodeNoticeOnce(const QString &root, QSettings &settings)
{
    const auto key = QStringLiteral("nodes/deprecatedMultiNodeNoticeShown");
    if (settings.value(key, false).toBool())
        return;
    const auto paths = deprecatedMultiNodePaths(root);
    settings.setValue(key, true);
    settings.sync();
    if (paths.isEmpty())
        return;
    const auto title = WinJACKNexus::UI::hostText("nodes.deprecated.title", "Deprecated nodes");
    const auto message = WinJACKNexus::UI::hostText(
        "nodes.deprecated.message",
        "The following multi-node DLLs were ignored. Reason: deprecated.\n\n{paths}",
        {{QStringLiteral("paths"), paths.join(QStringLiteral("\n"))}});
    QMessageBox::warning(nullptr, title, message);
}
}
