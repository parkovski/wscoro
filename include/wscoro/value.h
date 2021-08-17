#pragma once

#include "suspend.h"

#include <cstddef>
#include <cassert>
#include <atomic>
#include <type_traits>

namespace wscoro {
namespace detail {

// Space for the promise return/yield data, reusable with proper lifetime
// support.
template<typename T, size_t Align = alignof(std::max_align_t)>
struct alignas(Align) PromiseData {
private:
  union {
    T _data;
    unsigned char _cdata[sizeof(T)];
  };
  mutable std::atomic_flag _is_empty;

protected:
  void free_data() noexcept {
    if (_is_empty.test_and_set(std::memory_order_acq_rel) == false) {
      // _is_empty was false, is now true.
      _data.~T();
    }
  }

public:
  PromiseData() noexcept {
    _is_empty.test_and_set(std::memory_order_acq_rel);
  }

  PromiseData(PromiseData &&) = delete;
  PromiseData(const PromiseData &) = delete;
  PromiseData &operator=(PromiseData &&) = delete;
  PromiseData &operator=(const PromiseData &) = delete;

  ~PromiseData() {
    free_data();
  }

  bool has_value() const noexcept {
#if defined(__GNUC__) && !defined(__clang__)
    // TODO: GCC doesn't have atomic_flag::test yet, so here's a very hacky
    // workaround.
    return !__atomic_load_n(&_is_empty._M_i, int(std::memory_order_acquire));
#else
    return !_is_empty.test(std::memory_order_acquire);
#endif
  }

  // Move constructor equivalent for inner data.
  template<typename U = T>
  std::enable_if_t<std::is_constructible_v<T, U>, T &>
  init_data(U &&data) noexcept(std::is_nothrow_constructible_v<T, U>) {
    free_data();
    new (&_data) T(std::forward<U>(data));
    _is_empty.clear(std::memory_order_release);
    return _data;
  }

  // Returns a const reference to the inner data.
  const T &data() const & {
    assert(has_value());
    return _data;
  }

  // Returns a reference to the inner data.
  T &data() & {
    assert(has_value());
    return _data;
  }

  // Consumes and returns the inner data by move constructor.
  T data() && {
    [[maybe_unused]] bool is_empty =
      _is_empty.test_and_set(std::memory_order_acq_rel);
    assert(!is_empty);
    return std::move(_data);
  }
};

template<size_t Align>
struct PromiseData<void, Align> {
  constexpr bool has_value() const noexcept {
    return false;
  }
  void data() const noexcept {}
};

} // namespace detail

namespace value {

/// Enables the `co_return r;` statement where `r` is implicitly convertible
/// to type `R`.
/// \param R The coroutine's return type.
template<class R>
struct BasicReturn : public detail::PromiseData<R> {
  using value_type = R;

  template<class T, class = std::enable_if_t<std::is_constructible_v<R, T>>>
  void return_value(T &&value) noexcept(std::is_nothrow_constructible_v<R, T>)
  {
    this->init_data(std::forward<T>(value));
  }
};

/// Enables the `co_return;` statement. No return value storage is allocated
/// for the coroutine.
template<>
struct BasicReturn<void> : public detail::PromiseData<void> {
  using value_type = void;

  constexpr void return_void() const noexcept {}
};

/// Enables the `co_yield y;` statement where `y` is implicitly convertible to
/// type `Y` and the `co_return;` statement.
/// \param Y The coroutine's yield type (generator return type).
/// \param Suspend An `Awaitable` that defines the coroutine's behavior at a
///        yield point.
template<class Y>
struct BasicYield : detail::PromiseData<Y> {
  using value_type = Y;

  void return_void() const noexcept {}

  template<class T>
  std::enable_if_t<std::is_constructible_v<Y, T>, std::suspend_always>
  yield_value(T &&value) noexcept(std::is_nothrow_constructible_v<Y, T>) {
    this->init_data(std::forward<T>(value));
    return {};
  }
};

template<class Y>
struct YieldWithContinuation : detail::PromiseData<Y>,
                               virtual detail::Continuation {
  using value_type = Y;

  void return_void() const noexcept {}

  template<class T>
  std::enable_if_t<std::is_constructible_v<Y, T>,
                   detail::SuspendWithContinuation>
  yield_value(T &&value) noexcept(std::is_nothrow_constructible_v<Y, T>) {
    this->init_data(std::forward<T>(value));
    return this->suspend_with_continuation();
  }
};

} // namespace value
} // namespace wscoro
