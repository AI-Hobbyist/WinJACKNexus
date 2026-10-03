/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/UI/HostI18n.hpp>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

namespace WinJACKNexus::VstScanner {
inline UI::I18nDomainLease initializeLanguage(UI::HostI18n& language,
    const QString& root, QString* error = nullptr)
{
    if (!language.initialize(root, error)) return {};
    QFile manifestFile(QStringLiteral(":/WinJACKNexus/VstScanner/i18n/manifest.json"));
    QFile englishFile(QStringLiteral(":/WinJACKNexus/VstScanner/i18n/en-US.json"));
    if (!manifestFile.open(QIODevice::ReadOnly) || !englishFile.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Bundled scanner language resources are unavailable");
        return {};
    }
    const auto manifestBytes = manifestFile.readAll();
    const auto manifest = QJsonDocument::fromJson(manifestBytes).object();
    const QJsonObject descriptor {
        {QStringLiteral("descriptorVersion"), 1},
        {QStringLiteral("i18nApiVersion"), 1},
        {QStringLiteral("moduleId"), manifest[QStringLiteral("moduleId")]},
        {QStringLiteral("domains"), manifest[QStringLiteral("domains")]},
        {QStringLiteral("catalogVersion"), manifest[QStringLiteral("catalogVersion")]},
        {QStringLiteral("locatePath"), QStringLiteral("locate")}
    };
    return language.manager().registerBundledModule(descriptor,
        QDir(root).filePath(QStringLiteral("vstscanner")), manifestBytes,
        englishFile.readAll(), error);
}
}
