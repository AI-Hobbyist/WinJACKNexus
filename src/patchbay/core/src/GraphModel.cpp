/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <WinJACKNexus/Patchbay/Core/GraphModel.hpp>

#include <algorithm>

namespace WinJACKNexus::Patchbay::Core
{

namespace
{

template <typename Item, typename Predicate>
bool contains(const std::vector<Item> &items, Predicate predicate)
{
    return std::any_of(items.cbegin(), items.cend(), predicate);
}

}

void GraphModel::replace(Snapshot snapshot)
{
    snapshot.events.clear();

    for (const auto &client : snapshot.clients)
    {
        if (!contains(snapshot_.clients,
                      [&client](const ClientInfo &previous) { return previous.id == client.id; }))
            snapshot.events.push_back({SnapshotEventType::clientAdded, client.id});
    }
    for (auto &client : snapshot.clients)
    {
        const auto previous =
            std::find_if(snapshot_.clients.cbegin(), snapshot_.clients.cend(),
                         [&client](const ClientInfo &value) { return value.id == client.id; });
        if (previous == snapshot_.clients.cend())
            continue;

        client.x = previous->x;
        client.y = previous->y;
        client.split = previous->split;
        if (client.alias.empty())
            client.alias = previous->alias;
    }
    for (const auto &client : snapshot_.clients)
    {
        if (!contains(snapshot.clients,
                      [&client](const ClientInfo &current) { return current.id == client.id; }))
            snapshot.events.push_back({SnapshotEventType::clientRemoved, client.id});
    }
    for (const auto &port : snapshot.ports)
    {
        if (!contains(snapshot_.ports,
                      [&port](const PortInfo &previous) { return previous.id == port.id; }))
            snapshot.events.push_back({SnapshotEventType::portAdded, port.id, port.clientId});
    }
    for (const auto &port : snapshot_.ports)
    {
        if (!contains(snapshot.ports,
                      [&port](const PortInfo &current) { return current.id == port.id; }))
            snapshot.events.push_back({SnapshotEventType::portRemoved, port.id, port.clientId});
    }
    for (const auto &connection : snapshot.connections)
    {
        if (!contains(snapshot_.connections, [&connection](const ConnectionInfo &previous) {
                return previous.sourcePortId == connection.sourcePortId &&
                       previous.destinationPortId == connection.destinationPortId;
            }))
        {
            snapshot.events.push_back({SnapshotEventType::connectionAdded, connection.sourcePortId,
                                       connection.destinationPortId});
        }
    }
    for (const auto &connection : snapshot_.connections)
    {
        if (!contains(snapshot.connections, [&connection](const ConnectionInfo &current) {
                return current.sourcePortId == connection.sourcePortId &&
                       current.destinationPortId == connection.destinationPortId;
            }))
        {
            snapshot.events.push_back({SnapshotEventType::connectionRemoved,
                                       connection.sourcePortId, connection.destinationPortId});
        }
    }

    snapshot_ = std::move(snapshot);
}

const Snapshot &GraphModel::snapshot() const noexcept
{
    return snapshot_;
}

const ClientInfo *GraphModel::clientById(const std::string &id) const noexcept
{
    const auto item = std::find_if(snapshot_.clients.cbegin(), snapshot_.clients.cend(),
                                   [&id](const ClientInfo &client) { return client.id == id; });
    return item == snapshot_.clients.cend() ? nullptr : &*item;
}

const PortInfo *GraphModel::portById(const std::string &id) const noexcept
{
    const auto item = std::find_if(snapshot_.ports.cbegin(), snapshot_.ports.cend(),
                                   [&id](const PortInfo &port) { return port.id == id; });
    return item == snapshot_.ports.cend() ? nullptr : &*item;
}

const ConnectionInfo *GraphModel::connection(const std::string &sourcePortId,
                                             const std::string &destinationPortId) const noexcept
{
    const auto item =
        std::find_if(snapshot_.connections.cbegin(), snapshot_.connections.cend(),
                     [&sourcePortId, &destinationPortId](const ConnectionInfo &value) {
                         return value.sourcePortId == sourcePortId &&
                                value.destinationPortId == destinationPortId;
                     });
    return item == snapshot_.connections.cend() ? nullptr : &*item;
}

bool GraphModel::setClientSplit(const std::string &clientId, bool split) noexcept
{
    auto item =
        std::find_if(snapshot_.clients.begin(), snapshot_.clients.end(),
                     [&clientId](const ClientInfo &client) { return client.id == clientId; });
    if (item == snapshot_.clients.end())
        return false;
    item->split = split;
    return true;
}

}