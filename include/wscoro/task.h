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

  CoroutineBase(const CoroutineBase &) = delete;
  CoroutineBase &operator=(const CoroutineBase &) = delete;

  CoroutineBase(CoroutineBase &&o) noexcept : _handle{o._handle} {
    o._handle = nullptr;
  }

  CoroutineBase &operator=(CoroutineBase &&o) = delete;

  friend void swap(CoroutineBase &a, CoroutineBase &b) noexcept {
    using std::swap;
    swap(a._handle, b._handle);
  }

  promise_type &promise() const noexcept {
    return _handle.promise();
  }

  bool done() const noexcept {
    return !_handle || _handle.done();
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
  using outer_promise_type = P;

  using base::base;

  BasicCoroutine(BasicCoroutine &&) = default;
};

template<class T>
struct BasicTaskAwaiter;

template<class P, class<class> A = BasicTaskAwaiter>
struct BasicTask;

template<class T>
struct BasicTaskAwaiter {
protected:
  const T &_task;

public:
  BasicTaskAwaiter(const T &task) noexcept
    : _task{task}
  {}

  bool await_ready() const noexcept {
    return _task.done();
  }

  bool await_suspend(std::coroutine_handle<typename T::promise_type>) const noexcept {
    return false;
  }

  // If exception behavior is to save and rethrow (AsyncThrow) and one
  // was thrown and not caught, it will be rethrown here. For non-void types,
  // this returns the value from the inner coroutine's co_return.
  typename T::value_type await_resume() const {
    auto &promise = _task.promise();
    promise.rethrow_exception();
    return std::move(promise).data();
  }
};

template<class P, class<class> A>
struct BasicTask final
  : detail::CoroutineBase<typename P::template type<BasicTask<P, A>>>
{
  using base =
    detail::CoroutineBase<typename P::template type<BasicTask<P, A>>>;

public:
  using typename base::promise_type;
  using outer_promise_type = P;

  // The type produced by awaiting this task.
  using value_type = typename promise_type::value_type;

  using base::base;

  BasicTask(BasicTask &&) = default;

  ~BasicTask() {
    if (this->_handle) {
      this->_handle.destroy();
    }
  }

  A<BasicTask> operator co_await() const noexcept(std::is_nothrow_constructible_v<A<BasicTask>, const BasicTask &>) {
    return {*this};
  }
};

template<class G>
struct BasicGeneratorAwaiter;

template<class P, class<class> A = BasicGeneratorAwaiter>
struct BasicGenerator;

template<class G>
struct BasicGeneratorAwaiter {
protected:
  const G &_gen;

public:
  BasicGeneratorAwaiter(const G &gen) noexcept
    : _gen{gen}
  {}

  bool await_ready() const noexcept {
    return _gen.promise().has_value() || _gen.done();
  }

  bool await_suspend(std::coroutine_handle<typename G::promise_type>) const noexcept {
    return false;
  }

  typename G::value_type await_resume() const {
    auto &promise = _gen.promise();
    promise.rethrow_exception();
    if (_gen.done()) {
      return std::nullopt;
    }
    assert(promise.has_value());
    return {std::move(promise).data()};
  }
};

template<class P, class<class> A>
struct BasicGenerator final
  : detail::CoroutineBase<typename P::template type<BasicGenerator<P, A>>>
{
  using base =
    detail::CoroutineBase<typename P::template type<BasicGenerator<P, A>>>;

public:
  using typename base::promise_type;
  using outer_promise_type = P;

  using value_type = std::optional<typename promise_type::value_type>;

  using base::base;

  BasicGenerator(BasicGenerator &&) = default;

  ~BasicGenerator() {
    if (this->_handle) {
      this->_handle.destroy();
    }
  }

  A<BasicGenerator> operator co_await() const noexcept(std::is_nothrow_constructible_v<A<BasicGenerator>, const BasicGenerator &>) {
    return {*this};
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
