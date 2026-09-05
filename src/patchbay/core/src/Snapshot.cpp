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
#include <WinJACKNexus/Patchbay/Core/Snapshot.hpp>

#include <WinJACKNexus/Audio/JackApi.hpp>
#include <WinJACKNexus/Audio/JackClient.hpp>

#include <algorithm>
#include <cstring>
#include <string_view>

namespace WinJACKNexus::Patchbay::Core {

namespace {

std::pair<std::string, std::string> splitPortName(const char* fullName)
{
    const std::string value = fullName != nullptr ? fullName : "";
    const auto separator = value.find(':');
    return separator == std::string::npos
        ? std::pair { value, value }
        : std::pair { value.substr(0, separator), value.substr(separator + 1) };
}

PortKind portKind(const char* type)
{
    if (type == nullptr)
        return PortKind::unknown;
    if (std::strcmp(type, JACK_DEFAULT_AUDIO_TYPE) == 0)
        return PortKind::audio;
    if (std::strcmp(type, JACK_DEFAULT_MIDI_TYPE) == 0)
        return PortKind::midi;
    return PortKind::unknown;
}

Direction direction(int flags)
{
    if ((flags & JackPortIsInput) != 0)
        return Direction::input;
    if ((flags & JackPortIsOutput) != 0)
        return Direction::output;
    return Direction::unknown;
}

}

Snapshot Snapshot::capture(Audio::JackClient& client)
{
    Snapshot snapshot;
    if (!client.isOpen())
        return snapshot;

    auto& api = Audio::JackApi::instance();
    const auto** names = api.getPorts(client.nativeHandle(), nullptr, nullptr, 0);
    if (names == nullptr)
        return snapshot;

    for (const auto** name = names; *name != nullptr; ++name) {
        const auto* fullName = *name;
        const auto [clientId, portName] = splitPortName(fullName);
        if (std::none_of(snapshot.clients.cbegin(), snapshot.clients.cend(), [&clientId](const ClientInfo& item) {
                return item.id == clientId;
            })) {
            snapshot.clients.push_back(ClientInfo { .id = clientId, .name = clientId });
        }

        auto* nativePort = api.portByName(client.nativeHandle(), fullName);
        if (nativePort == nullptr)
            continue;

        const auto portId = std::string(fullName);
        snapshot.ports.push_back(PortInfo {
            .id = portId,
            .clientId = clientId,
            .name = portName,
            .kind = portKind(api.portType(nativePort)),
            .direction = direction(api.portFlags(nativePort)),
            .ordinal = static_cast<std::uint32_t>(snapshot.ports.size())
        });

        if ((api.portFlags(nativePort) & JackPortIsOutput) == 0)
            continue;
        const auto** destinations = api.portGetConnections(nativePort);
        if (destinations == nullptr)
            continue;
        for (const auto** destination = destinations; *destination != nullptr; ++destination) {
            snapshot.connections.push_back(ConnectionInfo { .sourcePortId = portId, .destinationPortId = *destination });
        }
        api.freeMemory(const_cast<void*>(static_cast<const void*>(destinations)));
    }
    api.freeMemory(const_cast<void*>(static_cast<const void*>(names)));
    return snapshot;
}

}