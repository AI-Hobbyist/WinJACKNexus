// SPDX-License-Identifier: AGPL-3.0-only
#include "PublishSession.hpp"
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrlQuery>
#include <algorithm>

namespace WinJACKNexus::Broadcast
{
PublishSession::PublishSession(QObject *parent)
    : QObject(parent), socket_(QString(), QWebSocketProtocol::VersionLatest, this)
{
    for (auto *timer : {&deadline_, &heartbeat_, &retry_})
        timer->setParent(this);
    deadline_.setSingleShot(true);
    retry_.setSingleShot(true);
    heartbeat_.setInterval(10000);
    socket_.setMaxAllowedIncomingFrameSize(65536);
    socket_.setMaxAllowedIncomingMessageSize(65536);
    connect(&deadline_, &QTimer::timeout, this, [this]
            { fail(QStringLiteral("retrying"), QStringLiteral("Publishing handshake timed out"), true); });
    connect(&retry_, &QTimer::timeout, this, [this] { connectNow(); });
    connect(&heartbeat_, &QTimer::timeout, this,
            [this]
            {
                if (activity_.elapsed() > 30000)
                    fail(QStringLiteral("retrying"), QStringLiteral("Publishing heartbeat timed out"), true);
                else
                    socket_.ping();
            });
    connect(&socket_, &QWebSocket::pong, this,
            [this](quint64 milliseconds, const QByteArray &)
            {
                activity_.restart();
                rtt_ = milliseconds;
            });
    connect(&socket_, &QWebSocket::textMessageReceived, this, &PublishSession::readMessage);
    connect(&socket_, &QWebSocket::authenticationRequired, this,
            [this]
            {
                fail(QStringLiteral("authentication-error"),
                     QStringLiteral("Publishing credentials were rejected"), false);
            });
    connect(&socket_, &QWebSocket::sslErrors, this, [this]
            { fail(QStringLiteral("error"), QStringLiteral("TLS certificate validation failed"), false); });
    connect(&socket_, &QWebSocket::errorOccurred, this,
            [this]
            {
                if (!wanted_ || handlingFailure_)
                    return;
                // QWebSocket exposes no HTTP response-status accessor. Its handshake
                // diagnostic carries the status; never copy the full diagnostic/request
                // (which can retain authorization data) into application logs.
                static const QRegularExpression rejection(
                    QStringLiteral("(?:http|HTTP)[^\\n]*\\b(401|409)\\b"));
                const auto status = rejection.match(socket_.errorString()).captured(1);
                // Qt's unsupported Bearer challenge diagnostic omits 401 and
                // does not emit authenticationRequired; the challenge marker
                // still identifies the terminal authentication rejection.
                if (status == QStringLiteral("401") ||
                    socket_.errorString().contains(QStringLiteral("WWW-Authenticate")))
                    fail(QStringLiteral("authentication-error"),
                         QStringLiteral("Publishing credentials were rejected"), false);
                else if (status == QStringLiteral("409"))
                    fail(QStringLiteral("identity-conflict"),
                         QStringLiteral("Publisher identity is already in use"), false);
                else
                    fail(QStringLiteral("retrying"), QStringLiteral("Publishing connection failed"), true);
            });
    connect(&socket_, &QWebSocket::disconnected, this,
            [this]
            {
                if (wanted_ && !handlingFailure_ && !retry_.isActive())
                    fail(QStringLiteral("retrying"), QStringLiteral("Publishing connection closed"), true);
            });
}

PublishSession::~PublishSession()
{
    changed = {};
    stop();
}

void PublishSession::start(const ConnectionSettings &settings, const QByteArray &token)
{
    stop();
    settings_ = settings;
    wanted_ = true;
    QString error;
    if (!settings.validate(&error) || token.isEmpty() || token.contains('\r') || token.contains('\n') ||
        token.contains('\0'))
    {
        fail(QStringLiteral("error"),
             error.isEmpty() ? QStringLiteral("Invalid publishing credential") : error, false);
        return;
    }
    token_ = token;
    retrySeconds_ = 1;
    connectNow();
}

void PublishSession::connectNow()
{
    if (!wanted_)
        return;
    auto url = settings_.serverBaseUrl;
    url.setScheme(url.scheme() == QStringLiteral("https") ? QStringLiteral("wss") : QStringLiteral("ws"));
    url.setPath(QStringLiteral("/bc/publish"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("publisher"), settings_.publisherId);
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + token_);
    state_ = QStringLiteral("connecting");
    error_.clear();
    deadline_.start(5000);
    socket_.open(request);
    notify();
}

void PublishSession::stop()
{
    wanted_ = false;
    deadline_.stop();
    heartbeat_.stop();
    retry_.stop();
    socket_.abort();
    token_.fill('\0');
    token_.clear();
    session_.clear();
    key_.fill('\0');
    key_.clear();
    udpPort_ = 0;
    state_ = QStringLiteral("stopped");
    error_.clear();
    notify();
}

void PublishSession::readMessage(const QString &message)
{
    if (!wanted_)
        return;
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(message.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        fail(QStringLiteral("error"), QStringLiteral("Invalid publishing control response"), false);
        return;
    }
    const auto object = document.object();
    if (state_ == QStringLiteral("connecting"))
    {
        static const QRegularExpression sessionPattern(QStringLiteral("\\A[0-9a-f]{32}\\z"));
        static const QRegularExpression keyPattern(QStringLiteral("\\A[0-9a-f]{64}\\z"));
        const auto session = object.value(QStringLiteral("session")).toString();
        const auto key = object.value(QStringLiteral("key")).toString();
        const auto port = object.value(QStringLiteral("udpPort")).toInt();
        if (object.value(QStringLiteral("type")) != QStringLiteral("hello") ||
            object.value(QStringLiteral("version")).toDouble() != 1 ||
            object.value(QStringLiteral("maxDatagram")).toDouble() != 1200 ||
            !sessionPattern.match(session).hasMatch() || !keyPattern.match(key).hasMatch() || port < 1 ||
            port > 65535)
        {
            fail(QStringLiteral("error"), QStringLiteral("Unsupported publishing session negotiation"),
                 false);
            return;
        }
        session_ = QByteArray::fromHex(session.toLatin1());
        key_ = QByteArray::fromHex(key.toLatin1());
        udpPort_ = static_cast<quint16>(port);
        state_ = QStringLiteral("online");
        deadline_.stop();
        activity_.start();
        heartbeat_.start();
        retrySeconds_ = 1;
        notify();
        if (ready)
            ready();
        return;
    }
    activity_.restart();
    if (received)
        received(object);
}

bool PublishSession::sendControl(const QJsonObject &message)
{
    if (state_ != QStringLiteral("online"))
        return false;
    const auto bytes = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (socket_.bytesToWrite() + bytes.size() > 65536)
    {
        fail(QStringLiteral("error"), QStringLiteral("Publishing control queue exceeded its budget"), false);
        return false;
    }
    return socket_.sendTextMessage(QString::fromUtf8(bytes)) >= 0;
}

void PublishSession::fail(const QString &state, const QString &error, bool retry)
{
    if (!wanted_ || handlingFailure_)
        return;
    handlingFailure_ = true;
    deadline_.stop();
    heartbeat_.stop();
    socket_.abort();
    session_.clear();
    key_.fill('\0');
    key_.clear();
    udpPort_ = 0;
    state_ = state;
    error_ = error;
    if (retry)
    {
        retry_.start(retrySeconds_ * 1000);
        retrySeconds_ = std::min(retrySeconds_ * 2, 30);
    }
    else
    {
        wanted_ = false;
        token_.fill('\0');
        token_.clear();
    }
    handlingFailure_ = false;
    notify();
}

void PublishSession::notify()
{
    if (changed)
        changed();
}

QString PublishSession::state() const
{
    return state_;
}
QString PublishSession::error() const
{
    return error_;
}
quint16 PublishSession::udpPort() const noexcept
{
    return udpPort_;
}
QHostAddress PublishSession::serverAddress() const
{
    return socket_.peerAddress();
}
const QByteArray &PublishSession::sessionId() const noexcept
{
    return session_;
}
const QByteArray &PublishSession::key() const noexcept
{
    return key_;
}
quint64 PublishSession::controlRttMilliseconds() const noexcept
{
    return rtt_;
}

} // namespace WinJACKNexus::Broadcast
