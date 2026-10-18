// SPDX-License-Identifier: AGPL-3.0-only
// Extend the existing offline JACK fixture with real connection bookkeeping.
#define jack_port_get_connections fixture_base_connections
#define jack_connect fixture_base_connect
#define jack_disconnect fixture_base_disconnect
#define jack_port_rename fixture_base_rename
#define jack_port_unregister fixture_base_unregister
#define jack_client_close fixture_base_close
#include "SubGraphJackFixture.cpp"
#undef jack_port_get_connections
#undef jack_connect
#undef jack_disconnect
#undef jack_port_rename
#undef jack_port_unregister
#undef jack_client_close
#include <cerrno>

namespace
{
std::vector<std::pair<std::string, std::string>> savedConnections;
int renameAttempts = 0;
int failRenameAt = -1;
} // namespace
extern "C"
{
    __declspec(dllexport) void wjn_classic_fail_rename(int relativeCall)
    {
        failRenameAt = relativeCall < 0 ? -1 : renameAttempts + relativeCall;
    }
    const char **jack_port_get_connections(const jack_port_t *port)
    {
        std::vector<const char *> names;
        for (const auto &[source, destination] : savedConnections)
        {
            if (source == port->name)
                names.push_back(destination.c_str());
            if (destination == port->name)
                names.push_back(source.c_str());
        }
        if (names.empty())
            return nullptr;
        auto **result = static_cast<const char **>(std::calloc(names.size() + 1, sizeof(char *)));
        std::copy(names.begin(), names.end(), result);
        return result;
    }
    int jack_connect(jack_client_t *, const char *source, const char *destination)
    {
        if (!jack_port_by_name(nullptr, source) || !jack_port_by_name(nullptr, destination))
            return ENOENT;
        const auto connection = std::make_pair(std::string(source), std::string(destination));
        if (std::find(savedConnections.begin(), savedConnections.end(), connection) !=
            savedConnections.end())
            return EEXIST;
        savedConnections.push_back(connection);
        return 0;
    }
    int jack_disconnect(jack_client_t *, const char *source, const char *destination)
    {
        std::erase(savedConnections, std::make_pair(std::string(source), std::string(destination)));
        return 0;
    }
    int jack_port_rename(jack_client_t *client, jack_port_t *port, const char *name)
    {
        if (++renameAttempts == failRenameAt)
            return EINVAL;
        const auto old = port->name;
        const auto next = client->name + ":" + name;
        if (const auto *existing = jack_port_by_name(nullptr, next.c_str());
            existing && existing != port)
            return EEXIST;
        for (auto &[source, destination] : savedConnections)
        {
            if (source == old)
                source = next;
            if (destination == old)
                destination = next;
        }
        return fixture_base_rename(client, port, name);
    }
    int jack_port_unregister(jack_client_t *client, jack_port_t *port)
    {
        const auto name = port->name;
        std::erase_if(savedConnections, [&](const auto &connection) {
            return connection.first == name || connection.second == name;
        });
        return fixture_base_unregister(client, port);
    }
    int jack_client_close(jack_client_t *client)
    {
        const auto prefix = client->name + ':';
        std::erase_if(savedConnections, [&](const auto &connection) {
            return connection.first.starts_with(prefix) || connection.second.starts_with(prefix);
        });
        return fixture_base_close(client);
    }
}
