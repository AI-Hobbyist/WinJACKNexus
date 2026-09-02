#pragma once

#include <WinJACKNexus/Patchbay/Core/Export.hpp>

#include <string>

namespace WinJACKNexus::Patchbay::Core {

struct WINJACK_PATCHBAY_API ClientInfo final {
    std::string id;
    std::string name;
    std::string alias;
    std::string iconName;
    std::string uri;
    double x = 0.0;
    double y = 0.0;
    bool split = false;
    bool alive = true;
};

}