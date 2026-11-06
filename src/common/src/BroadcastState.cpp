// SPDX-License-Identifier: AGPL-3.0-only
#include <QJsonArray>
#include <QSet>
#include <QUuid>
#include <WinJACKNexus/Common/BroadcastState.hpp>
#include <algorithm>
#include <cmath>

namespace WinJACKNexus::Common::Broadcast
{
namespace
{
bool fail(QString *error, const char *message)
{
    if (error)
        *error = QString::fromUtf8(message);
    return false;
}

bool validName(const QString &name)
{
    return !name.trimmed().isEmpty() && !name.contains(QChar::Null) && name.toUtf8().size() <= 1024;
}

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool validId(const QString &id)
{
    const QUuid uuid(id);
    return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id;
}

const Category *findCategory(const State &state, const QString &id)
{
    const auto found = std::find_if(state.categories.begin(), state.categories.end(),
                                    [&](const Category &category) { return category.id == id; });
    return found == state.categories.end() ? nullptr : &*found;
}
} // namespace

QString layoutName(Layout layout)
{
    switch (layout)
    {
    case Layout::mono:
        return QStringLiteral("mono");
    case Layout::stereo:
        return QStringLiteral("stereo");
    case Layout::surround51:
        return QStringLiteral("5.1");
    case Layout::surround71:
        return QStringLiteral("7.1");
    }
    return {};
}

QStringList channelNames(Layout layout)
{
    if (layout == Layout::mono)
        return {QStringLiteral("M")};
    const QStringList names{QStringLiteral("L"),   QStringLiteral("R"),  QStringLiteral("C"),
                            QStringLiteral("LFE"), QStringLiteral("Ls"), QStringLiteral("Rs"),
                            QStringLiteral("Lrb"), QStringLiteral("Rrb")};
    return layoutName(layout).isEmpty() ? QStringList{} : names.mid(0, channelCount(layout));
}

Controls Stream::controls() const noexcept
{
    return {layout, static_cast<float>(std::pow(10.0, gainDb / 20.0)), mute, listen, solo};
}

QString State::addCategory(const QString &name, const QString &parent)
{
    const auto id = newId();
    categories.push_back({id, name, parent});
    if (!validate())
    {
        categories.pop_back();
        return {};
    }
    return id;
}

QString State::addStream(const QString &name, Layout layout, const QString &category)
{
    const auto id = newId();
    Stream stream;
    stream.id = id;
    stream.name = name;
    stream.category = category;
    stream.layout = layout;
    streams.push_back(std::move(stream));
    if (!validate())
    {
        streams.pop_back();
        return {};
    }
    return id;
}

QStringList State::categoryPath(const QString &category) const
{
    QStringList path;
    QSet<QString> visited;
    auto id = category;
    while (!id.isEmpty())
    {
        const auto *entry = findCategory(*this, id);
        if (!entry || visited.contains(id) || visited.size() >= 32)
            return {};
        visited.insert(id);
        path.prepend(entry->name);
        id = entry->parent;
    }
    return path;
}

std::vector<Port> State::ports() const
{
    std::vector<Port> result;
    for (const auto &stream : streams)
    {
        auto prefix = stream.id;
        prefix.remove(QLatin1Char('-'));
        for (const bool input : {true, false})
            for (const auto &role : channelNames(stream.layout))
                result.push_back(
                    {QStringLiteral("bc_%1_%2_%3")
                         .arg(prefix, input ? QStringLiteral("in") : QStringLiteral("out"), role),
                     stream.id, role, input});
    }
    return result;
}

bool State::validate(QString *error) const
{
    if (!validName(clientName) || clientName.toUtf8().size() > 63 || clientName.contains(QLatin1Char(':')))
        return fail(error, "Invalid JACK client name");
    if (streams.size() > 64)
        return fail(error, "At most 64 streams are supported per publishing session");
    QSet<QString> ids;
    for (const auto &category : categories)
    {
        if (!validId(category.id) || ids.contains(category.id) || !validName(category.name))
            return fail(error, "Invalid category identity or name");
        ids.insert(category.id);
        if (categoryPath(category.id).isEmpty())
            return fail(error, "Missing category parent, cycle or category depth exceeds 32");
    }
    for (const auto &stream : streams)
    {
        if (!validId(stream.id) || ids.contains(stream.id) || !validName(stream.name) ||
            layoutName(stream.layout).isEmpty() || !std::isfinite(stream.gainDb) || stream.gainDb < -96 ||
            stream.gainDb > 12 || (!stream.category.isEmpty() && !findCategory(*this, stream.category)))
            return fail(error, "Invalid stream identity, layout, category or gain");
        ids.insert(stream.id);
    }
    return true;
}

QJsonObject State::toJson() const
{
    QJsonArray categoryValues, streamValues;
    for (const auto &category : categories)
        categoryValues.append(QJsonObject{{QStringLiteral("id"), category.id},
                                          {QStringLiteral("name"), category.name},
                                          {QStringLiteral("parent"), category.parent}});
    for (const auto &stream : streams)
        streamValues.append(QJsonObject{{QStringLiteral("id"), stream.id},
                                        {QStringLiteral("name"), stream.name},
                                        {QStringLiteral("category"), stream.category},
                                        {QStringLiteral("layout"), layoutName(stream.layout)},
                                        {QStringLiteral("gainDb"), stream.gainDb},
                                        {QStringLiteral("mute"), stream.mute}});
    return {{QStringLiteral("version"), 1},
            {QStringLiteral("clientName"), clientName},
            {QStringLiteral("categories"), categoryValues},
            {QStringLiteral("streams"), streamValues}};
}

bool State::fromJson(const QJsonObject &json, State &result, QString *error)
{
    if (json.value(QStringLiteral("version")).toDouble(-1) != 1 ||
        !json.value(QStringLiteral("clientName")).isString() ||
        !json.value(QStringLiteral("categories")).isArray() ||
        !json.value(QStringLiteral("streams")).isArray())
        return fail(error, "Invalid broadcast state schema");
    State candidate;
    candidate.clientName = json.value(QStringLiteral("clientName")).toString();
    for (const auto &value : json.value(QStringLiteral("categories")).toArray())
    {
        const auto category = value.toObject();
        candidate.categories.push_back({category.value(QStringLiteral("id")).toString(),
                                        category.value(QStringLiteral("name")).toString(),
                                        category.value(QStringLiteral("parent")).toString()});
    }
    for (const auto &value : json.value(QStringLiteral("streams")).toArray())
    {
        const auto object = value.toObject();
        Stream stream;
        stream.id = object.value(QStringLiteral("id")).toString();
        stream.name = object.value(QStringLiteral("name")).toString();
        stream.category = object.value(QStringLiteral("category")).toString();
        const auto layout = object.value(QStringLiteral("layout")).toString();
        bool found = false;
        for (const auto allowed : {Layout::mono, Layout::stereo, Layout::surround51, Layout::surround71})
            if (layout == layoutName(allowed))
            {
                stream.layout = allowed;
                found = true;
                break;
            }
        if (!found || !object.value(QStringLiteral("gainDb")).isDouble() ||
            !object.value(QStringLiteral("mute")).isBool())
            return fail(error, "Invalid broadcast stream schema");
        stream.gainDb = object.value(QStringLiteral("gainDb")).toDouble();
        stream.mute = object.value(QStringLiteral("mute")).toBool();
        candidate.streams.push_back(std::move(stream));
    }
    if (!candidate.validate(error))
        return false;
    result = std::move(candidate);
    return true;
}

} // namespace WinJACKNexus::Common::Broadcast
