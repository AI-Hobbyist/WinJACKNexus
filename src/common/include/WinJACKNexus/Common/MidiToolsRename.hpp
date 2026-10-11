// Control-thread transaction seam; production backend and offline model share it.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <string>
#include <vector>
namespace WinJACKNexus::Common::MidiTools
{
enum class PortType
{
    midi,
    audio
};
struct PortIdentity
{
    std::string shortName;
    bool input{};
    PortType type{};
};
struct Connection
{
    std::string source, destination;
};
struct RenameResult
{
    enum Status
    {
        unchanged,
        applied,
        invalidName,
        snapshotFailed,
        createFailed,
        rollbackFailed,
        partial
    } status{};
    std::string actualName;
    std::vector<Connection> failed;
};
class ClientBackend
{
  public:
    virtual ~ClientBackend() = default;
    virtual std::string actualName() const = 0;
    virtual bool validName(const std::string &) const = 0;
    // Must atomically obtain a complete snapshot for the fixed port identities.
    virtual bool snapshot(const std::vector<PortIdentity> &, std::vector<Connection> &) = 0;
    // Stops this node's callbacks and releases voices before replacing its client.
    virtual bool replace(const std::string &, const std::vector<PortIdentity> &) = 0;
    virtual bool connect(const Connection &) = 0;
};
inline std::string remapEndpoint(const std::string &endpoint, const std::string &oldName,
                                 const std::string &newName, const std::vector<PortIdentity> &ports)
{
    for (const auto &p : ports)
    {
        if (endpoint == oldName + ":" + p.shortName)
        {
            return newName + ":" + p.shortName;
        }
    }
    return endpoint;
}
inline RenameResult renameClient(ClientBackend &backend, const std::vector<PortIdentity> &ports,
                                 const std::string &requested)
{
    const auto oldName = backend.actualName();
    RenameResult result{RenameResult::unchanged, oldName, {}};
    if (!backend.validName(requested))
    {
        result.status = RenameResult::invalidName;
        return result;
    }
    if (oldName == requested)
    {
        return result;
    }
    std::vector<Connection> saved;
    if (!backend.snapshot(ports, saved))
    {
        result.status = RenameResult::snapshotFailed;
        return result;
    }
    const bool created = backend.replace(requested, ports);
    if (!created && !backend.replace(oldName, ports))
    {
        result.status = RenameResult::rollbackFailed;
        result.actualName = backend.actualName();
        result.failed = saved;
        return result;
    }
    result.actualName = backend.actualName();
    result.status = created ? RenameResult::applied : RenameResult::createFailed;
    for (const auto &connection : saved)
    {
        Connection next{remapEndpoint(connection.source, oldName, result.actualName, ports),
                        remapEndpoint(connection.destination, oldName, result.actualName, ports)};
        if (!backend.connect(next))
        {
            result.failed.push_back(next);
        }
    }
    if (!result.failed.empty() && created)
    {
        result.status = RenameResult::partial;
    }
    return result;
}
} // namespace WinJACKNexus::Common::MidiTools
