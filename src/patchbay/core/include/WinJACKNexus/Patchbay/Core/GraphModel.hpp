#pragma once

#include <WinJACKNexus/Patchbay/Core/Snapshot.hpp>

namespace WinJACKNexus::Patchbay::Core {

class WINJACK_PATCHBAY_API GraphModel final {
public:
    void replace(Snapshot snapshot);

    const Snapshot& snapshot() const noexcept;
    const ClientInfo* clientById(const std::string& id) const noexcept;
    const PortInfo* portById(const std::string& id) const noexcept;
    const ConnectionInfo* connection(const std::string& sourcePortId,
                                     const std::string& destinationPortId) const noexcept;
    bool setClientSplit(const std::string& clientId, bool split) noexcept;

private:
    Snapshot snapshot_;
};

}