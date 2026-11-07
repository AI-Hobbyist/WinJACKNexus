// SPDX-License-Identifier: AGPL-3.0-only
#include "Connection.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QUuid>
#include <cstdio>

namespace BC = WinJACKNexus::Broadcast;

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    int failures = 0;
    auto check = [&](bool value, const char *message)
    {
        std::printf("%s %s\n", value ? "PASS" : "FAIL", message);
        failures += !value;
    };
    QTemporaryDir temporary;
    check(temporary.isValid(), "isolated settings directory");
    QSettings settings(temporary.filePath("broadcast.ini"), QSettings::IniFormat);
    BC::ConnectionSettings config;
    config.serverBaseUrl = QUrl(QStringLiteral("https://stream.example.com:9443/bc"));
    config.publisherId = QStringLiteral("test_station");
    config.credentialTarget =
        QStringLiteral("WinJACKNexus/Broadcast/Test-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QByteArray token("bc-isolated-credential-test");
    QString error;
    check(BC::saveConnection(settings, config, &error), "persist global connection references");
    check(BC::Credentials::write(config.credentialTarget, token, &error),
          "write isolated Windows generic credential");
    QByteArray restored;
    check(BC::Credentials::read(config.credentialTarget, restored, &error) && restored == token,
          "retrieve exact credential bytes without printing them");
    check(BC::Credentials::write(config.credentialTarget, token + "\r\n", &error),
          "token copied from CRLF file is accepted like the server token file");
    check(BC::Credentials::read(config.credentialTarget, restored, &error) && restored == token,
          "file line ending is not stored in the bearer token");
    check(!BC::Credentials::write(config.credentialTarget, "\r\n \t", &error),
          "whitespace-only token is rejected");
    check(!BC::Credentials::write(config.credentialTarget, "first\nsecond", &error),
          "embedded newline is rejected");
    check(!BC::Credentials::write(config.credentialTarget, QByteArray("first\0second", 12), &error),
          "embedded NUL is rejected");
    check(BC::Credentials::read(config.credentialTarget, restored, &error) && restored == token,
          "rejected updates preserve the previous credential");
    const auto loaded = BC::loadConnection(settings);
    check(loaded.serverBaseUrl == config.serverBaseUrl && loaded.publisherId == config.publisherId &&
              loaded.credentialTarget == config.credentialTarget,
          "connection references roundtrip independently of project state");
    QFile file(settings.fileName());
    check(file.open(QIODevice::ReadOnly) && !file.readAll().contains(token),
          "ordinary settings contain no token");
    auto invalid = config;
    invalid.serverBaseUrl = QUrl(QStringLiteral("http://stream.example.com:9443/bc"));
    check(!BC::saveConnection(settings, invalid), "plaintext remote publishing is rejected");
    check(BC::loadConnection(settings).serverBaseUrl == config.serverBaseUrl,
          "invalid connection update leaves previous global settings intact");
    invalid.serverBaseUrl = QUrl(QStringLiteral("http://127.0.0.1:9443/bc"));
    check(invalid.validate(), "explicit plaintext loopback is available for offline integration");
    invalid.publisherId = QStringLiteral("station\n");
    check(!invalid.validate(), "publisher identity rejects a trailing newline");
    check(BC::Credentials::remove(config.credentialTarget, &error),
          "remove only the isolated test credential");
    restored = "must be cleared";
    check(!BC::Credentials::read(config.credentialTarget, restored) && restored.isEmpty(),
          "missing credential reports failure and clears output");
    return failures ? 1 : 0;
}
