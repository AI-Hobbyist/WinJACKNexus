// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <WinJACKNexus/Common/ClassicMixer.hpp>
#include <map>

namespace WinJACKNexus::Common::ClassicMixer
{
struct Connection
{
    QString source;
    QString destination;
};
class PortBackend
{
  public:
    virtual ~PortBackend() = default;
    virtual QString clientName() const = 0;
    virtual bool snapshot(std::vector<Connection> &connections) = 0;
    virtual bool rename(std::uint32_t id, const QString &name) = 0;
    virtual bool connect(const Connection &connection) = 0;
};
struct RenameResult
{
    bool success = false;
    bool rollbackComplete = true;
    std::vector<Connection> failedConnections;
    QString error;
};

inline QString mappedEndpoint(const QString &endpoint, const QString &client,
                              const std::vector<Port> &before, const std::vector<Port> &after)
{
    for (std::size_t index = 0; index < before.size(); ++index)
    {
        if (endpoint == client + ':' + before[index].name)
            return client + ':' + after[index].name;
    }
    return endpoint;
}

// Used by the production JACK backend and by offline failure-injection tests.
inline RenameResult renamePorts(PortBackend &backend, const std::vector<Port> &before,
                                const std::vector<Port> &after)
{
    RenameResult result;
    if (before.size() != after.size())
    {
        result.error = "Port topology changed";
        return result;
    }
    std::vector<std::size_t> changed;
    for (std::size_t index = 0; index < before.size(); ++index)
    {
        if (before[index].id != after[index].id || before[index].input != after[index].input)
        {
            result.error = "Port identity changed";
            return result;
        }
        if (before[index].name != after[index].name)
            changed.push_back(index);
    }
    if (changed.empty())
    {
        result.success = true;
        return result;
    }
    std::vector<Connection> saved;
    if (!backend.snapshot(saved))
    {
        result.error = "Connection snapshot failed";
        return result;
    }
    const auto client = backend.clientName();
    const auto moveNames = [&](const std::vector<Port> &target) {
        bool ok = true;
        // Temporary names permit swapping user names without transient collisions.
        for (const auto index : changed)
            ok = backend.rename(before[index].id,
                                QStringLiteral("__classic_mixer_%1").arg(before[index].id)) &&
                 ok;
        if (!ok)
            return false;
        for (const auto index : changed)
            ok = backend.rename(before[index].id, target[index].name) && ok;
        return ok;
    };
    const auto reconnect = [&](const std::vector<Port> &target) {
        bool ok = true;
        for (const auto &connection : saved)
        {
            Connection mapped{mappedEndpoint(connection.source, client, before, target),
                              mappedEndpoint(connection.destination, client, before, target)};
            if (!backend.connect(mapped))
            {
                result.failedConnections.push_back(mapped);
                ok = false;
            }
        }
        return ok;
    };
    if (moveNames(after) && reconnect(after))
    {
        result.success = true;
        return result;
    }
    result.error = "Port rename or reconnection failed";
    const bool namesRestored = moveNames(before);
    const bool connectionsRestored = reconnect(before);
    result.rollbackComplete = namesRestored && connectionsRestored;
    if (!result.rollbackComplete)
        result.error += "; rollback incomplete";
    return result;
}
} // namespace WinJACKNexus::Common::ClassicMixer
