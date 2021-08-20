#pragma once

#include <coroutine>
#include <type_traits>

namespace wscoro {
namespace detail {

template<class P>
struct ThisCoroutineAwaiter final {
  std::coroutine_handle<P> _coroutine;

  bool await_ready() const noexcept {
    return false;
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

struct ThisCoroutineTag final {};

} // namespace detail

namespace await {

/// Disables the `co_await` operator by marking `await_transform` as deleted.
struct DisableAwait final {
  template<class>
  struct type {
    void await_transform() = delete;
  };
};

/// Enables the `co_await` operator for the given list of transforms and any
/// other valid awaiter.
/// \param Transforms A list of default constructible types each containing an
///        `await_transform` method.
template<template<class> class... Transforms>
struct EnableAwait final {
  template<class P>
  struct type : public Transforms<P>... {
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
struct OnlyAwait final {
  template<class P>
  struct type : public Transforms<P>... {};
};

/// Enables the expression `co_await wscoro::this_coroutine` which returns a
/// handle to the current coroutine.
template<class P>
struct ThisCoroutine {
  detail::ThisCoroutineAwaiter<P>
  await_transform(const detail::ThisCoroutineTag &) const noexcept {
    return {};
  }
};

} // namespace await

/// If enabled with `await::ThisCoroutine`, awaiting this returns a handle to
/// the current coroutine.
constexpr detail::ThisCoroutineTag this_coroutine;

} // namespace wscoro
