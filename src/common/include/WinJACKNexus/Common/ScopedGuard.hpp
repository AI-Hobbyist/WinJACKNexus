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

#include <functional>
#include <type_traits>
#include <utility>

namespace WinJACKNexus::Common {

template <typename Function>
class ScopeExit final {
public:
    explicit ScopeExit(Function function) noexcept(std::is_nothrow_move_constructible_v<Function>)
        : function_(std::move(function))
    {
    }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

    ScopeExit(ScopeExit&& other) noexcept(std::is_nothrow_move_constructible_v<Function>)
        : function_(std::move(other.function_))
        , active_(other.active_)
    {
        other.active_ = false;
    }

    ~ScopeExit() noexcept
    {
        if (active_)
            function_();
    }

    void dismiss() noexcept
    {
        active_ = false;
    }

private:
    Function function_;
    bool active_ { true };
};

template <typename Function>
ScopeExit(Function&&) -> ScopeExit<std::decay_t<Function>>;

template <typename Function>
auto makeScopeExit(Function&& function)
{
    using DecayedFunction = std::decay_t<Function>;
    return ScopeExit<DecayedFunction>(std::forward<Function>(function));
}

template <typename Function>
using ScopedGuard = ScopeExit<Function>;

}