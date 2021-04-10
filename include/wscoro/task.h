#pragma once

#include "tasktraits.h"
#include "promise.h"

#include <typeinfo>

namespace wscoro {

template<typename T, CoroutineTraits Traits>
class BasicTask {
public:
  // 
  struct promise_type : Promise<BasicTask, T, Traits> {};

  // The type produced by awaiting this task.
  using value_type = T;

  // If this is true (std::true_type), the task produces multiple values via
  // co_yield.
  using is_generator = typename Traits::is_generator;

  // If either of these is true (std::true_type), the task could execute
  // asynchronously. If both are false (std::false_type), awaiting the task
  // is synchronous.
  using is_async = std::disjunction<
    typename Traits::is_async,
    typename Traits::is_awaiter
  >;

protected:
  std::coroutine_handle<promise_type> _coroutine;

public:
  BasicTask(std::coroutine_handle<promise_type> coroutine) noexcept
    : _coroutine{coroutine}
  {}

  BasicTask(const BasicTask &) = delete;
  BasicTask &operator=(const BasicTask &) = delete;

  BasicTask(BasicTask &&) = default;
  BasicTask &operator=(BasicTask &&) = default;

  // Calling this when you're not supposed to will break things.
  // Note: should be const but coroutine_handle::resume is not const in clang.
  void resume() {
    _coroutine.resume();
  }

  bool done() const {
    return _coroutine.done();
  }

  // If the coroutine is already done, we can skip the suspend stage.
  bool await_ready() const {
    if constexpr (Traits::is_generator::value) {
      return _coroutine.promise().has_data();
    } else {
      return _coroutine.done();
    }
  }

  // Should be const but clang and coroutine_handle::resume.
  std::coroutine_handle<>
  await_suspend(std::coroutine_handle<> continuation)
    noexcept(Traits::is_async::value)
  {
    if constexpr (Traits::is_generator::value) {
      // If a generator is done and someone awaits it, we "eat" that
      // continuation and fully suspend to this task's awaiter.
      if (_coroutine.done()) {
        log::warn("Generator discarded continuation because it was finished.");
        continuation.destroy();
        return std::noop_coroutine();
      }
    }

    if constexpr (Traits::is_async::value) {
      assert(!_coroutine.promise()._continuation);
      _coroutine.promise()._continuation = continuation;
      return _coroutine;
    } else {
      _coroutine.resume();
      return continuation;
      // return _coroutine;
    }
  }

  // If exception behavior is to save and rethrow (handle_exceptions) and one
  // was thrown and not caught, it will be rethrown here. For non-void types,
  // this returns the value from the inner coroutine's co_return.
  template<bool HandleExceptions = std::is_same_v<
      typename Traits::exception_behavior, traits::handle_exceptions>>
  T await_resume() {
    if constexpr (HandleExceptions) {
      if (auto ex = _coroutine.promise()._exception) {
        std::rethrow_exception(ex);
      }
    }

    if constexpr (std::is_void_v<T>) {
      return;
    }

    if constexpr (std::is_move_constructible_v<T>) {
      if constexpr (Traits::move_result::value ||
                    !std::is_copy_constructible_v<T>) {
        return std::move(_coroutine.promise()).take_data();
      }
    }

    static_assert(std::is_copy_constructible_v<T>,
                  "Type cannot be moved or copied.");
    return _coroutine.promise().take_data();
  }
};

template<typename T = void>
using Task = BasicTask<T, traits::TaskTraits>;

template<typename T = void>
using AutoTask = BasicTask<T, traits::AutoTaskTraits>;

template<typename T = void>
using Immediate = BasicTask<T, traits::ImmediateTraits>;

template<typename T = void>
using Lazy = BasicTask<T, traits::LazyTraits>;

template<typename T>
using Generator = BasicTask<T, traits::GeneratorTraits>;

template<typename T>
using AsyncGenerator = BasicTask<T, traits::AsyncGeneratorTraits>;

using FireAndForget = BasicTask<void, traits::FireAndForgetTraits>;

// -----

// A copy task copies the result data instead of moving.
template<typename T, CoroutineTraits Traits>
using BasicCopyTask = BasicTask<T, traits::CopyResultTraits<Traits>>;

template<typename T = void>
using CopyTask = BasicCopyTask<T, traits::TaskTraits>;

template<typename T = void>
using CopyAutoTask = BasicCopyTask<T, traits::AutoTaskTraits>;

template<typename T = void>
using CopyImmediate = BasicCopyTask<T, traits::ImmediateTraits>;

template<typename T = void>
using CopyLazy = BasicCopyTask<T, traits::LazyTraits>;

template<typename T>
using CopyGenerator = BasicCopyTask<T, traits::GeneratorTraits>;

template<typename T>
using CopyAsyncGenerator = BasicCopyTask<T, traits::AsyncGeneratorTraits>;

using CopyFireAndForget = BasicCopyTask<void, traits::FireAndForgetTraits>;

} // namespace wscoro
