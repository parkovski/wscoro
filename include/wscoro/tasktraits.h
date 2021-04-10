#pragma once

#include "wscoro.h"

namespace wscoro::traits {

// Ignore unhandled exceptions entirely.
struct ignore_exceptions {};

// Save unhandled exceptions and rethrow them on resume (finalize).
struct handle_exceptions {};

// Immediately rethrow uncaught exceptions.
struct rethrow_exceptions {};

// Behavior when an unhandled exception is caught inside the coroutine.
template<typename T>
concept ExceptionBehavior =
  std::is_same_v<T, ignore_exceptions> ||
  std::is_same_v<T, handle_exceptions> ||
  std::is_same_v<T, rethrow_exceptions>;

template<typename T>
concept AwaitSuspendReturn =
  std::is_same_v<T, void> ||
  std::is_same_v<T, bool> ||
  std::is_convertible_v<T, std::coroutine_handle<>>;

template<typename T, typename Awaitable>
concept AwaitSuspend =
  requires (Awaitable a, T t) { { a.*t({}) } -> AwaitSuspendReturn; } ||
  requires (Awaitable a, T t) { { a.*t() } -> AwaitSuspendReturn; };

// Any type that specifies these options is usable as traits for a Task.
// Note: gcc missing convertible_to.
template<typename T>
concept CoroutineTraits = requires (typename T::initial_suspend_type is) {
  { (bool)(typename T::is_generator{}) } -> std::same_as<bool>;
  { (bool)(typename T::is_awaiter{}) } -> std::same_as<bool>;
  { (bool)(typename T::is_async{}) } -> std::same_as<bool>;
  { typename T::exception_behavior{} } -> ExceptionBehavior;
  { typename T::initial_suspend_type{} };
  { is.await_ready() } -> std::same_as<bool>;
  { is.await_resume() };
  // { await_suspend({}) or await_suspend() } -> AwaitSuspendReturn;
  { (bool)(typename T::move_result{}) } -> std::same_as<bool>;
};

// Notes on the first three flags:
// If is_awaiter == true and is_async == false, the resulting task should not
// be awaitable. This setting is incompatible with is_generator and should be
// restricted to only return void.

template<typename T> struct DefaultTaskTraits {
  // The coroutine supports the co_yield expression. If this is supported,
  // a return type of void is enforced and the task type instead becomes the
  // type of the yielded value.
  using is_generator = typename T::is_generator;

  // The coroutine supports a saved continuation when others co_await this.
  // If this is true, awaiters of this task may be resumed asynchronously. If
  // this is false, this task does not contain a continuation, but if it can
  // await other coroutines, its execution still may not be synchronous. For
  // this reason, with a coroutine A awaiting a sync task B that awaits an
  // async task C,
  using is_async = typename T::is_async;

  // The coroutine supports the co_await expression. If this is true, this
  // task may suspend by awaiting an asynchronous task and therefore cannot be
  // run synchronously. If this is false, the task cannot use co_await and will
  // run synchronously through the next co_return or co_yield.
  using is_awaiter = is_async;

  // Exception handling behavior is explained on the relevant types, but a
  // good default is if you keep a continuation to resume later, then you
  // should use handle_exceptions, otherwise use rethrow_exceptions. The
  // only time ignore_exceptions should be used is when you're sure exceptions
  // won't be thrown, like in an exceptionless build (not yet supported).
  // TODO is there a way to know if we're compiling without exceptions? Then
  // I could make the default ignore under that circumstance.
  using exception_behavior =
    std::conditional<is_async::value, handle_exceptions, rethrow_exceptions>;

  // Type returned from promise's initial_suspend - should be
  // std::suspend_never or std::suspend_always (default), but any awaiter is
  // valid if there's a reason to customize this further. Issuing a suspend
  // here will stop the coroutine before any of its work is done. Otherwise
  // the coroutine will immediately run to the next suspend point on
  // creation.
  using initial_suspend_type = std::suspend_always;

  // Try to move data out of the promise. Data is only copied if a move can't
  // be done. If this is false, data is moved only when a copy can't be done.
  using move_result = std::true_type;
};

// A general purpose task type. Starts suspended and may await other
// coroutines.
struct TaskTraits {
  using is_generator = std::false_type;
  using is_async = std::true_type;
  using is_awaiter = std::true_type;
  using exception_behavior = traits::handle_exceptions;
  using initial_suspend_type = std::suspend_always;
  using move_result = std::true_type;
};

// A task that starts immediately. Similar to Task but without the initial
// suspend.
struct AutoTaskTraits {
  using is_generator = std::false_type;
  using is_async = std::true_type;
  using is_awaiter = std::true_type;
  using exception_behavior = handle_exceptions;
  using initial_suspend_type = std::suspend_never;
  using move_result = std::true_type;
};

// A synchronous task that starts immediately. Does the same thing as a
// function call but wrapped as a coroutine.
struct ImmediateTraits {
  using is_generator = std::false_type;
  using is_async = std::false_type;
  using is_awaiter = std::false_type;
  using exception_behavior = rethrow_exceptions;
  using initial_suspend_type = std::suspend_never;
  using move_result = std::true_type;
};

// A synchronous task that suspends initially, and then calculates a single
// value on demand. This is the coroutine equivalent of a lambda.
struct LazyTraits {
  using is_generator = std::false_type;
  using is_async = std::false_type;
  using is_awaiter = std::false_type;
  using exception_behavior = rethrow_exceptions;
  using initial_suspend_type = std::suspend_always;
  using move_result = std::true_type;
};

// A task that produces a series of values on demand, synchronously.
struct GeneratorTraits {
  using is_generator = std::true_type;
  using is_async = std::false_type;
  using is_awaiter = std::false_type;
  using exception_behavior = rethrow_exceptions;
  using initial_suspend_type = std::suspend_always;
  using move_result = std::true_type;
};

// A task that produces a series of values on demand, asynchronously.
struct AsyncGeneratorTraits {
  using is_generator = std::true_type;
  using is_async = std::true_type;
  using is_awaiter = std::true_type;
  using exception_behavior = rethrow_exceptions;
  using initial_suspend_type = std::suspend_always;
  using move_result = std::true_type;
};

// A task that cannot be reliably awaited, but can await other tasks.
struct FireAndForgetTraits {
  using is_generator = std::false_type;
  using is_async = std::false_type;
  using is_awaiter = std::true_type;
  using exception_behavior = rethrow_exceptions;
  using initial_suspend_type = std::suspend_never;
  using move_result = std::true_type;
};

// Traits that change the struct to copy from the result data instead of move.
template<CoroutineTraits CT>
struct CopyResultTraits {
  using is_generator         = typename CT::is_generator;
  using is_async             = typename CT::is_async;
  using is_awaiter           = typename CT::is_awaiter;
  using exception_behavior   = typename CT::exception_behavior;
  using initial_suspend_type = typename CT::initial_suspend_type;
  using move_result = std::false_type;
};

} // namespace wscoro::traits

namespace wscoro {
  using traits::CoroutineTraits;
}
