/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <QByteArray>
#include <QFile>
#include <QResource>

#include <cstdint>

extern "C" __declspec(dllexport) std::uint32_t wjn_default_theme_api_version()
{
    return 1;
}

extern "C" __declspec(dllexport) const char *wjn_default_theme_package_data(std::uint32_t *size)
{
    if (!size)
        return nullptr;
    static const QByteArray package = [] {
        Q_INIT_RESOURCE(default_theme);
        QFile file(QStringLiteral(":/WinJACKNexus/DefaultTheme/default.wjnskin"));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }();
    *size = std::uint32_t(package.size());
    return package.isEmpty() ? nullptr : package.constData();
}
