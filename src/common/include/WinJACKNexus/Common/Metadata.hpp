#pragma once

#include <WinJACKNexus/Common/Export.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace WinJACKNexus::Common {

struct WINJACK_COMMON_API ModuleMetadata final {
    std::string name;
    std::string category;
    std::string version;
    std::string author;
    std::string description;
    std::vector<std::string> dependencies;

    std::string toJson() const;
    static std::optional<ModuleMetadata> fromJson(std::string_view json);
};

}