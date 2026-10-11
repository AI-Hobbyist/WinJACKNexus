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

namespace WinJACKNexus::Common
{

class WINJACK_COMMON_API Thread final
{
  public:
    using EntryPoint = void (*)(Thread &) noexcept;

    explicit Thread(const wchar_t *name = nullptr);
    ~Thread() noexcept;

    Thread(const Thread &) = delete;
    Thread &operator=(const Thread &) = delete;

    bool start(EntryPoint entryPoint) noexcept;
    void requestStop() noexcept;
    bool stopRequested() const noexcept;
    bool join() noexcept;
    bool joinable() const noexcept;
    bool setRealtimePriority() noexcept;

    HANDLE nativeHandle() const noexcept;
    DWORD threadId() const noexcept;

  private:
    static DWORD WINAPI threadEntry(void *parameter) noexcept;

    struct Impl;
    Impl *impl_;
};

}