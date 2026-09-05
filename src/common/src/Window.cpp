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
#include <WinJACKNexus/Common/Window.hpp>

namespace WinJACKNexus::Common::win32 {

Window::Window(HWND handle) noexcept
    : handle_(handle)
{
}

HWND Window::handle() const noexcept
{
    return handle_;
}

bool Window::isValid() const noexcept
{
    return handle_ != nullptr && ::IsWindow(handle_) != FALSE;
}

std::wstring Window::title() const
{
    if (!isValid())
        return {};

    const int length = ::GetWindowTextLengthW(handle_);
    if (length <= 0)
        return {};

    std::wstring result(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = ::GetWindowTextW(handle_, result.data(), length + 1);
    if (copied <= 0)
        return {};

    result.resize(static_cast<std::size_t>(copied));
    return result;
}

}