#pragma once

#include <type_traits>
#include <memory>
#include <cassert>
#include <coroutine>

namespace wscoro {

namespace detail {
  template<typename F> requires std::is_nothrow_invocable_v<F>
  struct ScopeExit {
    F f;
    constexpr ScopeExit(F f) noexcept : f(std::move(f)) {}
    ~ScopeExit() { f(); }
  };

  // Use operator&& on a lambda.
  struct ScopeExitHelper {
    template<typename F>
    constexpr ScopeExit<F> operator&&(F f) const noexcept {
      return ScopeExit<F>(std::move(f));
    }
  };
} // namespace detail

template<typename F>
inline constexpr static detail::ScopeExit<F> scope_exit(F f) noexcept {
  return {std::move(f)};
}

inline constexpr static detail::ScopeExitHelper scope_exit() noexcept {
  return {};
}

} // namespace wscoro
