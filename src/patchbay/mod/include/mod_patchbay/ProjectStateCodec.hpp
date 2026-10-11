/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace WinJACKNexus::Patchbay::ProjectStateCodec
{

inline QString encode(const QJsonObject &state)
{
    return QString::fromLatin1(QJsonDocument(state).toJson(QJsonDocument::Compact).toBase64());
}

inline bool decode(const QString &encoded, QJsonObject &state)
{
    const auto result =
        QByteArray::fromBase64Encoding(encoded.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!result)
        return false;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(result.decoded, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return false;
    state = document.object();
    return true;
}

inline QJsonObject archiveModuleSettings(QJsonObject record, const QJsonObject &settings)
{
    record.remove(QStringLiteral("settings"));
    record.insert(QStringLiteral("settingsBase64"), encode(settings));
    return record;
}

inline bool restoreModuleSettings(const QJsonObject &record, QJsonObject &settings)
{
    if (record.contains(QStringLiteral("settingsBase64")))
    {
        const auto encoded = record.value(QStringLiteral("settingsBase64"));
        return encoded.isString() && decode(encoded.toString(), settings);
    }
    settings = record.value(QStringLiteral("settings")).toObject();
    return true;
}

}
