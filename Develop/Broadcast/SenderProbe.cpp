// SPDX-License-Identifier: AGPL-3.0-only
#include "UdpTransport.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <QTimer>
#include <cstdio>
#include <vector>

namespace BC = WinJACKNexus::Broadcast;

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 4)
        return 1;
    QFile file(QString::fromUtf8(argv[3]));
    if (!file.open(QIODevice::ReadOnly))
        return 1;
    const auto fixture = QJsonDocument::fromJson(file.readAll()).object();
    const auto registration = fixture.value(QStringLiteral("registration")).toObject();
    const auto variants = fixture.value(QStringLiteral("variants")).toObject();
    const auto head = QByteArray::fromBase64(registration.value(QStringLiteral("opusHeads"))
                                                 .toObject()
                                                 .value(QStringLiteral("all"))
                                                 .toString()
                                                 .toLatin1());
    if (head.size() < 19)
        return 1;
    const auto channels = static_cast<unsigned char>(head[9]);
    if (channels != 1 && channels != 2 && channels != 6 && channels != 8)
        return 1;
    BC::ConnectionSettings settings;
    settings.serverBaseUrl = QUrl(QString::fromUtf8(argv[1]));
    settings.publisherId = QString::fromUtf8(argv[2]);
    const auto token = QTextStream(stdin).readLine().toUtf8();
    BC::PublishSession session;
    BC::UdpTransport transport(session);
    QTimer timer;
    timer.setTimerType(Qt::PreciseTimer);
    timer.setInterval(20);
    unsigned block = 0;
    std::vector<float> pcm(960 * channels);
    for (unsigned frame = 0; frame < 960; ++frame)
        for (unsigned channel = 0; channel < channels; ++channel)
            pcm[frame * channels + channel] = float(channel + 1) / 32;
    QObject::connect(&timer, &QTimer::timeout, &application,
                     [&]
                     {
                         BC::AudioBlock audio{
                             1, 0, 7, 1, std::uint64_t(block) * 960, 960, BC::pcm16(pcm.data(), pcm.size())};
                         bool sent = transport.send(audio);
                         for (auto variant = variants.begin(); variant != variants.end(); ++variant)
                         {
                             const auto packets = variant.value().toArray();
                             audio.codec = 2;
                             audio.variant = static_cast<std::uint8_t>(variant.key().toUInt());
                             audio.payload = QByteArray::fromBase64(
                                 packets[block % packets.size()].toString().toLatin1());
                             sent &= transport.send(audio);
                         }
                         if (!sent)
                             application.exit(2);
                         if (++block == 250)
                             application.quit();
                     });
    session.changed = [&]
    {
        if (session.state() != QStringLiteral("connecting") && session.state() != QStringLiteral("online") &&
            session.state() != QStringLiteral("stopped"))
            application.exit(2);
    };
    session.ready = [&] { session.sendControl(registration); };
    session.received = [&](const QJsonObject &message)
    {
        if (message.value(QStringLiteral("type")) != QStringLiteral("ack"))
        {
            application.exit(2);
            return;
        }
        std::puts("{\"state\":\"registered\"}");
        std::fflush(stdout);
        timer.start();
    };
    QTimer::singleShot(0, &application, [&] { session.start(settings, token); });
    QTimer::singleShot(7000, &application, [&] { application.exit(3); });
    return application.exec();
}
