#pragma once

#include "traits.h"
#include "promise.h"

#include <typeinfo>

namespace wscoro {

template<typename T, traits::BasicTaskTraits Traits>
requires
  // A generator cannot yield void.
  (!std::is_void_v<T> || !Traits::is_generator::value) &&
  // A type with async=false and awaiter=true must return void since it
  // can't be reliably awaited.
  (Traits::is_async::value || !Traits::is_awaiter::value || std::is_void_v<T>)
class BasicTask {
public:
  // 
  struct promise_type : Promise<BasicTask, T, Traits> {};

  // The type produced by awaiting this task.
  using value_type = T;

  // If this is true (std::true_type), the task produces multiple values via
  // co_yield.
  using is_generator = typename Traits::is_generator;

  // This means the promise has a continuation. Without one, awaiting the
  // task will block the coroutine until it is done.
  using is_async = typename Traits::is_async;

  friend struct std::hash<BasicTask>;

protected:
  std::coroutine_handle<promise_type> _coroutine;

public:
  constexpr BasicTask(std::coroutine_handle<promise_type> coroutine) noexcept
    : _coroutine{coroutine}
  {}

  constexpr BasicTask(BasicTask &&o) noexcept
    : _coroutine{o._coroutine}
  {
    o._coroutine = nullptr;
  }

  constexpr BasicTask &operator=(BasicTask &&o) noexcept {
    _coroutine = o._coroutine;
    o._coroutine = nullptr;
    return *this;
  }

  BasicTask(const BasicTask &) = delete;
  BasicTask &operator=(const BasicTask &) = delete;

  ~BasicTask() {
    if (_coroutine) {
      _coroutine.destroy();
    }
  }

  friend void swap(BasicTask &a, BasicTask &b) noexcept {
    using std::swap;
    swap(a._coroutine, b._coroutine);
  }

  bool done() const {
    return !_coroutine || _coroutine.done();
  }

  explicit operator bool() const noexcept {
    return _coroutine != nullptr;
  }

  // Calling this when you're not supposed to will break things.
  // Note: should be const but coroutine_handle::resume is not const in clang.
  void resume() {
    _coroutine.resume();
  }

  void destroy() {
    _coroutine.destroy();
    _coroutine = nullptr;
  }

  // If the coroutine is already done, we can skip the suspend stage.
  bool await_ready() const {
    if (done()) {
      return true;
    }
    if constexpr (Traits::is_generator::value) {
      return _coroutine.promise().has_value();
    } else {
      return false;
    }
  }

  // Should be const but clang and coroutine_handle::resume.
  std::coroutine_handle<>
  await_suspend(std::coroutine_handle<> continuation)
    noexcept(Traits::is_async::value)
  {
    if constexpr (!Traits::is_async::value) {
      resume();
      return continuation;
    } else {
      assert(!_coroutine.promise()._continuation);
      _coroutine.promise()._continuation = continuation;
      // TODO: The following line should work but LLVM's await_ready is not
      // constexpr.
      // if constexpr (typename Traits::initial_suspend_type{}.await_ready()) {
      if constexpr (
        std::is_convertible_v<typename Traits::initial_suspend_type,
                              std::suspend_never>) {
        // If there is no initial suspend, the coroutine starts
        // automatically, so we should not resume it at an arbitrary location.
        return std::noop_coroutine();
      } else {
        // If there is an initial suspend, await is the mechanism to start the
        // coroutine.
        return _coroutine;
      }
    }
  }

  // If exception behavior is to save and rethrow (handle_exceptions) and one
  // was thrown and not caught, it will be rethrown here. For non-void types,
  // this returns the value from the inner coroutine's co_return.
  // If the type is configured to copy instead of move, the data is not
  // consumed.
  decltype(auto) await_resume() {
    if constexpr (std::is_same_v<typename Traits::exception_behavior,
                                 traits::handle_exceptions>)
    {
      if (auto ex = _coroutine.promise()._exception) {
        _coroutine.promise()._exception = nullptr;
        std::rethrow_exception(ex);
      }
    }

    if constexpr (Traits::is_generator::value) {
      // Generators return optional<T>, nullopt at the end.
      if (_coroutine.done()) {
        return std::optional<T>{};
      } else if constexpr (Traits::move_result::value) {
        return std::optional<T>{std::in_place,
                                std::move(_coroutine.promise()).data()};
      } else {
        return std::optional<T>{std::in_place, _coroutine.promise().data()};
      }
    } else {
      if constexpr (std::is_void_v<T>) {
        return;
      } else if constexpr (Traits::move_result::value) {
        return T{std::move(_coroutine.promise()).data()};
      } else {
        return T{_coroutine.promise().data()};
      }
    }
  }
};

template<typename T = void>
using Immediate = BasicTask<T, traits::ImmediateTraits>;

template<typename T = void>
using Lazy = BasicTask<T, traits::LazyTraits>;

template<typename T = void>
using Task = BasicTask<T, traits::TaskTraits>;

template<typename T = void>
using AutoTask = BasicTask<T, traits::AutoTaskTraits>;

template<typename T>
using Generator = BasicTask<T, traits::GeneratorTraits>;

template<typename T>
using AsyncGenerator = BasicTask<T, traits::AsyncGeneratorTraits>;

using FireAndForget = BasicTask<void, traits::FireAndForgetTraits>;

// -----

// A copy task copies the result data instead of moving.
template<typename T, traits::BasicTaskTraits Traits,
         typename = std::enable_if_t<!std::is_void_v<T>>>
using BasicCopyTask = BasicTask<T, traits::CopyResultTraits<Traits>>;

template<typename T>
using CopyImmediate = BasicCopyTask<T, traits::ImmediateTraits>;

template<typename T>
using CopyLazy = BasicCopyTask<T, traits::LazyTraits>;

template<typename T>
using CopyTask = BasicCopyTask<T, traits::TaskTraits>;

template<typename T>
using CopyAutoTask = BasicCopyTask<T, traits::AutoTaskTraits>;

template<typename T>
using CopyGenerator = BasicCopyTask<T, traits::GeneratorTraits>;

template<typename T>
using CopyAsyncGenerator = BasicCopyTask<T, traits::AsyncGeneratorTraits>;

} // namespace wscoro

namespace std {
  template<typename T, ::wscoro::traits::BasicTaskTraits Traits>
  struct hash<::wscoro::BasicTask<T, Traits>> {
    auto operator()(const ::wscoro::BasicTask<T, Traits> &task) const noexcept {
      return hash<remove_cvref_t<decltype(task._coroutine)>>{}(task._coroutine);
    }
  };
}
