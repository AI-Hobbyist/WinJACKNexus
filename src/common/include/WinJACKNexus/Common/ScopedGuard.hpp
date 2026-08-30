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