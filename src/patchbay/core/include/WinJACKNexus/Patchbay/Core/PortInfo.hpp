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