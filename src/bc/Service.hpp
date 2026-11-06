// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Connection.hpp"
#include <QObject>
#include <WinJACKNexus/Common/BroadcastAudioStream.hpp>
#include <WinJACKNexus/Common/BroadcastState.hpp>
#include <memory>
#include <vector>

#ifdef WJN_BROADCAST_RUNTIME_BUILD
#define WJN_BC_API __declspec(dllexport)
#else
#define WJN_BC_API __declspec(dllimport)
#endif

namespace WinJACKNexus::Broadcast
{
struct Publication
{
    Common::Broadcast::Stream stream;
    QStringList category;
    std::shared_ptr<Common::Broadcast::AudioStream> audio;
};

struct ServiceStatus
{
    QString state = QStringLiteral("stopped");
    QString error;
    unsigned activeStreams = 0;
    quint64 sentBytes = 0, sentDatagrams = 0, droppedFrames = 0, staleFrames = 0;
    quint64 controlRttMilliseconds = 0;
};

// Control-thread facade shared by all node instances. Audio producers only use
// AudioStream; network, codecs and queue consumption belong to the worker.
class WJN_BC_API Service final : public QObject
{
    Q_OBJECT
  public:
    static Service &instance();
    ~Service() override;
    bool setPublications(const void *owner, std::vector<Publication> publications, QString *error = nullptr);
    void detach(const void *owner);
    bool start(const ConnectionSettings &settings, QString *error = nullptr);
    bool start(QString *error = nullptr);
    void stop();
    ServiceStatus status() const;
    ConnectionSettings connection() const;
    bool configure(const ConnectionSettings &settings, const QByteArray &newToken, QString *error = nullptr);

  signals:
    void statusChanged();
    void configurationChanged();

  private:
    Service();
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace WinJACKNexus::Broadcast
