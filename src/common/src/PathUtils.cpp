#include <WinJACKNexus/Common/PathUtils.hpp>

#include <Windows.h>

#include <cstdlib>
#include <system_error>

namespace WinJACKNexus::Common {

std::filesystem::path PathUtils::executablePath()
{
    std::wstring buffer(256, L'\0');
    for (;;) {
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
            return {};
        if (length < buffer.size() - 1) {
            buffer.resize(length);
            return std::filesystem::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::filesystem::path PathUtils::moduleDirectory()
{
    const auto path = executablePath();
    return path.empty() ? std::filesystem::path {} : path.parent_path();
}

std::filesystem::path PathUtils::localAppDataDirectory()
{
    wchar_t* value = nullptr;
    std::size_t length = 0;
    if (_wdupenv_s(&value, &length, L"LOCALAPPDATA") != 0 || value == nullptr)
        return {};

    const std::filesystem::path result = std::filesystem::path(value) / L"WinJACKNexus";
    std::free(value);
    std::error_code error;
    std::filesystem::create_directories(result, error);
    return result;
}

std::filesystem::path PathUtils::logDirectory()
{
    const auto result = localAppDataDirectory() / L"logs";
    std::error_code error;
    std::filesystem::create_directories(result, error);
    return result;
}

std::filesystem::path PathUtils::normalize(const std::filesystem::path& path)
{
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    if (error)
        return path.lexically_normal();
    return absolute.lexically_normal();
}

}