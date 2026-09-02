#pragma once

#include <WinJACKNexus/Patchbay/Core/Export.hpp>

#include <string>

namespace WinJACKNexus::Patchbay::Core {

struct WINJACK_PATCHBAY_API ConnectionInfo final {
    std::string sourcePortId;
    std::string destinationPortId;
    bool alive = true;
};

}