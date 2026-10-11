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
#pragma once

#include <WinJACKNexus/Common/Export.hpp>

#include <Windows.h>

#include <string>

namespace WinJACKNexus::Common::win32
{

class WINJACK_COMMON_API Window final
{
  public:
    explicit Window(HWND handle = nullptr) noexcept;

    HWND handle() const noexcept;
    bool isValid() const noexcept;
    std::wstring title() const;

  private:
    HWND handle_;
};

}