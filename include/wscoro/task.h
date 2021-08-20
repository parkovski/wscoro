#pragma once

#include <typeinfo>
#include <type_traits>
#include <coroutine>
#include <optional>
#include <cassert>

namespace wscoro {

namespace detail {

/// Coroutine base type.
/// \param P The promise type.
template<class P>
struct CoroutineBase {
public:
  friend struct std::hash<CoroutineBase>;

  using promise_type = P;

protected:
  std::coroutine_handle<promise_type> _handle;

public:
  explicit CoroutineBase(std::coroutine_handle<promise_type> handle) noexcept
    : _handle{handle}
  {}

  friend void swap(CoroutineBase &a, CoroutineBase &b) noexcept {
    using std::swap;
    swap(a._handle, b._handle);
  }

  promise_type &promise() const noexcept {
    return _handle.promise();
  }

  bool done() const noexcept {
    return _handle && _handle.done();
  }

  explicit operator bool() const noexcept {
    return _handle != nullptr;
  }

  void operator()() const {
    _handle();
  }

  void resume() const {
    _handle.resume();
  }

  void destroy() {
    _handle.destroy();
    _handle = nullptr;
  }
};

} // namespace detail

/// This type is not awaitable and it does not destroy the coroutine when it
/// finishes - the coroutine is entirely on its own.
/// \param P The coroutine's promise type.
template<class P>
struct BasicCoroutine
  : detail::CoroutineBase<typename P::template type<BasicCoroutine<P>>>
{
  using base =
    detail::CoroutineBase<typename P::template type<BasicCoroutine<P>>>;

public:
  using typename base::promise_type;

  using base::base;
};

template<class P>
struct BasicTask
  : detail::CoroutineBase<typename P::template type<BasicTask<P>>>
{
  using base =
    detail::CoroutineBase<typename P::template type<BasicTask<P>>>;

public:
  using typename base::promise_type;

  // The type produced by awaiting this task.
  using value_type = typename promise_type::value_type;

  using base::base;

  ~BasicTask() {
    if (this->_handle) {
      this->_handle.destroy();
    }
  }

  bool await_ready() const noexcept {
    return this->done();
  }

  std::coroutine_handle<>
  await_suspend(std::coroutine_handle<> continuation) const {
    auto &promise = this->promise();
    if (!promise.set_continuation(continuation)) {
      // Can't store the continuation - execute synchronously.
      this->resume();
      return continuation;
    }

    if (promise.did_initial_suspend()) {
      // If there is an initial suspend, await is the mechanism to start the
      // coroutine.
      return this->_handle;
    } else {
      // If there is no initial suspend, the coroutine already started
      // automatically, so we should not resume it at an arbitrary location.
      return std::noop_coroutine();
    }
  }

  // If exception behavior is to save and rethrow (AsyncThrow) and one
  // was thrown and not caught, it will be rethrown here. For non-void types,
  // this returns the value from the inner coroutine's co_return.
  value_type await_resume() const {
    auto &promise = this->promise();
    promise.rethrow_exception();
    return std::move(promise).data();
  }
};

template<class P>
struct BasicGenerator
  : detail::CoroutineBase<typename P::template type<BasicGenerator<P>>>
{
  using base =
    detail::CoroutineBase<typename P::template type<BasicGenerator<P>>>;

public:
  using typename base::promise_type;

  using value_type = std::optional<typename promise_type::value_type>;

  using base::base;

  ~BasicGenerator() {
    if (this->_handle) {
      this->_handle.destroy();
    }
  }

  bool await_ready() const noexcept {
    return this->promise().has_value() || this->done();
  }

  std::coroutine_handle<>
  await_suspend(std::coroutine_handle<> continuation) const {
    auto &promise = this->promise();
    if (!promise.set_continuation(continuation)) {
      // Can't store the continuation - execute synchronously.
      this->resume();
      return continuation;
    }

    // Generators should always suspend initially since they can be awaited
    // multiple times. Without an initial suspend, the behavior of the first
    // await would differ from all the others.
    assert(promise.did_initial_suspend());
    return this->_handle;
  }

  value_type await_resume() const {
    auto &promise = this->promise();
    promise.rethrow_exception();
    if (this->done()) {
      return std::nullopt;
    }
    assert(promise.has_value());
    return value_type{std::move(promise).data()};
  }
};

} // namespace wscoro

namespace std {
  template<class P>
  struct hash<::wscoro::detail::CoroutineBase<P>> {
    size_t operator()(const ::wscoro::detail::CoroutineBase<P> &co) const
      noexcept
    {
      return hash<remove_cvref_t<decltype(co._handle)>>{}(co._handle);
    }
  };
}
