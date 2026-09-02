#pragma once

#include <WinJACKNexus/Patchbay/Core/GraphModel.hpp>

#include <string>
#include <vector>

namespace WinJACKNexus::Patchbay::Core {

class WINJACK_PATCHBAY_API SearchModel final {
public:
    static std::vector<std::string> matchingClientIds(const GraphModel& model, const std::string& query);
    static std::vector<std::string> matchingPortIds(const GraphModel& model, const std::string& query);
};

}