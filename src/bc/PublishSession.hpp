// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Connection.hpp"
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QWebSocket>
#include <functional>

namespace WinJACKNexus::Broadcast
{

// Lives entirely on the publishing worker's event-loop thread, never the JACK
// callback. One process-global publisher owns this control connection.
class PublishSession final : public QObject
{
  public:
    explicit PublishSession(QObject *parent = nullptr);
    ~PublishSession() override;
    void start(const ConnectionSettings &settings, const QByteArray &token);
    void stop();
    bool sendControl(const QJsonObject &message);

    QString state() const;
    QString error() const;
    quint16 udpPort() const noexcept;
    QHostAddress serverAddress() const;
    const QByteArray &sessionId() const noexcept;
    const QByteArray &key() const noexcept;
    quint64 controlRttMilliseconds() const noexcept;

    std::function<void()> changed;
    std::function<void()> ready;
    std::function<void(const QJsonObject &)> received;

  private:
    QWebSocket socket_;
    QTimer deadline_, heartbeat_, retry_;
    QElapsedTimer activity_;
    ConnectionSettings settings_;
    QByteArray token_, session_, key_;
    QString state_ = QStringLiteral("stopped"), error_;
    quint16 udpPort_ = 0;
    quint64 rtt_ = 0;
    int retrySeconds_ = 1;
    bool wanted_ = false, handlingFailure_ = false;

    void connectNow();
    void readMessage(const QString &message);
    void fail(const QString &state, const QString &error, bool retry);
    void notify();
};

} // namespace WinJACKNexus::Broadcast
