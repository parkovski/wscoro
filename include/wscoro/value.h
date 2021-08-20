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
template<typename T, size_t Align = 0>
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
  std::enable_if_t<std::is_constructible_v<T, U>>
  init_data(U &&data) noexcept(std::is_nothrow_constructible_v<T, U>) {
    free_data();
    new (&_data) T(std::forward<U>(data));
    _is_empty.clear(std::memory_order_release);
  }

  // Returns a const reference to the inner data.
  const T &data() const & noexcept {
    assert(has_value());
    return _data;
  }

  // Returns a reference to the inner data.
  T &data() & noexcept {
    assert(has_value());
    return _data;
  }

  // Consumes and returns the inner data by move constructor.
  T data() && noexcept {
    [[maybe_unused]] bool is_empty =
      _is_empty.test_and_set(std::memory_order_acq_rel);
    assert(!is_empty);
    return std::move(_data);
  }
};

template<>
struct PromiseData<void, 0> {
  bool has_value() const noexcept {
    return false;
  }

  void data() const noexcept {}
};

} // namespace detail

namespace value {

/// Enables the `co_return r;` statement where `r` is implicitly convertible
/// to type `R`.
/// \param R The coroutine's return type.
/// \param Align The alignment of the promise return data. The default value 0
///        uses the default alignment of `R`.
template<class R, size_t Align = 0>
struct BasicReturn : detail::PromiseData<R, Align> {
  using value_type = R;

  template<class T = R,
           class = std::enable_if_t<std::is_constructible_v<R, T>>>
  void return_value(T &&value) noexcept(std::is_nothrow_constructible_v<R, T>)
  {
    this->init_data(std::forward<T>(value));
  }
};

/// Enables the `co_return;` statement. No return value storage is allocated
/// for the coroutine.
template<>
struct BasicReturn<void, 0> : detail::PromiseData<void> {
  using value_type = void;

  constexpr void return_void() const noexcept {}
};

/// Enables the `co_yield y;` statement where `y` is implicitly convertible to
/// type `Y` and the `co_return;` statement.
/// \param Y The coroutine's yield type (generator return type).
/// \param Align The alignment of the promise return data. The default value 0
///        uses the default alignment of `R`.
template<class Y, size_t Align = 0>
struct BasicYield : detail::PromiseData<Y, Align> {
  using value_type = Y;

  void return_void() const noexcept {}

  template<class T = Y,
           class = std::enable_if_t<std::is_constructible_v<Y, T>>>
  std::suspend_always
  yield_value(T &&value) noexcept(std::is_nothrow_constructible_v<Y, T>) {
    this->init_data(std::forward<T>(value));
    return {};
  }
};

/// Enables the `co_yield y;` statement where `y` is implicitly convertible to
/// type `Y` and the `co_return;` statement. Resumes the awaiter asynchronously
/// when finished.
/// \param Y The coroutine's yield type (generator return type).
/// \param Align The alignment of the promise return data. The default value 0
///        uses the default alignment of `R`.
template<class Y, size_t Align = 0>
struct YieldWithContinuation : detail::PromiseData<Y, Align>,
                               virtual detail::Continuation {
  using value_type = Y;

  void return_void() const noexcept {}

  template<class T = Y,
           class = std::enable_if_t<std::is_constructible_v<Y, T>>>
  detail::SuspendWithContinuation
  yield_value(T &&value) noexcept(std::is_nothrow_constructible_v<Y, T>) {
    this->init_data(std::forward<T>(value));
    return this->suspend_with_continuation();
  }
};

} // namespace value
} // namespace wscoro
