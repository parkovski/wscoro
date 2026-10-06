#pragma once

#include "await.h"
#include "suspend.h"

#include <coroutine>
#include <type_traits>
#include <thread>
#include <semaphore>

namespace wscoro {

/// A scheduler for types where await_ready always returns true. In this case,
/// await_suspend and therefore this type's operator() should never be called.
template<class T>
struct ReadyScheduler {
  [[maybe_unused]] T &_task;

  std::coroutine_handle<>
  operator()(std::coroutine_handle<>) const {
    assert(false);
    return std::noop_coroutine();
  }
};

/// A basic scheduler that runs the task and then the continuation.
template<class T>
struct SyncScheduler {
  T &_task;

  std::coroutine_handle<>
  operator()(std::coroutine_handle<> continuation) const {
    if constexpr (
      std::is_base_of_v<detail::Continuation, typename T::promise_type>
    ) {
      _task.promise().set_continuation(continuation);
      if (_task.promise().did_initial_suspend()) {
        // If the task had an initial suspend, awaiting it should start it.
        return _task.handle();
      } else {
        // If there was no initial suspend, we are awaiting the task when it is
        // already suspended at some point and will resume later.
        return std::noop_coroutine();
      }
    } else {
      // If the type has no continuation, it should not be able to await other
      // coroutines. If it could await, we would catch it at a random suspend
      // point here and potentially resume the continuation before the first
      // task was finished.
      // Rule: If you can await and be awaited, you need a continuation.
      static_assert(
        std::is_base_of_v<
          await::DisableAwait::type<typename T::promise_type>,
          typename T::promise_type
        >
      );
      _task.resume();
      return continuation;
    }
  }
};

/// Runs a task in a new thread, switching back to the original thread when the
/// task completes.
/// Warning: If this is used with a generator, it will start a new thread each
/// time it is awaited.
template<class T>
struct RunInNewThread {
  T &_task;
  std::binary_semaphore _sema{0};

  struct SignalCoroutine {
    struct promise_type;

    std::coroutine_handle<promise_type> _handle;

    struct promise_type {
      constexpr void return_void() const noexcept {}
      constexpr std::suspend_always initial_suspend() const noexcept {
        return {};
      }
      constexpr std::suspend_never final_suspend() const noexcept {
        return {};
      }
      SignalCoroutine get_return_object() noexcept {
        return {std::coroutine_handle<promise_type>::from_promise(*this)};
      }
      constexpr void unhandled_exception() const noexcept {}
    };
  };

  SignalCoroutine signal() {
    _sema.release();
    co_return;
  }

  std::coroutine_handle<>
  operator()(std::coroutine_handle<> continuation) {
    // The task must have an initial suspend because we are going to start it
    // in another thread. This doesn't work if it's already started.
    assert(_task.promise().did_initial_suspend());

    if constexpr (
      std::is_base_of_v<detail::Continuation, typename T::promise_type>
    ) {
      auto sig = signal();
      _task.promise().set_continuation(sig._handle);
      std::jthread{[&_task](){
        // TODO: What if this throws an exception?
        _task.resume();
      }};
    } else {
      // If the task has no continuation and can await, we would potentially be
      // resuming the continuation before the task is finished.
      static_assert(
        std::is_base_of_v<
          await::DisableAwait::type<typename T::promise_type>,
          typename T::promise_type
        >
      );
      std::jthread{[&_task, &_sema](){
        // TODO: What if this throws an exception?
        _task.resume();
        _sema.release();
      }};
    }
    _sema.acquire();
    return continuation;
  }
};

/// Switches to a new thread for the duration of the entire coroutine chain.
/// Warning: If this is used with a generator, it will start a new thread each
/// time it is awaited.
template<class T>
struct SwitchToNewThread {
  T &_task;

  std::coroutine_handle<>
  operator()(std::coroutine_handle<> continuation) {
    // The task must have an initial suspend otherwise we're resuming it at a
    // random location.
    assert(_task.promise().did_initial_suspend());

    if constexpr (
      std::is_base_of_v<detail::Continuation, typename T::promise_type>
    ) {
      _task.promise().set_continuation(continuation);
      std::jthread{[&_task](){
          // TODO: What if this throws an exception?
        _task.resume();
      }};
    } else {
      // If the task has no continuation and can await, we would potentially be
      // resuming the continuation before the task is finished.
      static_assert(
        std::is_base_of_v<
          await::DisableAwait::type<typename T::promise_type>,
          typename T::promise_type
        >
      );
      std::jthread{[&_task, continuation](){
          // TODO: What if this throws an exception?
        _task.resume();
        continuation.resume();
      }};
    }
    return std::noop_coroutine();
  }
};

} // namespace wscoro