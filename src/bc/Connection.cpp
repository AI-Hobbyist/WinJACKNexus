// SPDX-License-Identifier: AGPL-3.0-only
#include "Connection.hpp"
#include <QRegularExpression>
#include <QSettings>
#include <Windows.h>
#include <wincred.h>

namespace WinJACKNexus::Broadcast
{
namespace
{
bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

bool validCredentialTarget(const QString &target)
{
    return target.startsWith(QStringLiteral("WinJACKNexus/Broadcast/")) &&
           target.size() <= CRED_MAX_GENERIC_TARGET_NAME_LENGTH && !target.contains(QChar::Null);
}

bool credentialError(QString *error, const char *operation, DWORD code)
{
    return fail(
        error,
        QStringLiteral("Windows credential %1 failed (%2)").arg(QString::fromLatin1(operation)).arg(code));
}
} // namespace

bool ConnectionSettings::validate(QString *error) const
{
    const auto host = serverBaseUrl.host();
    const bool loopback = host == QStringLiteral("127.0.0.1") || host == QStringLiteral("::1") ||
                          host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0;
    const bool transport = serverBaseUrl.scheme() == QStringLiteral("https") ||
                           (serverBaseUrl.scheme() == QStringLiteral("http") && loopback);
    if (!serverBaseUrl.isValid() || host.isEmpty() || !transport || !serverBaseUrl.userInfo().isEmpty() ||
        serverBaseUrl.hasQuery() || serverBaseUrl.hasFragment() ||
        (serverBaseUrl.path() != QStringLiteral("/bc") && serverBaseUrl.path() != QStringLiteral("/bc/")))
        return fail(error,
                    QStringLiteral("Use an HTTPS server base URL ending in /bc; plaintext is loopback-only"));
    static const QRegularExpression idPattern(
        QRegularExpression::anchoredPattern(QStringLiteral("[A-Za-z0-9_-]{1,64}")));
    if (!idPattern.match(publisherId).hasMatch())
        return fail(
            error,
            QStringLiteral("Publisher ID must contain 1-64 ASCII letters, digits, hyphens or underscores"));
    if (!validCredentialTarget(credentialTarget))
        return fail(error, QStringLiteral("Invalid broadcast credential reference"));
    return true;
}

ConnectionSettings loadConnection(QSettings &settings)
{
    ConnectionSettings connection;
    connection.serverBaseUrl =
        QUrl(settings.value(QStringLiteral("broadcast/serverBaseUrl"), connection.serverBaseUrl.toString())
                 .toString());
    connection.publisherId =
        settings.value(QStringLiteral("broadcast/publisherId"), connection.publisherId).toString();
    connection.credentialTarget =
        settings.value(QStringLiteral("broadcast/credentialTarget"), connection.credentialTarget).toString();
    return connection;
}

bool saveConnection(QSettings &settings, const ConnectionSettings &connection, QString *error)
{
    if (!connection.validate(error))
        return false;
    settings.setValue(QStringLiteral("broadcast/serverBaseUrl"), connection.serverBaseUrl.toString());
    settings.setValue(QStringLiteral("broadcast/publisherId"), connection.publisherId);
    settings.setValue(QStringLiteral("broadcast/credentialTarget"), connection.credentialTarget);
    settings.sync();
    return settings.status() == QSettings::NoError ||
           fail(error, QStringLiteral("Unable to persist broadcast connection settings"));
}

ConnectionSettings loadGlobalConnection()
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("WinJACKNexus"),
                       QStringLiteral("Application"));
    return loadConnection(settings);
}

bool saveGlobalConnection(const ConnectionSettings &connection, QString *error)
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("WinJACKNexus"),
                       QStringLiteral("Application"));
    return saveConnection(settings, connection, error);
}

bool Credentials::write(const QString &target, const QByteArray &token, QString *error)
{
    if (!validCredentialTarget(target) || token.isEmpty() || token.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE ||
        token.contains('\r') || token.contains('\n') || token.contains('\0'))
        return fail(error, QStringLiteral("Invalid broadcast credential target or token"));
    auto name = target.toStdWString();
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = name.data();
    credential.CredentialBlobSize = static_cast<DWORD>(token.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(token.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    wchar_t user[] = L"WinJACKNexus Broadcast";
    credential.UserName = user;
    return CredWriteW(&credential, 0) || credentialError(error, "write", GetLastError());
}

bool Credentials::read(const QString &target, QByteArray &token, QString *error)
{
    token.clear();
    if (!validCredentialTarget(target))
        return fail(error, QStringLiteral("Invalid broadcast credential reference"));
    const auto name = target.toStdWString();
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(name.c_str(), CRED_TYPE_GENERIC, 0, &credential))
        return credentialError(error, "read", GetLastError());
    token = QByteArray(reinterpret_cast<const char *>(credential->CredentialBlob),
                       credential->CredentialBlobSize);
    CredFree(credential);
    return true;
}

bool Credentials::remove(const QString &target, QString *error)
{
    if (!validCredentialTarget(target))
        return fail(error, QStringLiteral("Invalid broadcast credential reference"));
    const auto name = target.toStdWString();
    return CredDeleteW(name.c_str(), CRED_TYPE_GENERIC, 0) ||
           credentialError(error, "delete", GetLastError());
}

} // namespace WinJACKNexus::Broadcast
