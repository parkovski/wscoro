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
  std::is_convertible_v<T, std_::coroutine_handle<>>;

template<typename A, typename P = void>
concept DirectAwaitable = requires (A a, std_::coroutine_handle<P> h) {
  bool(a.await_ready());
  a.await_resume();
  requires requires { { a.await_suspend(h) } -> AwaitSuspendReturn; } ||
           requires { { a.await_suspend() }  -> AwaitSuspendReturn; };
};

template<class A, typename P = void>
concept Awaiter =
  requires (A a) { { a.operator co_await() } -> DirectAwaitable<P>; } ||
  requires (A a) { { operator co_await(a) }  -> DirectAwaitable<P>; } ||
  DirectAwaitable<A, P>;

template<class P, class C = decltype(std::declval<P>().get_return_object())>
concept PartialPromise = requires(P p) {
  { p.get_return_object() } -> std_::same_as<C>;
  { p.initial_suspend() } -> Awaiter<P>;
  { p.final_suspend() }   -> Awaiter<P>;
};

template<class P>
concept PromiseReturnVoid = requires (P p) { p.return_void(); };

template<class P, class T>
concept PromiseReturnValue = requires (P p, T t) {
  p.return_value(std::move(t));
};

template<class P, class T>
concept PromiseReturn =
  (std::is_void_v<T> && requires(P p) { p.return_void(); }) ||
  requires (P p, T t) { p.return_value(std::move(t)); };

template<class P>
concept VoidPromise = PartialPromise<P> && PromiseReturnVoid<P>;

template<class P, class T>
concept ValuePromise = PartialPromise<P> && PromiseReturnValue<P, T>;

template<class P, class T>
concept GeneratorPromise = PartialPromise<P> && PromiseReturnVoid<P> &&
  requires (P p, T t) {
    { p.yield_value(std::move(t)) } -> Awaiter;
  };

template<class P, class T = std::nullptr_t, class C = std::nullptr_t>
concept Promise = (std::is_void_v<T> && VoidPromise<P>) ||
  ValuePromise<P, T> ||
  GeneratorPromise<P, T>;

template<class A, class P>
concept Awaitable =
  requires (A a, P p) { { p.await_transform(a) } -> Awaiter; } ||
  Awaiter<A>;

template<class C, class T>
concept Coroutine =
  Promise<typename C::promise_type, T, C> &&
  requires(C c) {
    c.resume();
    c.destroy();
    { c.done() } -> std_::same_as<bool>;
    c.operator bool();
  };

template<class C>
concept Detachable = requires (C c) { c.detach(); };

template<class TaskT, class T>
concept Tasklike = Coroutine<TaskT, T> && Awaiter<TaskT>;

// Any type that specifies these options is usable as traits for a Task.
// bool_constant::value is actually a const bool, so trying to use same_as
// is annoying.
template<typename T>
concept BasicTaskTraits =
  Awaiter<typename T::initial_suspend_type> &&
  ExceptionBehavior<typename T::exception_behavior> &&
  requires {
    (bool)T::is_generator::value;
    (bool)T::is_awaiter::value;
    (bool)T::is_async::value;
    (bool)T::move_result::value;
  };

template<bool Async>
struct DefaultTaskTraits {
  // The coroutine supports the co_yield expression. If this is supported,
  // a return type of void is enforced and the task type instead becomes the
  // type of the yielded value.
  using is_generator = std::false_type;

  // The coroutine supports a saved continuation when others co_await this.
  // If this is true, awaiters of this task may be resumed asynchronously. If
  // this is false, this task does not contain a continuation, but if it can
  // await other coroutines, its execution still may not be synchronous. For
  // this reason, with a coroutine A awaiting a sync task B that awaits an
  // async task C,
  using is_async = std::bool_constant<Async>;

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
    std::conditional_t<Async, handle_exceptions, rethrow_exceptions>;

  // Type returned from promise's initial_suspend - should be
  // std_::suspend_never or std_::suspend_always (default), but any awaiter is
  // valid if there's a reason to customize this further. Issuing a suspend
  // here will stop the coroutine before any of its work is done. Otherwise
  // the coroutine will immediately run to the next suspend point on
  // creation.
  using initial_suspend_type = std_::suspend_always;

  // Disabling this is only useful when you don't care about the result. It
  // causes the coroutine to self-destruct when it finishes.
  using final_suspend = std::true_type;

  // Try to move data out of the promise. Data is only copied if a move can't
  // be done. If this is false, data is moved only when a copy can't be done.
  // Ignored when the return type is void.
  using move_result = std::true_type;
};

struct ImmediateTraits {
  using is_generator = std::false_type;
  using is_async = std::false_type;
  using is_awaiter = std::false_type;
  using exception_behavior = rethrow_exceptions;
  using initial_suspend_type = std_::suspend_never;
  using final_suspend = std::true_type;
  using move_result = std::true_type;
};

struct LazyTraits {
  using is_generator = std::false_type;
  using is_async = std::false_type;
  using is_awaiter = std::false_type;
  using exception_behavior = rethrow_exceptions;
  using initial_suspend_type = std_::suspend_always;
  using final_suspend = std::true_type;
  using move_result = std::true_type;
};

struct TaskTraits {
  using is_generator = std::false_type;
  using is_async = std::true_type;
  using is_awaiter = std::true_type;
  using exception_behavior = traits::handle_exceptions;
  using initial_suspend_type = std_::suspend_always;
  using final_suspend = std::true_type;
  using move_result = std::true_type;
};

struct AutoTaskTraits {
  using is_generator = std::false_type;
  using is_async = std::true_type;
  using is_awaiter = std::true_type;
  using exception_behavior = handle_exceptions;
  using initial_suspend_type = std_::suspend_never;
  using final_suspend = std::true_type;
  using move_result = std::true_type;
};

struct GeneratorTraits {
  using is_generator = std::true_type;
  using is_async = std::false_type;
  using is_awaiter = std::false_type;
  using exception_behavior = rethrow_exceptions;
  using initial_suspend_type = std_::suspend_always;
  using final_suspend = std::true_type;
  using move_result = std::true_type;
};

struct AsyncGeneratorTraits {
  using is_generator = std::true_type;
  using is_async = std::true_type;
  using is_awaiter = std::true_type;
  using exception_behavior = rethrow_exceptions;
  using initial_suspend_type = std_::suspend_always;
  using final_suspend = std::true_type;
  using move_result = std::true_type;
};

struct FireAndForgetTraits {
  using is_generator = std::false_type;
  using is_async = std::false_type;
  using is_awaiter = std::true_type;
  using exception_behavior = rethrow_exceptions;
  using initial_suspend_type = std_::suspend_always;
  using final_suspend = std::false_type;
  using move_result = std::true_type;
};

// Traits that change the struct to copy from the result data instead of move.
template<BasicTaskTraits CT>
struct CopyResultTraits {
  using is_generator         = typename CT::is_generator;
  using is_async             = typename CT::is_async;
  using is_awaiter           = typename CT::is_awaiter;
  using exception_behavior   = typename CT::exception_behavior;
  using initial_suspend_type = typename CT::initial_suspend_type;
  using final_suspend        = typename CT::final_suspend;
  using move_result = std::false_type;
};

} // namespace wscoro::traits
