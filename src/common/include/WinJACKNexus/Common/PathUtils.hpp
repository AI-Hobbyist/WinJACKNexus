#pragma once

#include <WinJACKNexus/Common/Export.hpp>

#include <filesystem>

namespace WinJACKNexus::Common {

class WINJACK_COMMON_API PathUtils final {
public:
    static std::filesystem::path executablePath();
    static std::filesystem::path moduleDirectory();
    static std::filesystem::path localAppDataDirectory();
    static std::filesystem::path logDirectory();
    static std::filesystem::path normalize(const std::filesystem::path& path);
};

}