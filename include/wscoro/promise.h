#pragma once

#include "tasktraits.h"

#include <optional>
#include <exception>
#include <atomic>

namespace wscoro {

namespace detail {

// A type that suspends to the awaiter.
template<bool Continuation, typename P>
struct ReadySuspend;

// This is a continuation-supported promise awaiter. It always suspends and if
// there is a pending continuation, resumes it.
template<typename P>
struct ReadySuspend<true, P> {
  // Return false to always suspend at this point.
  bool await_ready() const noexcept { return false; }

  // If there was a continuation, return it to let the caller resume it.
  // This is the last of the coroutine work.
  std::coroutine_handle<>
  await_suspend(std::coroutine_handle<P> coroutine) const noexcept {
    if (auto continuation = coroutine.promise()._continuation) {
      coroutine.promise()._continuation = nullptr;
      return continuation;
    }
    return std::noop_coroutine();
  }

  // Last thing on the promise called - maybe should do cleanup here?
  // Is this even called if I don't resume the suspend handle?
  void await_resume() const noexcept {
    log::debug("ReadySuspend<true>::await_resume");
  }
};

// If there is no continuation, all there is to do is just suspend to the
// awaiter.
template<typename P>
struct ReadySuspend<false, P> : std::suspend_always {};

// Behavior when an unhandled exception is thrown inside the coroutine.
template<traits::ExceptionBehavior EB> struct PromiseUnhandledException;

// Ignore exceptions. Good when exceptions are disabled or not needed.
template<>
struct PromiseUnhandledException<traits::ignore_exceptions> {
  void unhandled_exception() const noexcept {}
};

// Handle exceptions - save an std::exception_ptr to be rethrown when control
// is yielded back to the awaiter of the coroutine. Good when the coroutine
// is asynchronous.
template<>
struct PromiseUnhandledException<traits::handle_exceptions> {
  std::exception_ptr _exception{};

  void unhandled_exception() noexcept {
    auto ex = std::current_exception();
    log::warn("unhandled_exception: stored exception pointer.");
    this->_exception = ex;
  }
};

// Rethrow exceptions - immediately rethrow unhandled exceptions. Good when
// the coroutine is synchronous.
template<>
struct PromiseUnhandledException<traits::rethrow_exceptions> {
  [[noreturn]] void unhandled_exception() const {
    std::rethrow_exception(std::current_exception());
  }
};

// Space for the promise return/yield data, reusable with proper lifetime
// support.
template<typename T>
struct PromiseDataBase {
private:
  union {
    T _data;
    unsigned char _cdata[sizeof(T)];
  };
  std::atomic_flag _is_empty;

  void free_data() noexcept {
    if (_is_empty.test_and_set() == false) {
      // _is_empty was false, is now true.
      _data.~T();
    }
  }

public:
  PromiseDataBase() noexcept {
    _is_empty.test_and_set();
  }

  ~PromiseDataBase() {
    free_data();
  }

  bool has_data() const noexcept {
#if defined(__GNUC__) && !defined(__clang__)
    // TODO: GCC doesn't have atomic_flag::test yet, making this not thread
    // safe.
    if (!const_cast<std::atomic_flag &>(_is_empty).test_and_set()) {
      const_cast<std::atomic_flag &>(_is_empty).clear();
      return false;
    }
    return true;
#else
    return !_is_empty.test(std::memory_order_acquire);
#endif
  }

  // Copy constructor equivalent for inner data.
  template<typename = std::enable_if_t<std::is_copy_constructible_v<T>>>
  T &init_data(const T &data)
    noexcept(std::is_nothrow_copy_constructible_v<T>)
  {
    free_data();
    new (&_data) T(data);
    _is_empty.clear();
    return _data;
  }

  // Move constructor equivalent for inner data.
  template<typename = std::enable_if_t<std::is_move_constructible_v<T>>>
  T &init_data(T &&data)
    noexcept(std::is_nothrow_move_constructible_v<T>)
  {
    free_data();
    new (&_data) T(std::move(data));
    _is_empty.clear();
    return _data;
  }

  // Returns a reference to the inner data or throws std::runtime_error if
  // the inner data was empty (clear flag was set).
  const T &data() const {
    if (!has_data()) {
      throw std::runtime_error("Data was empty");
    }
    return _data;
  }

  // Returns a reference to the inner data or throws std::runtime_error if
  // the inner data was empty (clear flag was set).
  T &data() {
    if (!has_data()) {
      throw std::runtime_error("Data was empty");
    }
    return _data;
  }

  // Returns a move reference to the inner data and marks the data empty.
  template<typename = std::enable_if_t<std::is_move_constructible_v<T>>>
  T &&take_data() && {
    if (_is_empty.test_and_set()) {
      throw std::runtime_error("Data was empty");
    }
    return std::move(_data);
  }

