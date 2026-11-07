// SPDX-License-Identifier: AGPL-3.0-only
#include "Service.hpp"
#include "Encoder.hpp"
#include "UdpTransport.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QThread>
#include <map>
#include <optional>
#include <set>

namespace WinJACKNexus::Broadcast
{
namespace Model = Common::Broadcast;
namespace
{
class Worker final : public QObject
{
  public:
    struct Entry
    {
        Publication publication;
        quint32 handle = 0, epoch = 0, request = 0;
        quint64 generation = 0, cursor = 0;
        std::optional<quint64> origin;
        std::unique_ptr<Encoder> encoder;
    };

    PublishSession session;
    UdpTransport transport{session};
    QTimer pump;
    std::map<QString, Entry> entries;
    std::function<void(ServiceStatus)> report;
    quint32 nextHandle = 0, nextRequest = 0;
    quint64 staleFrames = 0;
    QString failure;
    QElapsedTimer reporting;
    std::array<Model::QueuedFrame, 1920> frames;
    std::array<float, 1920 * 8> samples;

    Worker()
    {
        pump.setTimerType(Qt::PreciseTimer);
        pump.setInterval(1);
        reporting.start();
        connect(&pump, &QTimer::timeout, this, [this] { consume(); });
        session.changed = [this]
        {
            if (session.state() != QStringLiteral("online"))
                disarm();
            notify();
        };
        session.ready = [this]
        {
            transport.reset();
            for (auto &[id, entry] : entries)
                registerStream(entry, true);
            pump.start();
        };
        session.received = [this](const QJsonObject &message)
        {
            const auto type = message.value(QStringLiteral("type")).toString();
            if (type == QStringLiteral("error"))
            {
                fail(QStringLiteral("Server rejected stream registration"));
                return;
            }
            if (type != QStringLiteral("ack"))
                return;
            const auto request = message.value(QStringLiteral("requestId")).toInteger();
            for (auto &[id, entry] : entries)
                if (entry.request && entry.request == request)
                {
                    entry.request = 0;
                    if (!entry.generation)
                        entry.generation = entry.publication.audio->arm();
                }
            notify();
        };
    }

    void disarm()
    {
        pump.stop();
        for (auto &[id, entry] : entries)
        {
            entry.publication.audio->disarm();
            entry.generation = 0;
            entry.request = 0;
        }
    }

    void stop()
    {
        disarm();
        session.stop();
        transport.reset();
        failure.clear();
        notify();
    }

    void fail(const QString &error)
    {
        stop();
        failure = error;
        notify();
    }

    void registerStream(Entry &entry, bool reset)
    {
        try
        {
            if (reset)
            {
                entry.publication.audio->disarm();
                entry.generation = 0;
                entry.origin.reset();
                entry.cursor = 0;
                entry.encoder =
                    std::make_unique<Encoder>(entry.publication.stream.layout,
                                              entry.publication.audio->rate(), entry.handle, ++entry.epoch);
            }
            const auto &publication = entry.publication;
            entry.request = ++nextRequest;
            if (!session.sendControl(
                    {{QStringLiteral("type"), QStringLiteral("register")},
                     {QStringLiteral("requestId"), qint64(entry.request)},
                     {QStringLiteral("streamId"), publication.stream.id},
                     {QStringLiteral("handle"), qint64(entry.handle)},
                     {QStringLiteral("epoch"), qint64(entry.epoch)},
                     {QStringLiteral("displayName"), publication.stream.name},
                     {QStringLiteral("category"), QJsonArray::fromStringList(publication.category)},
                     {QStringLiteral("layout"), Model::layoutName(publication.stream.layout)},
                     {QStringLiteral("pcmRate"), int(publication.audio->rate())},
                     {QStringLiteral("opusHeads"), entry.encoder->opusHeads()}}))
                fail(QStringLiteral("Could not send stream registration"));
        }
        catch (const std::exception &)
        {
            fail(QStringLiteral("Could not initialize audio encoder"));
        }
    }

