#pragma once

#include <coroutine>
#include <type_traits>
#include <cassert>

namespace wscoro {
namespace detail {

/// Alias to std::suspend_always or std::suspend_never.
/// \param Suspend Determines whether the coroutine should suspend at this
///        point.
template<bool Suspend>
using BasicSuspend =
  std::conditional_t<Suspend, std::suspend_always, std::suspend_never>;

/// Async suspender.
/// Provides an await_suspend that returns the continuation if one was present,
/// otherwise it simply returns to the awaiter.
struct SuspendWithContinuation {
  std::coroutine_handle<> _continuation;

  constexpr bool await_ready() const noexcept { return false; }

  constexpr std::coroutine_handle<>
  await_suspend(std::coroutine_handle<>) const noexcept {
    if (_continuation) {
      return _continuation;
    } else {
      return std::noop_coroutine();
    }
  }

  constexpr void await_resume() const noexcept {}
};

struct Continuation {
private:
  std::coroutine_handle<> _continuation = nullptr;

public:
  bool set_continuation(std::coroutine_handle<> continuation) noexcept {
    assert(!_continuation);
    _continuation = continuation;
    return true;
  }

  SuspendWithContinuation suspend_with_continuation() noexcept {
    auto continuation = _continuation;
    _continuation = nullptr;
    return {continuation};
  }
};

struct NoContinuation {
  constexpr bool set_continuation(std::coroutine_handle<>) const noexcept {
    return false;
  }
};

} // namespace detail

namespace suspend {

/// Provides an `initial_suspend` that either always or never suspends.
///
/// The initial suspend determines whether the coroutine starts executing at
/// creation time or waits until it is first resumed, either by a call to
/// `resume()`, `operator()()`, or by the `co_await` operator. The behavior of
/// using `co_await` on the task depends on whether or not it suspended
/// initially. This should usually be determined at the type level, but wscoro
/// allows it to be determined at runtime by implementing the
/// `did_initial_suspend()` method and returning the same value as
/// `initial_suspend().await_ready()` for the current instance.
///
/// \param Suspend Determines whether the coroutine suspends initially.
template<bool Suspend>
struct BasicInitialSuspend {
  /// This method must return the same value as
  /// `initial_suspend().await_ready()`.
  constexpr bool did_initial_suspend() const noexcept {
    return Suspend;
  }

  constexpr detail::BasicSuspend<Suspend> initial_suspend() const noexcept {
    return {};
  }
};

/// Provides a `final_suspend` that either always or never suspends.
///
/// The final suspend allows the awaiter of the coroutine to obtain the value
/// it produced, therefore the final suspend should only be disabled for
/// coroutines that return `void`.
///
/// \param Suspend Determines whether the coroutine suspends on completion.
template<bool Suspend>
struct BasicFinalSuspend : detail::NoContinuation {
  constexpr detail::BasicSuspend<Suspend> final_suspend() const noexcept {
    return {};
  }
};

/// Provides a `final_suspend` that can store and resume a continuation.
struct FinalSuspendWithContinuation : virtual detail::Continuation {
  detail::SuspendWithContinuation final_suspend() noexcept {
    return this->suspend_with_continuation();
  }
};

} // namespace suspend
} // namespace wscoro
