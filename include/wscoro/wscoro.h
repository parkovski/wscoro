#pragma once

#include <type_traits>

#if __has_include(<experimental/concepts>) && !__has_include(<concepts>)
# include <experimental/concepts>
# define STD_NEEDS_EXPERIMENTAL
#else
# include <concepts>
#endif

#if __has_include(<experimental/coroutine>) && !__has_include(<coroutine>)
# include <experimental/coroutine>
# define STD_NEEDS_EXPERIMENTAL
#else
# include <coroutine>
#endif

#ifdef STD_NEEDS_EXPERIMENTAL
namespace std {
  using namespace experimental;
}
#endif

#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/logger.h>

namespace wscoro {

template<typename F> requires std::is_nothrow_invocable_v<F>
struct ScopeExit {
  F f;
  constexpr ScopeExit(F f) noexcept : f(std::move(f)) {}
  ~ScopeExit() { f(); }
};

template<typename F>
inline constexpr static auto scope_exit(F f) noexcept {
  return ScopeExit<F>(f);
}

// Use operator&& on a lambda.
struct ScopeExitHelper {
  template<typename F>
  constexpr ScopeExit<F> operator&&(F f) const noexcept {
    return ScopeExit<F>(std::move(f));
  }
};

inline constexpr static auto scope_exit() noexcept {
  return ScopeExitHelper{};
}

} // namespace wscoro

#define WSCORO_SCOPE_EXIT \
  auto _scope_exit_##__LINE__ = ::wscoro::scope_exit() && [&]() noexcept

namespace wscoro::log {

#ifdef WSCORO_LOGGER
extern std::shared_ptr<spdlog::logger> WSCORO_LOGGER;
# define LOGMETHOD(name)                                   \
  template<typename... Args>                               \
  static void name(const char *fmt, Args &&...args) {      \
    WSCORO_LOGGER->name(fmt, std::forward<Args>(args)...); \
  }

#else
# define LOGMETHOD(name)     \
  template<typename... Args> \
  static void name(const char *, Args &&...) {}

#endif

LOGMETHOD(trace)
LOGMETHOD(debug)
LOGMETHOD(info)
LOGMETHOD(warn)
LOGMETHOD(error)
#undef LOGMETHOD

// Logs a critical error and calls std::terminate.
template<typename... Args>
static void critical(const char *fmt, Args &&...args) {
#ifdef WSCORO_LOGGER
  WSCORO_LOGGER->name(fmt, std::forward<Args>(args)...);
#endif
  std::terminate();
}

} // namespace wscoro::log
