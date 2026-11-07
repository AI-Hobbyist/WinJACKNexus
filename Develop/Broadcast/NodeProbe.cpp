// SPDX-License-Identifier: AGPL-3.0-only
#include "Service.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibrary>
#include <QTextStream>
#include <QTimer>
#include <WinJACKNexus/Audio/JackApi.hpp>
#include <WinJACKNexus/Node/NodeApi.h>
#include <array>
#include <cmath>
#include <cstdio>

namespace BC = WinJACKNexus::Broadcast;
namespace Model = WinJACKNexus::Common::Broadcast;
namespace Audio = WinJACKNexus::Audio;
using Node = WinJACKNexus::Node::INode;

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 4)
        return 1;
    const QString fixturePath = QString::fromUtf8(WJN_BC_BOUNDARY);
    QLibrary fixture(fixturePath);
    auto &api = Audio::JackApi::instance();
    if (!fixture.load() || !api.load(reinterpret_cast<const wchar_t *>(fixturePath.utf16())))
        return 2;
    const auto process = reinterpret_cast<void (*)()>(fixture.resolve("wjn_boundary_process"));
    const auto clock =
        reinterpret_cast<void (*)(jack_nframes_t, jack_nframes_t)>(fixture.resolve("wjn_boundary_set_clock"));
    const auto counter =
        reinterpret_cast<std::uint32_t (*)(std::uint32_t)>(fixture.resolve("wjn_boundary_counter"));
    if (!process || !clock || !counter)
        return 2;
    clock(48000, 480);
    QLibrary module(application.applicationDirPath() + "/nodes/lan_remote_audio/mod_broadcast.dll");
    if (!module.load())
        return 2;
    const auto create = reinterpret_cast<Node *(*)(std::uint32_t)>(module.resolve("wjn_create_node_v4"));
    const auto destroy = reinterpret_cast<void (*)(Node *)>(module.resolve("wjn_destroy_node"));
    const auto restore =
        reinterpret_cast<int (*)(Node *, const char *)>(module.resolve("wjn_set_node_settings_json"));
    if (!create || !destroy || !restore)
        return 2;
    std::unique_ptr<Node, decltype(destroy)> node(create(WJN_NODE_MODE_JACK), destroy);
    Model::State state;
    state.clientName = QStringLiteral("OfflineBC");
    for (const auto layout :
         {Model::Layout::mono, Model::Layout::stereo, Model::Layout::surround51, Model::Layout::surround71})
        state.addStream(Model::layoutName(layout), layout);
    for (auto &stream : state.streams)
        stream.gainDb = -6.020599913279624;
    const auto json = QJsonDocument(state.toJson()).toJson(QJsonDocument::Compact);
    if (!node || !restore(node.get(), json.constData()) || !node->init())
        return 2;
    struct Buffer
    {
        float *samples;
        bool input;
        double frequency;
    };
    std::vector<Buffer> buffers;
    const std::array<double, 8> frequencies{330, 440, 550, 60, 770, 880, 990, 1100};
    for (const auto &description : state.ports())
    {
        const auto name = (state.clientName + ":" + description.name).toUtf8();
        auto *port = api.portByName(nullptr, name.constData());
        if (!port)
            return 2;
        const auto stream = std::find_if(state.streams.begin(), state.streams.end(),
                                         [&](const auto &item) { return item.id == description.streamId; });
        const auto roles = Model::channelNames(stream->layout);
        const double frequency =
            stream->layout == Model::Layout::mono ? 220 : frequencies[roles.indexOf(description.role)];
        buffers.push_back({static_cast<float *>(api.portGetBuffer(port, 480)), description.input, frequency});
    }
    BC::ConnectionSettings settings;
    settings.serverBaseUrl = QUrl(QString::fromUtf8(argv[1]));
    settings.publisherId = QString::fromUtf8(argv[2]);
    settings.credentialTarget = QString::fromUtf8(argv[3]);
    QString error;
    if (!BC::Credentials::write(settings.credentialTarget, QTextStream(stdin).readLine().toUtf8(), &error))
        return 2;
    struct CredentialCleanup
    {
        QString target;
        ~CredentialCleanup()
        {
            BC::Credentials::remove(target);
        }
    } cleanup{settings.credentialTarget};
    auto &service = BC::Service::instance();
    bool announced = false, monitorSilent = true;
    quint64 position = 0;
    QTimer producer;
    producer.setTimerType(Qt::PreciseTimer);
    producer.setInterval(10);
    QObject::connect(&producer, &QTimer::timeout, &application,
                     [&]
                     {
                         for (auto &buffer : buffers)
                             if (buffer.input)
                                 for (unsigned frame = 0; frame < 480; ++frame)
                                     buffer.samples[frame] =
                                         float(0.2 * std::sin(2 * 3.141592653589793 * buffer.frequency *
                                                              double(position + frame) / 48000));
                         process();
                         for (const auto &buffer : buffers)
                             if (!buffer.input)
                                 for (unsigned frame = 0; frame < 480; ++frame)
                                     monitorSilent &= buffer.samples[frame] == 0;
                         position += 480;
                     });
    QObject::connect(&service, &BC::Service::statusChanged, &application,
                     [&]
                     {
                         const auto status = service.status();
                         if (!announced && status.activeStreams == 4)
                         {
                             announced = true;
                             QJsonArray ids;
                             for (const auto &stream : state.streams)
                                 ids.append(stream.id);
                             const QJsonDocument ready(QJsonObject{
                                 {"state", "publishing"}, {"streams", ids}, {"ports", int(counter(4))}});
                             std::puts(ready.toJson(QJsonDocument::Compact).constData());
                             std::fflush(stdout);
                         }
                         if (!status.error.isEmpty())
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
    QTimer::singleShot(18000, &application, &QCoreApplication::quit);
    const auto result = application.exec();
    producer.stop();
    node.reset();
    const QJsonDocument report(QJsonObject{{"monitorSilent", monitorSilent},
                                           {"callbacks", int(counter(2))},
                                           {"remainingClients", int(counter(3))},
                                           {"remainingPorts", int(counter(4))}});
    std::puts(report.toJson(QJsonDocument::Compact).constData());
    api.unload();
    return result;
}
