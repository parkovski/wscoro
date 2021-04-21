#pragma once

#include "traits.h"

#include <optional>
#include <exception>
#include <atomic>
#include <cstddef>

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
  T &init_data(
    std::enable_if_t<std::is_move_constructible_v<U>, T> &&data
  ) noexcept(std::is_nothrow_move_constructible_v<U>)
  {
    free_data();
    new (&_data) T(std::move(data));
    _is_empty.clear(std::memory_order_release);
    return _data;
  }

  // Copy constructor equivalent for inner data.
  template<typename U = T>
  T &init_data(
    std::enable_if_t<std::is_copy_constructible_v<U>, T> const &data
  ) noexcept(std::is_nothrow_copy_constructible_v<U>)
  {
    free_data();
    new (&_data) T(data);
    _is_empty.clear(std::memory_order_release);
    return _data;
  }

  // Returns a reference to the inner data or throws std::runtime_error if
  // the inner data was empty (clear flag was set).
  const T &data() const & {
#ifndef NDEBUG
    if (!has_value()) {
      throw std::runtime_error("Data was empty");
    }
#endif
    return _data;
  }

  // Returns a reference to the inner data or throws std::runtime_error if
  // the inner data was empty (clear flag was set).
  T &data() & {
#ifndef NDEBUG
    if (!has_value()) {
      throw std::runtime_error("Data was empty");
    }
#endif
    return _data;
  }

  // Consumes and returns the inner data by move constructor.
  T data() && {
#ifndef NDEBUG
    if (_is_empty.test_and_set(std::memory_order_acq_rel)) {
      throw std::runtime_error("Data was empty");
    }
#endif
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

// Provides an await_ready that always suspends and an await_resume that does
// nothing.
struct SuspendBase {
  constexpr bool await_ready() const noexcept { return false; }
  constexpr void await_resume() const noexcept {}
};

// Sync suspender.
// Provides an empty await_suspend, equivalent to std::suspend_always, but
// templated to the promise type for consistency/type safety.
template<bool Async, typename P>
struct Suspend : SuspendBase {
  // Nothing to do here but return to the awaiter.
  constexpr void await_suspend(std::coroutine_handle<P>) const noexcept {}
};

template<bool Enable, bool Async, typename P>
using SuspendIf =
  std::conditional_t<Enable, Suspend<Async, P>, std::suspend_never>;

// Async suspender.
// Provides an await_suspend that returns the continuation if one was present,
// otherwise it simply returns to the awaiter.
template<typename P>
struct Suspend<true, P> : SuspendBase {
  std::coroutine_handle<>
  await_suspend(std::coroutine_handle<P> coroutine) const noexcept {
    auto &promise = coroutine.promise();
    auto continuation = promise._continuation;

    if (continuation) {
      promise._continuation = nullptr;
    } else {
      continuation = std::noop_coroutine();
    }

    return continuation;
  }
};

template<class T, class P, class I, bool FS>
struct SyncPromiseBase : detail::PromiseData<T> {
  using suspend_type = Suspend<false, P>;

  I initial_suspend() const noexcept {
    return {};
  }

  SuspendIf<FS, false, P> final_suspend() const noexcept {
    return {};
  }
};

template<class T, class P, class I, bool FS>
struct AsyncPromiseBase : detail::PromiseData<T> {
  using suspend_type = Suspend<true, P>;

  std::coroutine_handle<> _continuation{};

  ~AsyncPromiseBase() {
    if (_continuation) {
      _continuation.destroy();
    }
  }

  I initial_suspend() const noexcept {
    return {};
  }

  SuspendIf<FS, true, P> final_suspend() const noexcept {
    return {};
  }
};

template<class Base>
struct ValuePromiseBase;

template<template<class, class, class, bool> class Base,
         class T, class P, class I, bool FS>
struct ValuePromiseBase<Base<T, P, I, FS>> : Base<T, P, I, FS> {
  template<typename U = T>
  void return_value(
    std::enable_if_t<std::is_move_constructible_v<U>, T> &&value
  ) noexcept(std::is_nothrow_move_constructible_v<U>)
  {
    this->init_data(std::move(value));
  }

  template<typename U = T>
  void return_value(
    std::enable_if_t<std::is_copy_constructible_v<U>, T> const &value
  ) noexcept(std::is_nothrow_copy_constructible_v<U>)
  {
    this->init_data(value);
  }
};

template<template<class, class, class, bool> class Base,
         class P, class I, bool FS>
struct ValuePromiseBase<Base<void, P, I, FS>> : Base<void, P, I, FS> {
  constexpr void return_void() const noexcept {}
};

template<class Base>
struct GeneratorPromiseBase;

template<template<class, class, class, bool> class Base,
         class T, class P, class I, bool FS>
struct GeneratorPromiseBase<Base<T, P, I, FS>> : Base<T, P, I, FS> {
  using suspend_type = typename Base<T, P, I, FS>::suspend_type;

  constexpr void return_void() const noexcept {}

  // Move value into internal data where it will be moved out at
  // await_resume.
  template<typename U = T>
  suspend_type
  yield_value(
    std::enable_if_t<std::is_move_constructible_v<U>, T> &&value
  ) noexcept(std::is_nothrow_move_constructible_v<U>)
  {
    this->init_data(std::move(value));
    return {};
  }

  // Copy value into internal data where it will be moved/copied out at
  // await_resume.
  template<typename U = T>
  suspend_type
  yield_value(
    std::enable_if_t<std::is_copy_constructible_v<U>, T> const &value
  ) noexcept(std::is_nothrow_copy_constructible_v<U>)
  {
    this->init_data(value);
    return {};
  }
};

template<class T, class P, class I, bool FS, bool Generator, bool Async>
struct PromiseBaseT;

template<class T, class P, class I, bool FS>
struct PromiseBaseT<T, P, I, FS, false, false> {
  using type = ValuePromiseBase<SyncPromiseBase<T, P, I, FS>>;
};

template<class T, class P, class I, bool FS>
struct PromiseBaseT<T, P, I, FS, false, true> {
  using type = ValuePromiseBase<AsyncPromiseBase<T, P, I, FS>>;
};

template<class T, class P, class I, bool FS>
struct PromiseBaseT<T, P, I, FS, true, false> {
  using type = GeneratorPromiseBase<SyncPromiseBase<T, P, I, FS>>;
};

template<class T, class P, class I, bool FS>
struct PromiseBaseT<T, P, I, FS, true, true> {
  using type = GeneratorPromiseBase<AsyncPromiseBase<T, P, I, FS>>;
};

/// @param T Promise data type.
/// @param P The task's promise_type.
/// @param I The initial suspend type.
/// @param FS Determines if the task will have a final suspend.
/// @param Generator Is the coroutine a generator?
/// @param Async Does the coroutine have a continuation?
template<class T, class P, class I, bool FS, bool Generator, bool Async>
using PromiseBase = typename PromiseBaseT<T, P, I, FS, Generator, Async>::type;

// Behavior when an unhandled exception is thrown inside the coroutine.
template<traits::ExceptionBehavior EB>
struct PromiseUnhandledException;

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

template<bool Awaiter>
struct PromiseAwaitTransform {};

template<>
struct PromiseAwaitTransform<false> {
  void await_transform() = delete;
};

} // namespace detail

template<class TaskT>
struct Promise;

template<
  template<class, traits::BasicTaskTraits> class TaskT,
  class T, traits::BasicTaskTraits Traits
>
struct Promise<TaskT<T, Traits>> :
  detail::PromiseBase<
    T,
    typename TaskT<T, Traits>::promise_type,
    typename Traits::initial_suspend_type,
    Traits::final_suspend::value,
    Traits::is_generator::value,
    Traits::is_async::value
  >,
  detail::PromiseUnhandledException<typename Traits::exception_behavior>,
  detail::PromiseAwaitTransform<Traits::is_awaiter::value>
{
  TaskT<T, Traits> get_return_object()
    noexcept(std::is_nothrow_constructible_v<
      TaskT<T, Traits>,
      std::coroutine_handle<typename TaskT<T, Traits>::promise_type>
    >)
  {
    return {
      std::coroutine_handle<typename TaskT<T, Traits>::promise_type>
        ::from_promise(
          *static_cast<typename TaskT<T, Traits>::promise_type *>(this)
        )
    };
  }
};

} // namespace wscoro
