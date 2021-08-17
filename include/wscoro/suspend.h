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

  bool await_ready() const noexcept { return !_continuation; }

  std::coroutine_handle<>
  await_suspend(std::coroutine_handle<>) noexcept {
    if (_continuation) {
      return _continuation;
    } else {
      return std::noop_coroutine();
    }
  }

  void await_resume() const noexcept {}
};

struct Continuation {
private:
  std::coroutine_handle<> _continuation;

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
  constexpr bool set_continuation(std::coroutine_handle<>) noexcept {
    return false;
  }
};

} // namespace detail

namespace suspend {

/// Provides an `initial_suspend` that either always or never suspends.
/// \param Suspend Determines whether the coroutine suspends initially.
template<bool Suspend>
struct BasicInitialSuspend {
  constexpr bool did_initial_suspend() const noexcept {
    return Suspend;
  }

  constexpr detail::BasicSuspend<Suspend> initial_suspend() const noexcept {
    return {};
  }
};

/// Provides a `final_suspend` that either always or never suspends.
/// \param Suspend Determines whether the coroutine suspends on completion.
template<bool Suspend>
struct BasicFinalSuspend : detail::NoContinuation {
  constexpr detail::BasicSuspend<Suspend> final_suspend() const noexcept {
    return {};
  }
};

struct FinalSuspendWithContinuation : virtual detail::Continuation {
  detail::SuspendWithContinuation final_suspend() noexcept {
    return this->suspend_with_continuation();
  }
};

} // namespace suspend
} // namespace wscoro
