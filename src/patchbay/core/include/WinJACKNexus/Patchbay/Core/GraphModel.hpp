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