  // Copies the inner data, frees the inner data, marks the data as empty,
  // and returns the copy.
  template<typename = std::enable_if_t<std::is_copy_constructible_v<T>>>
  T take_data() & {
#if defined(__GNUC__) && !defined(__clang__)
    if (_is_empty.test_and_set()) {
      throw std::runtime_error("Data was empty");
    }
    _is_empty.clear();
#else
    if (_is_empty.test(std::memory_order_acquire)) {
      throw std::runtime_error("Data was empty");
    }
#endif
    T data{_data};
    free_data();
    return data;
  }
};

// For a promise that returns data, this struct stores that data and enables
// the return_value function. For a void promise, this enables the
// return_void function.
template<typename P, typename T, bool Async, bool Yield>
struct PromiseData;

// The promise returns void. Yield is not available for void returns.
template<typename P, bool Async>
struct PromiseData<P, void, Async, false> {
  void return_void() const noexcept {}
};

// A type that has data but does not support yield requires the data be
// initialized exactly once. Requires a move/copy constructor.
template<typename P, typename T, bool Async>
struct PromiseData<P, T, Async, false> : PromiseDataBase<T> {
  template<typename = std::enable_if_t<std::is_move_constructible_v<T>>>
  void return_value(T &&value)
    noexcept(std::is_nothrow_move_constructible_v<T>)
  {
    this->init_data(std::move(value));
  }

  template<typename = std::enable_if_t<std::is_copy_constructible_v<T>>>
  void return_value(const T &value)
    noexcept(std::is_nothrow_copy_constructible_v<T>)
  {
    this->init_data(value);
  }
};

// A type with yield support may "return" multiple times.
template<typename P, typename T, bool Async>
struct PromiseData<P, T, Async, true> : PromiseDataBase<T> {
  // Generators can't return a value. This discards the current continuation
  // if one exists as an await on this type expects a value via yield.
  void return_void() noexcept {
    if constexpr (Async) {
      auto promise = static_cast<P *>(this);
      if (auto continuation = promise->_continuation) {
        promise->_continuation = nullptr;
        continuation.destroy();
        log::warn("Generator finished and discarded its continuation.");
      }
    }
  }

  // Move value into internal data where it will be moved out at
  // await_resume.
  template<typename = std::enable_if_t<std::is_move_constructible_v<T>>>
  ReadySuspend<Async, P> yield_value(T &&value)
    noexcept(std::is_nothrow_move_constructible_v<T>)
  {
    this->init_data(std::move(value));
    return {};
  }

  // Copy value into internal data where it will be moved/copied out at
  // await_resume.
  template<typename = std::enable_if_t<std::is_copy_constructible_v<T>>>
  ReadySuspend<Async, P> yield_value(const T &value)
    noexcept(std::is_nothrow_copy_constructible_v<T>)
  {
    this->init_data(value);
    return {};
  }
};

// If Enable is true, contains a continuation handle.
template<bool Enable> struct PromiseContinuation;
template<> struct PromiseContinuation<false> {};
template<> struct PromiseContinuation<true> {
  std::coroutine_handle<> _continuation{};

  ~PromiseContinuation() {
    if (_continuation) {
      _continuation.destroy();
    }
  }
};

template<bool SupportsInnerAwait>
struct PromiseAwaitTransform {};

template<>
struct PromiseAwaitTransform<false> {
  template<typename T>
  void await_transform(T&&) = delete;

  template<typename T>
  void await_transform(const T &) = delete;
};

} // namespace detail

template<typename TTask, typename TData, CoroutineTraits Traits>
struct Promise :
  detail::PromiseData<
    typename TTask::promise_type,
    TData,
    Traits::is_async::value,
    Traits::is_generator::value
  >,
  detail::PromiseContinuation<typename Traits::is_async {}>,
  detail::PromiseUnhandledException<typename Traits::exception_behavior>,
  detail::PromiseAwaitTransform<typename Traits::is_awaiter {}>
{
  TTask get_return_object()
    noexcept(std::is_nothrow_constructible_v<
      TTask, std::coroutine_handle<typename TTask::promise_type>>)
  {
    TTask task(
      std::coroutine_handle<typename TTask::promise_type>::from_promise(
        *static_cast<typename TTask::promise_type *>(this)));
    return task;
  }

  // See traits initial_suspend_type.
  typename Traits::initial_suspend_type
  initial_suspend() const noexcept {
    log::trace("Promise::initial_suspend.");
    return {};
  }

  // If inner await is supported and there is a pending continuation,
  // it will be resumed here.
  detail::ReadySuspend<Traits::is_async::value, typename TTask::promise_type>
  final_suspend() const noexcept {
    log::trace("Promise::final_suspend");
    return {};
  }
};

namespace detail {
  template<typename TTask, typename TTaskBase>
  struct PromiseFor;

  template<
    typename TTask,
    template<typename, CoroutineTraits> typename TTaskBase,
    typename TData,
    CoroutineTraits Traits
  >
  struct PromiseFor<TTask, TTaskBase<TData, Traits>> {
    using type = Promise<TTask, TData, Traits>;
  };
}

// Create a promise type for a task that extends TaskBase.
template<typename TTask, typename TTaskBase>
using PromiseFor = typename detail::PromiseFor<TTask, TTaskBase>::type;

} // namespace wscoro
