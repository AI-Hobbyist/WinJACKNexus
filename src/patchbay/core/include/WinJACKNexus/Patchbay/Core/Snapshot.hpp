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