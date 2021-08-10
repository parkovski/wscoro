#pragma once

#include "traits.h"
#include "promise.h"

#include <typeinfo>

namespace wscoro::detail {

template<class P>
class CoroutineBase {
public:
  friend struct std::hash<CoroutineBase>;

  using promise_type = P;

protected:
  std::coroutine_handle<promise_type> _coroutine;

public:
  constexpr CoroutineBase(std::coroutine_handle<promise_type> coroutine)
    noexcept
    : _coroutine{coroutine}
  {}

  constexpr CoroutineBase(CoroutineBase &&o) noexcept
    : _coroutine{o._coroutine}
  {
    o._coroutine = nullptr;
  }

  constexpr CoroutineBase &operator=(CoroutineBase &&o) noexcept {
    _coroutine = o._coroutine;
    o._coroutine = nullptr;
    return *this;
  }

  CoroutineBase(const CoroutineBase &) = delete;
  CoroutineBase &operator=(const CoroutineBase &) = delete;

  ~CoroutineBase() {
    if (_coroutine) {
      _coroutine.destroy();
    }
  }

  friend void swap(CoroutineBase &a, CoroutineBase &b) noexcept {
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

protected:
  // promise_type &promise() noexcept {
  //   return _coroutine.promise();
  // }
  // const promise_type &promise() const noexcept {
  //   return _coroutine.promise();
  // }

  std::coroutine_handle<promise_type> detach() noexcept {
    auto coroutine = _coroutine;
    _coroutine = nullptr;
    return coroutine;
  }
};

} // namespace wscoro::detail

namespace std {
  template<class P>
  struct hash<::wscoro::detail::CoroutineBase<P>> {
    auto operator()(const ::wscoro::detail::CoroutineBase<P> &co)
      const noexcept
    {
      return hash<remove_cvref_t<decltype(co._coroutine)>>{}(co._coroutine);
    }
  };
}

namespace wscoro {

template<typename T, traits::BasicTaskTraits Traits>
class BasicCoroutine :
  public detail::CoroutineBase<Promise<BasicCoroutine<T, Traits>, T, Traits>>
{
  using base = detail::CoroutineBase<Promise<BasicCoroutine, T, Traits>>;

public:
  using typename base::promise_type;

  BasicCoroutine(std::coroutine_handle<promise_type> coroutine) noexcept
    : base{coroutine}
  {}

  BasicCoroutine(BasicCoroutine &&) = default;
  BasicCoroutine &operator=(BasicCoroutine &&) = default;

  BasicCoroutine(const BasicCoroutine &) = delete;
  BasicCoroutine &operator=(const BasicCoroutine &) = delete;

  ~BasicCoroutine() {
    // Coroutines without a final suspend will destroy themselves - by setting
    // this to null, the base class destructor will not try to destroy it.
    if constexpr (!Traits::final_suspend::value) {
      this->_coroutine = nullptr;
    }
  }

  template<bool FS = Traits::final_suspend::value,
           typename = std::enable_if_t<!FS>>
  auto detach() {
    return this->base::detach();
  }
};

template<typename T, traits::BasicTaskTraits Traits>
/*requires
  // A generator cannot yield void.
  (!std::is_void_v<T> || !Traits::is_generator::value) &&
  // A type with async=false and awaiter=true can't be reliably awaited.
  (Traits::is_async::value || !Traits::is_awaiter::value)*/
class BasicTask :
  public detail::CoroutineBase<Promise<BasicTask<T, Traits>, T, Traits>>
{
  using base = detail::CoroutineBase<Promise<BasicTask, T, Traits>>;

public:
  //
  using typename base::promise_type;

  // The type produced by awaiting this task.
  using value_type = T;

  using is_generator = typename Traits::is_generator;

  using is_async = typename Traits::is_async;

  constexpr BasicTask(std::coroutine_handle<promise_type> coroutine) noexcept
    : base{coroutine}
  {}

  BasicTask(BasicTask &&o) = default;
  BasicTask &operator=(BasicTask &&o) = default;

  BasicTask(const BasicTask &) = delete;
  BasicTask &operator=(const BasicTask &) = delete;

  // If the coroutine is already done, we can skip the suspend stage.
  bool await_ready() const;

  // Should be const but clang and coroutine_handle::resume.
  std::coroutine_handle<>
  await_suspend(std::coroutine_handle<> continuation)
    noexcept(Traits::is_async::value);

  // If exception behavior is to save and rethrow (handle_exceptions) and one
  // was thrown and not caught, it will be rethrown here. For non-void types,
  // this returns the value from the inner coroutine's co_return.
  // If the type is configured to copy instead of move, the data is not
  // consumed.
  decltype(auto) await_resume();
};

template<typename T, traits::BasicTaskTraits Traits>
// requires
//   (!std::is_void_v<T> || !Traits::is_generator::value) &&
//   (Traits::is_async::value || !Traits::is_awaiter::value)
bool BasicTask<T, Traits>::await_ready() const {
  if (this->done()) {
    return true;
  }
  if constexpr (Traits::is_generator::value) {
    return this->_coroutine.promise().has_value();
  } else {
    return false;
  }
}

template<typename T, traits::BasicTaskTraits Traits>
// requires
//   (!std::is_void_v<T> || !Traits::is_generator::value) &&
//   (Traits::is_async::value || !Traits::is_awaiter::value)
std::coroutine_handle<>
BasicTask<T, Traits>::await_suspend(std::coroutine_handle<> continuation)
  noexcept(Traits::is_async::value)
{
  if constexpr (!Traits::is_async::value) {
    this->resume();
    return continuation;
  } else {
    assert(!this->_coroutine.promise()._continuation);
    this->_coroutine.promise()._continuation = continuation;
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
      return this->_coroutine;
    }
  }
}

template<typename T, traits::BasicTaskTraits Traits>
// requires
//   (!std::is_void_v<T> || !Traits::is_generator::value) &&
//   (Traits::is_async::value || !Traits::is_awaiter::value)
decltype(auto) BasicTask<T, Traits>::await_resume() {
  if constexpr (std::is_same_v<typename Traits::exception_behavior,
                               traits::handle_exceptions>)
  {
    if (auto ex = this->_coroutine.promise()._exception) {
      this->_coroutine.promise()._exception = nullptr;
      std::rethrow_exception(ex);
    }
  }

  if constexpr (Traits::is_generator::value) {
    // Generators return optional<T>, nullopt at the end.
    if (this->done()) {
      return std::optional<T>{};
    }
    if constexpr (Traits::move_result::value) {
      return std::optional<T>{std::in_place,
                              std::move(this->_coroutine.promise()).data()};
    } else {
      return std::optional<T>{std::in_place,
                              this->_coroutine.promise().data()};
    }
  } else {
    if constexpr (std::is_void_v<T>) {
      return;
    } else if constexpr (Traits::move_result::value) {
      return T{std::move(this->_coroutine.promise()).data()};
    } else {
      return T{this->_coroutine.promise().data()};
    }
  }
}

// A synchronous task that starts immediately. Basically a coroutine wrapper
// for a simple function call.
template<typename T = void>
using Immediate = BasicTask<T, traits::ImmediateTraits>;

// A synchronous task that suspends initially, and then calculates a single
// value on demand. This is the coroutine equivalent of a lambda.
template<typename T = void>
using Lazy = BasicTask<T, traits::LazyTraits>;

// A general purpose task type. Starts suspended and may await other
// coroutines.
template<typename T = void>
using Task = BasicTask<T, traits::TaskTraits>;

// A task that starts immediately. Similar to Task but without the initial
// suspend.
template<typename T = void>
using AutoTask = BasicTask<T, traits::AutoTaskTraits>;

// A task that produces a series of values on demand, synchronously.
template<typename T>
using Generator = BasicTask<T, traits::GeneratorTraits>;

// A task that produces a series of values on demand, asynchronously.
template<typename T>
using AsyncGenerator = BasicTask<T, traits::AsyncGeneratorTraits>;

// A task that cannot be reliably awaited, but can await other tasks.
using FireAndForget = BasicCoroutine<void, traits::FireAndForgetTraits>;

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
