// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QJsonObject>
#include <QStringList>
#include <WinJACKNexus/Common/BroadcastProcessor.hpp>
#include <vector>

namespace WinJACKNexus::Common::Broadcast
{

QString layoutName(Layout layout);
QStringList channelNames(Layout layout);

struct Category
{
    QString id;
    QString name;
    QString parent;
};

struct Stream
{
    QString id;
    QString name;
    QString category;
    Layout layout = Layout::stereo;
    double gainDb = 0;
    bool mute = false;
    // Session-only controls: never serialized or armed by project restore.
    bool listen = false;
    bool solo = false;

    Controls controls() const noexcept;
};

struct Port
{
    QString name;
    QString streamId;
    QString role;
    bool input;
    bool operator==(const Port &) const = default;
};

// Control-thread model. Call validate before applying a modified value to a node.
// This class contains no global server settings, credentials or live-ID registry.
struct State
{
    QString clientName = QStringLiteral("WJN Broadcast");
    std::vector<Category> categories;
    std::vector<Stream> streams;

    QString addCategory(const QString &name, const QString &parent = {});
    QString addStream(const QString &name, Layout layout, const QString &category = {});
    QStringList categoryPath(const QString &category) const;
    std::vector<Port> ports() const;
    bool validate(QString *error = nullptr) const;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &json, State &result, QString *error = nullptr);
};

} // namespace WinJACKNexus::Common::Broadcast
