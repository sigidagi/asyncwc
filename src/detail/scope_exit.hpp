#pragma once

#include <utility>

namespace asyncwc::detail {

/// Runs a callable when leaving the scope (a minimal `std::scope_exit`).
template <typename F>
class [[nodiscard]] ScopeExit {
public:
    explicit ScopeExit(F function) noexcept : function_{std::move(function)} {}

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit(ScopeExit&&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    ScopeExit& operator=(ScopeExit&&) = delete;

    ~ScopeExit() { function_(); }

private:
    F function_;
};

} // namespace asyncwc::detail
