#pragma once

#include "suspend.h"

#include <cstddef>
#include <cassert>
#include <atomic>
#include <type_traits>

namespace wscoro {
namespace detail {

// gcc: requested alignment '0' is not a positive power of 2.
#ifdef __GNUC__
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wattributes"
#endif

// Space for the promise return/yield data, reusable with proper lifetime
// support.
template<typename T, size_t Align = 0>
struct alignas(Align) promise_data {
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
  promise_data() noexcept {
    _is_empty.test_and_set(std::memory_order_release);
  }

  promise_data(promise_data &&) = delete;
  promise_data(const promise_data &) = delete;
  promise_data &operator=(promise_data &&) = delete;
  promise_data &operator=(const promise_data &) = delete;

  ~promise_data() {
    free_data();
  }

  bool has_value() const noexcept {
    return !_is_empty.test(std::memory_order_acquire);
  }

  // Move constructor equivalent for inner data.
  template<typename U>
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
    [[maybe_unused]] bool was_empty =
      _is_empty.test_and_set(
#ifdef NDEBUG
        std::memory_order_release
#else
        std::memory_order_acq_rel
#endif
      );
    assert(!was_empty);
    return std::move(_data);
  }
};

#ifdef __GNUC__
# pragma GCC diagnostic pop
#endif

template<>
struct promise_data<void, 0> {
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
struct basic_return : detail::promise_data<R, Align> {
  using value_type = R;

  template<class T,
           class = std::enable_if_t<std::is_constructible_v<R, T>>>
  void return_value(T &&value) noexcept(std::is_nothrow_constructible_v<R, T>)
  {
    this->init_data(std::forward<T>(value));
  }
};

/// Enables the `co_return;` statement. No return value storage is allocated
/// for the coroutine.
template<>
struct basic_return<void, 0> : detail::promise_data<void> {
  using value_type = void;

  constexpr void return_void() const noexcept {}
};

/// Enables the `co_yield y;` statement where `y` is implicitly convertible to
/// type `Y` and the `co_return;` statement.
/// \param Y The coroutine's yield type (generator return type).
/// \param Align The alignment of the promise return data. The default value 0
///        uses the default alignment of `R`.
template<class Y, size_t Align = 0>
struct basic_yield : detail::promise_data<Y, Align> {
  using value_type = Y;

  void return_void() const noexcept {}

  template<class T,
           class = std::enable_if_t<std::is_constructible_v<Y, T>>>
  std::suspend_always
  yield_value(T &&value) noexcept(std::is_nothrow_constructible_v<Y, T>) {
    this->init_data(std::forward<T>(value));
    return {};
  }
};

template<class Y, size_t Align = 0>
struct yield_with_continuation
  : detail::promise_data<Y, Align>
  , detail::with_continuation {
  using value_type = Y;

  void return_void() const noexcept {}

  template<class T,
           class = std::enable_if_t<std::is_constructible_v<Y, T>>>
  detail::resumer
  yield_value(T &&value) noexcept(std::is_nothrow_constructible_v<Y, T>) {
    this->init_data(std::forward<T>(value));
    return {this->_continuation};
  }

  detail::resumer final_suspend() const noexcept {
    return {this->_continuation};
  }
};

} // namespace value
} // namespace wscoro
