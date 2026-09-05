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
#pragma once

#include <WinJACKNexus/Patchbay/Core/ClientInfo.hpp>
#include <WinJACKNexus/Patchbay/Core/ConnectionInfo.hpp>
#include <WinJACKNexus/Patchbay/Core/PortInfo.hpp>

#include <vector>

namespace WinJACKNexus::Audio {
class JackClient;
}

namespace WinJACKNexus::Patchbay::Core {

enum class SnapshotEventType : unsigned char {
    clientAdded,
    clientRemoved,
    clientRenamed,
    clientDataChanged,
    portAdded,
    portRemoved,
    connectionAdded,
    connectionRemoved
};

struct WINJACK_PATCHBAY_API SnapshotEvent final {
    SnapshotEventType type = SnapshotEventType::clientAdded;
    std::string subjectId;
    std::string relatedId;
};

struct WINJACK_PATCHBAY_API Snapshot final {
    std::vector<ClientInfo> clients;
    std::vector<PortInfo> ports;
    std::vector<ConnectionInfo> connections;
    std::vector<SnapshotEvent> events;

    static Snapshot capture(Audio::JackClient& client);
};

}