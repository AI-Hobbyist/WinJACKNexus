/* SPDX-License-Identifier: AGPL-3.0-only */
#include "State.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <cmath>

namespace WinJACKNexus::SignalSwitch
{
namespace
{
bool fail(QString *error, const char *message)
{
    if (error)
        *error = QString::fromLatin1(message);
    return false;
}

QJsonArray rectangle(const QRect &rect)
{
    return {rect.x(), rect.y(), rect.width(), rect.height()};
}

bool readRectangle(const QJsonValue &value, QRect *result)
{
    if (!value.isArray() || value.toArray().size() != 4)
        return false;
    const auto array = value.toArray();
    for (const auto &item : array)
    {
        if (!item.isDouble() || !std::isfinite(item.toDouble()) || item.toDouble() != item.toInt() ||
            std::abs(item.toDouble()) > 1000000)
            return false;
    }
    *result = QRect(array[0].toInt(), array[1].toInt(), array[2].toInt(), array[3].toInt());
    return true;
}

bool readMembers(const QJsonValue &value, QStringList *result)
{
    if (!value.isArray())
        return false;
    for (const auto &member : value.toArray())
    {
        if (!member.isString())
            return false;
        result->append(member.toString());
    }
    return true;
}
} // namespace

void State::add(const QString &id)
{
    if (cards.contains(id))
        return;
    Card card;
    card.geometry.moveTopLeft(QPoint(16, 16 + cards.size() * 64));
    cards.insert(id, card);
}

void State::remove(const QString &id)
{
    cards.remove(id);
    ungroup({id});
    makeIndependent({id});
}

bool State::setExclusive(const QString &id, const QStringList &members, QString *error)
{
    State candidate = *this;
    candidate.exclusive[id] = members;
    if (!candidate.validate(error))
        return false;
    *this = std::move(candidate);
    return true;
}

void State::makeIndependent(const QStringList &members)
{
    for (auto it = exclusive.begin(); it != exclusive.end();)
    {
        for (const auto &member : members)
            it->removeAll(member);
        if (it->size() < 2)
            it = exclusive.erase(it);
        else
            ++it;
    }
}

bool State::setGroup(const QString &id, const QString &name, const QStringList &members, QString *error)
{
    State candidate = *this;
    candidate.groups[id] = {name, members};
    if (!candidate.validate(error))
        return false;
    *this = std::move(candidate);
    return true;
}

void State::ungroup(const QStringList &members)
{
    for (auto it = groups.begin(); it != groups.end();)
    {
        for (const auto &member : members)
            it->members.removeAll(member);
        if (it->members.isEmpty())
            it = groups.erase(it);
        else
            ++it;
    }
}

bool State::resolve(const QMap<QString, bool> &current, const QStringList &selection, bool enabled,
                    QMap<QString, bool> *result, QString *error) const
{
    if (!result || selection.isEmpty())
        return fail(error, "Empty selection");
    auto next = current;
    for (const auto &id : selection)
    {
        if (!cards.contains(id))
            return fail(error, "Unknown switch");
        next[id] = enabled;
    }
    if (enabled)
    {
        for (const auto &members : exclusive)
        {
            QString winner;
            for (const auto &id : selection)
            {
                if (!members.contains(id))
                    continue;
                if (!winner.isEmpty() && winner != id)
                    return fail(error, "Cannot enable multiple exclusive switches");
                winner = id;
            }
            if (!winner.isEmpty())
            {
                for (const auto &id : members)
                    next[id] = id == winner;
            }
        }
    }
    *result = std::move(next);
    return true;
}

QPoint State::snapped(QPoint point) const
{
    if (!grid)
        return point;
    return {qRound(double(point.x()) / gridX) * gridX, qRound(double(point.y()) / gridY) * gridY};
}

bool State::validate(QString *error) const
{
    if (gridX < 1 || gridY < 1 || gridX > 4096 || gridY > 4096 || window.opacity < 10 || window.opacity > 100 ||
        window.geometry.width() < 160 || window.geometry.height() < 100 || window.geometry.width() > 1000000 ||
        window.geometry.height() > 1000000 || !QStringList{"", "left", "right", "top", "bottom"}.contains(window.edge))
        return fail(error, "Invalid window or grid settings");
    for (auto it = cards.begin(); it != cards.end(); ++it)
    {
        if (it.key().isEmpty() || it->geometry.width() < 28 || it->geometry.height() < 28 ||
            it->geometry.width() > 1000000 || it->geometry.height() > 1000000)
            return fail(error, "Invalid card geometry");
    }
    auto validateMembers = [&](const QStringList &members, QSet<QString> &used) {
        for (const auto &id : members)
        {
            if (!cards.contains(id) || used.contains(id))
                return false;
            used.insert(id);
        }
        return true;
    };
    QSet<QString> grouped;
    for (auto it = groups.begin(); it != groups.end(); ++it)
    {
        if (it.key().isEmpty() || it->name.trimmed().isEmpty() || it->members.isEmpty() ||
            !validateMembers(it->members, grouped))
            return fail(error, "Invalid or overlapping layout group");
    }
    QSet<QString> related;
    for (auto it = exclusive.begin(); it != exclusive.end(); ++it)
    {
        if (it.key().isEmpty() || it->size() < 2 || !validateMembers(*it, related))
            return fail(error, "Invalid or overlapping exclusive relation");
    }
    return true;
}

QJsonObject State::toJson() const
{
    QJsonObject cardObjects;
    for (auto it = cards.begin(); it != cards.end(); ++it)
    {
        QJsonObject card{{"geometry", rectangle(it->geometry)}};
        if (it->color.isValid())
            card["color"] = it->color.name(QColor::HexArgb);
        cardObjects[it.key()] = card;
    }
    QJsonObject groupObjects;
    for (auto it = groups.begin(); it != groups.end(); ++it)
        groupObjects[it.key()] = QJsonObject{{"name", it->name}, {"members", QJsonArray::fromStringList(it->members)}};
    QJsonObject relations;
    for (auto it = exclusive.begin(); it != exclusive.end(); ++it)
        relations[it.key()] = QJsonArray::fromStringList(*it);
    return {{"version", 1},
            {"cards", cardObjects},
            {"groups", groupObjects},
            {"exclusive", relations},
            {"locked", locked},
            {"grid", grid},
            {"gridX", gridX},
            {"gridY", gridY},
            {"window", QJsonObject{{"geometry", rectangle(window.geometry)},
                                   {"frameless", window.frameless},
                                   {"topmost", window.topmost},
                                   {"autoHide", window.autoHide},
                                   {"opacity", window.opacity},
                                   {"screen", window.screen},
                                   {"edge", window.edge}}}};
}

QString State::toBase64() const
{
    return QString::fromLatin1(QJsonDocument(toJson()).toJson(QJsonDocument::Compact).toBase64());
}

bool State::fromBase64(const QString &encoded, State *result, QString *error)
{
    const auto decoded = QByteArray::fromBase64Encoding(encoded.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!result || !decoded)
        return fail(error, "Invalid Signal Switch Base64");
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(decoded.decoded, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, "Invalid Signal Switch JSON");
    const auto object = document.object();
    if (object.value("version") != 1 || !object.value("cards").isObject() || !object.value("groups").isObject() ||
        !object.value("exclusive").isObject() || !object.value("window").isObject() ||
        !object.value("locked").isBool() || !object.value("grid").isBool() || !object.value("gridX").isDouble() ||
        !object.value("gridY").isDouble())
        return fail(error, "Unsupported or malformed Signal Switch state");
    State next;
    next.locked = object["locked"].toBool();
    next.grid = object["grid"].toBool();
    next.gridX = object["gridX"].toInt();
    next.gridY = object["gridY"].toInt();
    if (object["gridX"].toDouble() != next.gridX || object["gridY"].toDouble() != next.gridY)
        return fail(error, "Invalid grid interval");
    const auto cards = object["cards"].toObject();
    for (auto it = cards.begin(); it != cards.end(); ++it)
    {
        const auto item = it->toObject();
        Card card;
        if (!it->isObject() || !readRectangle(item["geometry"], &card.geometry))
            return fail(error, "Invalid card");
        if (item.contains("color"))
        {
            card.color = QColor(item["color"].toString());
            if (!item["color"].isString() || !card.color.isValid())
                return fail(error, "Invalid card color");
        }
        next.cards[it.key()] = card;
    }
    const auto groups = object["groups"].toObject();
    for (auto it = groups.begin(); it != groups.end(); ++it)
    {
        const auto item = it->toObject();
        Group group;
        if (!it->isObject() || !item["name"].isString() || !readMembers(item["members"], &group.members))
            return fail(error, "Invalid group");
        group.name = item["name"].toString();
        next.groups[it.key()] = group;
    }
    const auto relations = object["exclusive"].toObject();
    for (auto it = relations.begin(); it != relations.end(); ++it)
    {
        QStringList members;
        if (!readMembers(*it, &members))
            return fail(error, "Invalid relation");
        next.exclusive[it.key()] = members;
    }
    const auto window = object["window"].toObject();
    if (!readRectangle(window["geometry"], &next.window.geometry) || !window["frameless"].isBool() ||
        !window["topmost"].isBool() || !window["autoHide"].isBool() || !window["opacity"].isDouble() ||
        !window["screen"].isString() || !window["edge"].isString())
        return fail(error, "Invalid window");
    next.window.frameless = window["frameless"].toBool();
    next.window.topmost = window["topmost"].toBool();
    next.window.autoHide = window["autoHide"].toBool();
    next.window.opacity = window["opacity"].toInt();
    next.window.screen = window["screen"].toString();
    next.window.edge = window["edge"].toString();
    if (window["opacity"].toDouble() != next.window.opacity || !next.validate(error))
        return fail(error, "Invalid Signal Switch values");
    *result = std::move(next);
    return true;
}
} // namespace WinJACKNexus::SignalSwitch
