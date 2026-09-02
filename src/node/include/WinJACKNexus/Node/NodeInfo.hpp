#pragma once

#include <WinJACKNexus/Node/Export.hpp>

namespace WinJACKNexus::Node {

struct WINJACK_NODE_API NodeInfo final {
    const char* name = "";
    const char* category = "";
    const char* version = "";
    const char* author = "";
    const char* description = "";
};

}