// SPDX-License-Identifier: AGPL-3.0-only
#include "Service.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <QTimer>
#include <cmath>
#include <cstdio>

namespace BC = WinJACKNexus::Broadcast;
namespace Model = WinJACKNexus::Common::Broadcast;

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 4 && argc != 5)
        return 1;
    BC::ConnectionSettings settings;
    settings.serverBaseUrl = QUrl(QString::fromUtf8(argv[1]));
    settings.publisherId = QString::fromUtf8(argv[2]);
    settings.credentialTarget = QString::fromUtf8(argv[3]);
    QString error;
    if (!BC::Credentials::write(settings.credentialTarget, QTextStream(stdin).readLine().toUtf8(), &error))
        return 2;
    auto &service = BC::Service::instance();
    const auto mode = argc == 5 ? QString::fromUtf8(argv[4]) : QString();
    const bool lifecycle = mode == QStringLiteral("lifecycle");
    const bool disconnect = mode == QStringLiteral("disconnect");
    const auto layout = mode == QStringLiteral("7.1")      ? Model::Layout::surround71
                        : mode == QStringLiteral("5.1")    ? Model::Layout::surround51
                        : mode == QStringLiteral("stereo") ? Model::Layout::stereo
                                                           : Model::Layout::mono;
    Model::State first, second;
    first.addStream(QStringLiteral("Source"), layout);
    second.addStream(QStringLiteral("Stereo"), Model::Layout::stereo);
    auto mono = std::make_shared<Model::AudioStream>(48000, first.streams[0].controls());
    auto stereo = std::make_shared<Model::AudioStream>(48000, second.streams[0].controls());
    service.setPublications(&first, {{first.streams[0], {}, mono}});
    service.setPublications(&second, {{second.streams[0], {}, stereo}});
    QTimer producer;
    producer.setTimerType(Qt::PreciseTimer);
    producer.setInterval(10);
    quint64 position = 0;
    bool monitorAfterStop = false;
    first.streams[0].listen = lifecycle || disconnect;
    QObject::connect(
        &producer, &QTimer::timeout, &application,
        [&]
        {
            std::array<std::array<float, 480>, 8> source{}, monitor{};
            std::array<const float *, 8> sourceInput{};
            std::array<float *, 8> monitorOutput{};
            std::array<float, 480> l, r;
            const std::array<double, 8> frequencies{330, 440, 550, 60, 770, 880, 990, 1100};
            const auto channels = Model::channelCount(layout);
            for (unsigned channel = 0; channel < channels; ++channel)
            {
                sourceInput[channel] = source[channel].data();
                monitorOutput[channel] = monitor[channel].data();
            }
            for (unsigned frame = 0; frame < 480; ++frame)
            {
                const auto time = double(position + frame) / 48000;
                for (unsigned channel = 0; channel < channels; ++channel)
                    source[channel][frame] =
                        float(0.2 * std::sin(2 * 3.141592653589793 *
                                             (channels == 1 ? 220 : frequencies[channel]) * time));
                l[frame] = float(0.2 * std::sin(2 * 3.141592653589793 * 330 * time));
                r[frame] = float(0.2 * std::sin(2 * 3.141592653589793 * 440 * time));
            }
            const float *stereoInput[] = {l.data(), r.data()};
            mono->render(first.streams[0].controls(), false, sourceInput.data(), monitorOutput.data(), 480);
            if (service.status().state == QStringLiteral("stopped") ||
                service.status().state == QStringLiteral("retrying"))
                for (const auto value : monitor[0])
                    monitorAfterStop |= std::abs(value) > 0.1f;
            stereo->render(second.streams[0].controls(), false, stereoInput, nullptr, 480);
            position += 480;
            if (disconnect && service.status().state == QStringLiteral("retrying") && monitorAfterStop)
            {
                std::puts("{\"state\":\"retrying\",\"monitorAlive\":true}");
                std::fflush(stdout);
                application.quit();
            }
        });
    bool announced = false;
    unsigned publications = 0;
    QObject::connect(
        &service, &BC::Service::statusChanged, &application,
        [&]
        {
            const auto status = service.status();
            if (!announced && status.activeStreams == 2)
            {
                announced = true;
                const auto json = QJsonDocument(QJsonObject{
                    {QStringLiteral("state"), status.state},
                    {QStringLiteral("streams"), QJsonArray{first.streams[0].id, second.streams[0].id}}});
                std::puts(json.toJson(QJsonDocument::Compact).constData());
                std::fflush(stdout);
                if (lifecycle)
                {
                    ++publications;
                    QTimer::singleShot(
                        600, &application,
                        [&]
                        {
                            if (publications == 1)
                            {
                                service.stop();
                                QTimer::singleShot(100, &application,
                                                   [&]
                                                   {
                                                       std::printf(
                                                           "{\"state\":\"stopped\",\"monitorAlive\":%s}\n",
                                                           monitorAfterStop ? "true" : "false");
                                                       std::fflush(stdout);
                                                       announced = false;
                                                       service.start(settings, &error);
                                                   });
                            }
                            else
                            {
                                service.detach(&first);
                                QTimer::singleShot(100, &application,
                                                   [&]
                                                   {
                                                       std::printf(
                                                           "{\"state\":\"removed\",\"activeStreams\":%u}\n",
                                                           service.status().activeStreams);
                                                       std::fflush(stdout);
                                                   });
                            }
                        });
                }
            }
            if (!status.error.isEmpty() && !(disconnect && status.state == QStringLiteral("retrying")))
            {
                std::fprintf(stderr, "%s\n", status.error.toUtf8().constData());
                application.exit(2);
            }
        });
    QTimer::singleShot(0, &application,
                       [&]
                       {
                           if (!service.start(settings, &error))
                               application.exit(2);
                           producer.start();
                       });
    QTimer::singleShot(3500, &application, &QCoreApplication::quit);
    const auto result = application.exec();
    producer.stop();
    service.detach(&first);
    service.detach(&second);
    BC::Credentials::remove(settings.credentialTarget);
    return result;
}
