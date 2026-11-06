// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

class QSettings;

namespace WinJACKNexus::Broadcast
{

struct ConnectionSettings
{
    QUrl serverBaseUrl{QStringLiteral("https://localhost:9443/bc")};
    QString publisherId = QStringLiteral("station");
    QString credentialTarget = QStringLiteral("WinJACKNexus/Broadcast/default");

    bool validate(QString *error = nullptr) const;
};

ConnectionSettings loadConnection(QSettings &settings);
bool saveConnection(QSettings &settings, const ConnectionSettings &connection, QString *error = nullptr);
ConnectionSettings loadGlobalConnection();
bool saveGlobalConnection(const ConnectionSettings &connection, QString *error = nullptr);

class Credentials
{
  public:
    static bool write(const QString &target, const QByteArray &token, QString *error = nullptr);
    static bool read(const QString &target, QByteArray &token, QString *error = nullptr);
    static bool remove(const QString &target, QString *error = nullptr);
};

} // namespace WinJACKNexus::Broadcast
