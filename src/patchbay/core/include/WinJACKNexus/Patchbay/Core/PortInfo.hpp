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

#include <WinJACKNexus/Patchbay/Core/Export.hpp>
#include <WinJACKNexus/Patchbay/Core/Types.hpp>

#include <string>

namespace WinJACKNexus::Patchbay::Core {

struct WINJACK_PATCHBAY_API PortInfo final {
    std::string id;
    std::string clientId;
    std::string name;
    std::string alias;
    PortKind kind = PortKind::unknown;
    Direction direction = Direction::unknown;
    PortGroupHint groupHints = PortGroupHint::none;
    std::uint32_t ordinal = 0;
    bool alive = true;
};

}