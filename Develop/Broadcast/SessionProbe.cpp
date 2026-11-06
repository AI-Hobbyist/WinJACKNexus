// SPDX-License-Identifier: AGPL-3.0-only
#include "PublishSession.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTextStream>
#include <QTimer>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 3 && argc != 4)
        return 1;
    if (argc == 4)
    {
        // Only this test process trusts the fixture CA; verification stays on.
        // Use a closed test trust set instead of exporting the system CA list
        // into Schannel's temporary certificate store.
        const auto certificates = QSslCertificate::fromPath(QString::fromUtf8(argv[3]));
        if (certificates.isEmpty())
            return 1;
        auto tls = QSslConfiguration::defaultConfiguration();
        tls.setCaCertificates(certificates);
        QSslConfiguration::setDefaultConfiguration(tls);
    }
    WinJACKNexus::Broadcast::ConnectionSettings settings;
    settings.serverBaseUrl = QUrl(QString::fromUtf8(argv[1]));
    settings.publisherId = QString::fromUtf8(argv[2]);
    const auto token = QTextStream(stdin).readLine().toUtf8();
    WinJACKNexus::Broadcast::PublishSession session;
    session.changed = [&]
    {
        if (session.state() == QStringLiteral("connecting") || session.state() == QStringLiteral("stopped"))
            return;
        const QJsonObject report{{QStringLiteral("state"), session.state()},
                                 {QStringLiteral("tlsBackend"), settings.serverBaseUrl.scheme() == "https"
                                                                    ? QSslSocket::activeBackend()
                                                                    : QString()},
                                 {QStringLiteral("udpPort"), session.udpPort()},
                                 {QStringLiteral("maxDatagram"), 1200}};
        std::printf("%s\n", QJsonDocument(report).toJson(QJsonDocument::Compact).constData());
        std::fflush(stdout);
        application.exit(session.state() == QStringLiteral("online") ? 0 : 2);
    };
    QTimer::singleShot(0, &application, [&] { session.start(settings, token); });
    QTimer::singleShot(7000, &application, [&] { application.exit(3); });
    return application.exec();
}