    void update(const std::vector<Publication> &publications)
    {
        std::set<QString> retained;
        for (const auto &publication : publications)
            retained.insert(publication.stream.id);
        for (auto it = entries.begin(); it != entries.end();)
        {
            if (retained.contains(it->first))
            {
                ++it;
                continue;
            }
            it->second.publication.audio->disarm();
            if (session.state() == QStringLiteral("online"))
                session.sendControl({{QStringLiteral("type"), QStringLiteral("unregister")},
                                     {QStringLiteral("requestId"), qint64(++nextRequest)},
                                     {QStringLiteral("handle"), qint64(it->second.handle)}});
            it = entries.erase(it);
        }
        for (const auto &publication : publications)
        {
            auto [it, added] = entries.try_emplace(publication.stream.id);
            auto &entry = it->second;
            const bool reset = added || entry.publication.audio != publication.audio;
            const bool metadata = added || entry.publication.stream.name != publication.stream.name ||
                                  entry.publication.category != publication.category;
            if (reset && !added)
                entry.publication.audio->disarm();
            entry.publication = publication;
            if (added)
                entry.handle = ++nextHandle;
            if ((reset || metadata) && session.state() == QStringLiteral("online"))
                registerStream(entry, reset);
        }
        notify();
    }

    void consume()
    {
        for (auto &[id, entry] : entries)
        {
            if (!entry.generation)
                continue;
            const auto channels = Model::channelCount(entry.publication.stream.layout);
            // Timer delivery can be delayed beyond one 10 ms chunk. Drain a
            // bounded 100 ms budget per stream rather than falling behind on
            // every wake; the existing age/continuity checks still apply.
            for (unsigned batch = 0; batch < 10; ++batch)
            {
                const auto count = entry.publication.audio->read(
                    std::span(frames).first(entry.publication.audio->rate() / 100));
                if (!count)
                    break;
                std::size_t valid = 0;
                const auto now = Model::AudioStream::nowNanoseconds();
                for (std::size_t index = 0; index < count; ++index)
                {
                    const auto &frame = frames[index];
                    if (frame.generation != entry.generation)
                        continue;
                    if (now - frame.createdNanoseconds > 100000000)
                    {
                        ++staleFrames;
                        continue;
                    }
                    if (!entry.origin)
                        entry.origin = frame.position;
                    if (frame.position - *entry.origin != entry.cursor + valid)
                    {
                        fail(QStringLiteral(
                            "Audio source exceeded publishing queue budget; restart publishing"));
                        return;
                    }
                    std::copy_n(frame.samples.data(), channels, samples.data() + valid * channels);
                    ++valid;
                }
                if (!valid)
                    continue;
                try
                {
                    for (const auto &block :
                         entry.encoder->push(std::span(samples).first(valid * channels), entry.cursor))
                        if (!transport.send(block))
                        {
                            fail(transport.error());
                            return;
                        }
                    entry.cursor += valid;
                }
                catch (const std::exception &)
                {
                    fail(QStringLiteral("Audio encoding failed"));
                    return;
                }
            }
        }
        if (reporting.elapsed() >= 100)
        {
            reporting.restart();
            notify();
        }
    }

    void notify()
    {
        ServiceStatus status;
        status.state = failure.isEmpty() ? session.state() : QStringLiteral("error");
        status.error = failure.isEmpty() ? session.error() : failure;
        for (const auto &[id, entry] : entries)
        {
            status.activeStreams += entry.generation != 0;
            status.droppedFrames += entry.publication.audio->droppedFrames();
        }
        if (status.activeStreams)
            status.state = QStringLiteral("publishing");
        status.sentBytes = transport.sentBytes();
        status.sentDatagrams = transport.sentDatagrams();
        status.staleFrames = staleFrames;
        status.controlRttMilliseconds = session.controlRttMilliseconds();
        if (report)
            report(status);
    }
};
} // namespace

class Service::Impl
{
  public:
    std::map<const void *, std::vector<Publication>> owners;
    QThread thread;
    QObject *context = nullptr;
    Worker *worker = nullptr;
    ServiceStatus status;
    ConnectionSettings connection = loadGlobalConnection();
    quint64 generation = 0;

