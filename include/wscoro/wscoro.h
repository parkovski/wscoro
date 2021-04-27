#pragma once

#include <type_traits>
#include <memory>
#include <cassert>

#if !defined(WSCORO_STD_EXPERIMENTAL)
# // Both clang (11) and gcc (10) need experimental headers, MSVC needs
# // non-experimental.
# if defined(__GNUC__) || defined(__clang__) || !__has_include(<coroutine>)
#  define WSCORO_STD_EXPERIMENTAL
# endif
#endif

namespace wscoro {
  namespace detail {
    template< class T, class U >
    concept SameHelper = std::is_same_v<T, U>;
  }
  namespace std_ {
    template<class T, class U>
    concept same_as = detail::SameHelper<T, U> && detail::SameHelper<U, T>;
  }
}

#ifdef WSCORO_STD_EXPERIMENTAL
# include <experimental/coroutine>
namespace wscoro::std_ {
  template<class P = void>
  using coroutine_handle = std::experimental::coroutine_handle<P>;

  using std::experimental::suspend_never;
  using std::experimental::suspend_always;

  using std::experimental::noop_coroutine;
}
#else
# include <coroutine>
namespace wscoro::std_ {
  template<class P = void>
  using coroutine_handle = std::coroutine_handle<P>;

  using std::suspend_never;
  using std::suspend_always;

  using std::noop_coroutine;
}
#endif

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
