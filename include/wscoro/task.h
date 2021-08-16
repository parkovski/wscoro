#pragma once

#include <typeinfo>
#include <type_traits>
#include <coroutine>
#include <optional>

namespace wscoro {

namespace detail {

template<class C>
class CoroutineBase {
public:
  friend struct std::hash<CoroutineBase>;

  using promise_type = typename C::promise_type;

protected:
  std::coroutine_handle<promise_type> _handle;

  promise_type &promise() const {
    return _handle.promise();
  }

public:
  constexpr CoroutineBase(std::coroutine_handle<promise_type> handle)
    noexcept
    : _handle{handle}
  {}

  CoroutineBase(const CoroutineBase &) = default;
  CoroutineBase &operator=(const CoroutineBase &) = default;

  constexpr CoroutineBase(CoroutineBase &&o) noexcept
    : _handle{o._handle}
  {
    o._handle = nullptr;
  }

  constexpr CoroutineBase &operator=(CoroutineBase &&o) noexcept {
    _handle = o._handle;
    o._handle = nullptr;
    return *this;
  }

  friend void swap(CoroutineBase &a, CoroutineBase &b) noexcept {
    using std::swap;
    swap(a._handle, b._handle);
  }

  bool done() const {
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
  }
};

} // namespace detail

template<class P>
class BasicCoroutine : public detail::CoroutineBase<BasicCoroutine<P>> {
public:
  using promise_type = typename P::template type<BasicCoroutine>;

  constexpr BasicCoroutine(std::coroutine_handle<promise_type> handle)
    noexcept
    : detail::CoroutineBase<BasicCoroutine>{handle}
  {}
};

template<class P>
class BasicTask : public detail::CoroutineBase<BasicTask<P>> {
public:
  using promise_type = typename P::template type<BasicTask>;

  // The type produced by awaiting this task.
  using value_type = typename promise_type::value_type;

  constexpr BasicTask(std::coroutine_handle<promise_type> handle) noexcept
    : detail::CoroutineBase<BasicTask>{handle}
  {}

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

  // If exception behavior is to save and rethrow (handle_exceptions) and one
  // was thrown and not caught, it will be rethrown here. For non-void types,
  // this returns the value from the inner coroutine's co_return.
  // If the type is configured to copy instead of move, the data is not
  // consumed.
  value_type await_resume() const {
    auto &promise = this->promise();
    promise.rethrow_exception();
    return std::move(promise).data();
  }
};

template<class P>
class BasicGenerator : public detail::CoroutineBase<BasicGenerator<P>> {
  using promise_type = typename P::template type<BasicGenerator>;

  // The type produced by awaiting this task.
  using value_type = typename promise_type::value_type;

  constexpr BasicGenerator(std::coroutine_handle<promise_type> handle) noexcept
    : detail::CoroutineBase<BasicGenerator>{handle}
  {}

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

  std::optional<value_type> await_resume() const {
    auto &promise = this->_coroutine.promise();
    promise.rethrow_exception();
    if (this->done()) {
      return std::nullopt;
    }
    assert(promise.has_value());
    return std::move(promise).data();
  }
};

} // namespace wscoro

namespace std {
  template<class P>
  struct hash<::wscoro::BasicCoroutine<P>> {
    size_t operator()(const ::wscoro::BasicCoroutine<P> &co) const noexcept {
      return hash<remove_cvref_t<decltype(co._handle)>>{}(co._handle);
    }
  };
}
