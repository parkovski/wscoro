#pragma once

#include <coroutine>
#include <type_traits>

namespace wscoro {
namespace detail {

template<class P>
struct this_coroutine_awaiter final {
  std::coroutine_handle<P> _coroutine = nullptr;

  bool await_ready() const noexcept {
    return !!_coroutine;
  }

  bool await_suspend(std::coroutine_handle<P> coroutine) noexcept {
    _coroutine = coroutine;
    // Resume this coroutine.
    return false;
  }

  std::coroutine_handle<P> await_resume() const noexcept {
    return _coroutine;
  }
};

struct this_coroutine_tag final {};

} // namespace detail

namespace await {

/// Disables the `co_await` operator by marking `await_transform` as deleted.
struct disable_await final {
  template<class>
  struct type {
    template<class T>
    void await_transform(T &&) = delete;
  };
};

/// Enables the `co_await` operator for the given list of transforms and any
/// other valid awaiter.
/// \param Transforms A list of default constructible types each containing an
///        `await_transform` method.
template<template<class> class... Transforms>
struct enable_await final {
  template<class P>
  struct type : Transforms<P>... {
    using Transforms<P>::await_transform...;

    template<class T>
    decltype(auto) await_transform(T &&t) const noexcept {
      return std::forward<T>(t);
    }
  };
};

/// Enables the `co_await` operator for the given list of transforms only.
/// The operator remains disabled for regular awaiters.
/// \param Transforms A list of default constructible types each containing an
///        `await_transform` method.
template<template<class> class... Transforms>
struct only_await final {
  template<class P>
  struct type : Transforms<P>... {
    using Transforms<P>::await_transform...;
  };
};

/// Enables the expression `co_await wscoro::this_coroutine` which returns a
/// handle to the current coroutine.
template<class P>
struct await_this_coroutine {
  detail::this_coroutine_awaiter<P>
  await_transform(const detail::this_coroutine_tag &) noexcept {
    return {std::coroutine_handle<P>::from_promise(*static_cast<P *>(this))};
  }
};

} // namespace await

/// If enabled with `await::ThisCoroutine`, awaiting this returns a handle to
/// the current coroutine.
constexpr detail::this_coroutine_tag this_coroutine;

} // namespace wscoro
