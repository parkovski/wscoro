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

struct Continuation {
protected:
  std::coroutine_handle<> _continuation = nullptr;

public:
  bool set_continuation(std::coroutine_handle<> continuation) noexcept {
    _continuation = continuation;
    return true;
  }

  std::coroutine_handle<> continuation() const noexcept {
    return _continuation;
  }
};

struct NoContinuation {
  bool set_continuation(std::coroutine_handle<>) noexcept {
    return false;
  }

  std::coroutine_handle<> continuation() const noexcept {
    return nullptr;
  }
};

struct Resumer {
  std::coroutine_handle<> _coroutine;

  bool await_ready() const noexcept {
    return false;
  }

  std::coroutine_handle<> await_suspend(std::coroutine_handle<>) const noexcept {
    if (_coroutine) {
      return _coroutine;
    }
    return std::noop_coroutine();
  }

  void await_resume() const noexcept {}
};

} // namespace detail

namespace suspend {

/// Provides an `initial_suspend` that either always or never suspends.
///
/// The initial suspend determines whether the coroutine starts executing at
/// creation time or waits until it is first resumed, either by a call to
/// `resume()`, `operator()()`, or by the `co_await` operator. The behavior of
/// using `co_await` on the task depends on whether or not it suspended
/// initially.
///
/// \param Suspend Determines whether the coroutine suspends initially.
template<bool Suspend>
struct BasicInitialSuspend {
  constexpr detail::BasicSuspend<Suspend> initial_suspend() const noexcept {
    return {};
  }

  constexpr bool did_initial_suspend() const noexcept {
    return Suspend;
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
struct BasicFinalSuspend : NoContinuation {
  constexpr detail::BasicSuspend<Suspend> final_suspend() const noexcept {
    return {};
  }
};

struct FinalSuspendWithContinuation 
  : virtual detail::Continuation {
  detail::Resumer final_suspend() const noexcept {
    return {this->_continuation};
  }
};

} // namespace suspend
} // namespace wscoro
