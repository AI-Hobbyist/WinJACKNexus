#pragma once

#include <WinJACKNexus/Common/Export.hpp>

#include <Windows.h>

#include <string>

namespace WinJACKNexus::Common::win32 {

class WINJACK_COMMON_API Window final {
public:
    explicit Window(HWND handle = nullptr) noexcept;

    HWND handle() const noexcept;
    bool isValid() const noexcept;
    std::wstring title() const;

private:
    HWND handle_;
};

}