    std::vector<Publication> publications() const
    {
        std::vector<Publication> result;
        for (const auto &[owner, list] : owners)
            result.insert(result.end(), list.begin(), list.end());
        return result;
    }
};

Service &Service::instance()
{
    static Service service;
    return service;
}

Service::Service() : impl_(std::make_unique<Impl>())
{
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] { stop(); });
}
Service::~Service()
{
    stop();
}

bool Service::setPublications(const void *owner, std::vector<Publication> publications, QString *error)
{
    std::set<QString> ids;
    std::size_t count = publications.size();
    for (const auto &[other, list] : impl_->owners)
        if (other != owner)
        {
            count += list.size();
            for (const auto &publication : list)
                ids.insert(publication.stream.id);
        }
    for (const auto &publication : publications)
        if (!publication.audio || publication.stream.id.isEmpty() ||
            publication.stream.layout != publication.audio->layout() ||
            !ids.insert(publication.stream.id).second)
        {
            if (error)
                *error = QStringLiteral("Invalid or duplicate publishing stream");
            return false;
        }
    if (!owner || count > 64)
    {
        if (error)
            *error = QStringLiteral("Publishing capacity exceeded");
        return false;
    }
    impl_->owners[owner] = std::move(publications);
    if (impl_->worker)
    {
        auto all = impl_->publications();
        auto *worker = impl_->worker;
        QMetaObject::invokeMethod(impl_->context, [worker, all = std::move(all)] { worker->update(all); });
    }
    return true;
}

void Service::detach(const void *owner)
{
    setPublications(owner, {});
    impl_->owners.erase(owner);
    if (impl_->owners.empty())
        stop();
}

bool Service::start(QString *error)
{
    return start(impl_->connection, error);
}

bool Service::start(const ConnectionSettings &settings, QString *error)
{
    QByteArray token;
    if (!settings.validate(error) || !Credentials::read(settings.credentialTarget, token, error))
        return false;
    stop();
    auto &state = *impl_;
    state.context = new QObject;
    state.context->moveToThread(&state.thread);
    connect(&state.thread, &QThread::finished, state.context, &QObject::deleteLater);
    state.thread.start();
    const auto generation = ++state.generation;
    // Construct every socket/timer and destroy them on their owning event thread.
    QMetaObject::invokeMethod(
        state.context,
        [this, generation, settings, token]() mutable
        {
            auto *worker = new Worker;
            impl_->worker = worker;
            worker->setParent(impl_->context);
            worker->report = [this, generation](ServiceStatus status)
            {
                QMetaObject::invokeMethod(this,
                                          [this, generation, status]
                                          {
                                              if (impl_->generation == generation)
                                              {
                                                  impl_->status = status;
                                                  emit statusChanged();
                                              }
                                          });
            };
            worker->update(impl_->publications());
            worker->session.start(settings, token);
            token.fill('\0');
        },
        Qt::BlockingQueuedConnection);
    token.fill('\0');
    return true;
}

void Service::stop()
{
    auto &state = *impl_;
    ++state.generation;
    if (state.context)
    {
        QMetaObject::invokeMethod(
            state.context,
            [&state]
            {
                state.worker->report = {};
                state.worker->stop();
                delete state.worker;
                state.worker = nullptr;
            },
            Qt::BlockingQueuedConnection);
        state.thread.quit();
        state.thread.wait();
        state.context = nullptr;
    }
    state.status = {};
    emit statusChanged();
}

ServiceStatus Service::status() const
{
    return impl_->status;
}

ConnectionSettings Service::connection() const
{
    return impl_->connection;
}

bool Service::configure(const ConnectionSettings &settings, const QByteArray &newToken, QString *error)
{
    if (!settings.validate(error))
        return false;
    if (!newToken.isEmpty() && !Credentials::write(settings.credentialTarget, newToken, error))
        return false;
    if (!saveGlobalConnection(settings, error))
        return false;
    stop();
    impl_->connection = settings;
    emit configurationChanged();
    return true;
}
} // namespace WinJACKNexus::Broadcast
