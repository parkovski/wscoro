#pragma once

#include <type_traits>
#include <coroutine>

namespace wscoro::concepts {

template<class P>
concept ExceptionHandler = requires(P p) {
  { p.unhandled_exception() } -> std::same_as<void>;
};

template<class T>
concept AwaitSuspendReturn =
  std::is_same_v<T, void> ||
  std::is_same_v<T, bool> ||
  std::is_convertible_v<T, std::coroutine_handle<>>;

template<class A, class P = void>
concept DirectAwaitable = requires (A a, std::coroutine_handle<P> h) {
  bool(a.await_ready());
  a.await_resume();
  requires requires { { a.await_suspend(h) } -> AwaitSuspendReturn; } ||
           requires { { a.await_suspend() }  -> AwaitSuspendReturn; };
};

template<class A, class P = void>
concept Awaiter =
  requires (A a) { { a.operator co_await() } -> DirectAwaitable<P>; } ||
  requires (A a) { { operator co_await(a) }  -> DirectAwaitable<P>; } ||
  DirectAwaitable<A, P>;

template<class P, class C = decltype(std::declval<P>().get_return_object())>
concept PartialPromise = ExceptionHandler<P> && requires(P p) {
  { p.get_return_object() } -> std::same_as<C>;
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
    { c.done() } -> std::same_as<bool>;
    c.operator bool();
  };

template<class TaskT, class T>
concept Tasklike = Coroutine<TaskT, T> && Awaiter<TaskT>;

#if 0
struct ImmediateTraits {
  using is_generator = std::false_type;
  using is_async = std::false_type;
  using is_awaiter = std::false_type;
  using exception_behavior = rethrow_exceptions;
  using initial_suspend_type = std::suspend_never;
  using final_suspend = std::true_type;
  using move_result = std::true_type;
};

struct LazyTraits {
  using is_generator = std::false_type;
  using is_async = std::false_type;
  using is_awaiter = std::false_type;
  using exception_behavior = rethrow_exceptions;
  using initial_suspend_type = std::suspend_always;
  using final_suspend = std::true_type;
  using move_result = std::true_type;
};

struct TaskTraits {
  using is_generator = std::false_type;
  using is_async = std::true_type;
  using is_awaiter = std::true_type;
  using exception_behavior = traits::handle_exceptions;
  using initial_suspend_type = std::suspend_never;
  using final_suspend = std::true_type;
  using move_result = std::true_type;
};

struct DelayTaskTraits {
  using is_generator = std::false_type;
  using is_async = std::true_type;
  using is_awaiter = std::true_type;
  using exception_behavior = handle_exceptions;
  using initial_suspend_type = std::suspend_always;
  using final_suspend = std::true_type;
  using move_result = std::true_type;
};

struct GeneratorTraits {
  using is_generator = std::true_type;
  using is_async = std::false_type;
  using is_awaiter = std::false_type;
  using exception_behavior = rethrow_exceptions;
  using initial_suspend_type = std::suspend_always;
  using final_suspend = std::true_type;
  using move_result = std::true_type;
};

struct AsyncGeneratorTraits {
  using is_generator = std::true_type;
  using is_async = std::true_type;
  using is_awaiter = std::true_type;
  using exception_behavior = rethrow_exceptions;
  using initial_suspend_type = std::suspend_always;
  using final_suspend = std::true_type;
  using move_result = std::true_type;
};

struct FireAndForgetTraits {
  using is_generator = std::false_type;
  using is_async = std::false_type;
  using is_awaiter = std::true_type;
  using exception_behavior = rethrow_exceptions;
  using initial_suspend_type = std::suspend_always;
  using final_suspend = std::false_type;
  using move_result = std::true_type;
};
#endif

} // namespace wscoro::concepts
