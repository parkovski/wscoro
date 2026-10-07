#pragma once

#include <coroutine>
#include <type_traits>
#include <cassert>
#include <atomic>

namespace wscoro {
namespace detail {

/// Alias to std::suspend_always or std::suspend_never.
/// \param Suspend Determines whether the coroutine should suspend at this
///        point.
template<bool Suspend>
using basic_suspend =
  std::conditional_t<Suspend, std::suspend_always, std::suspend_never>;

struct with_continuation {
protected:
  std::coroutine_handle<> _continuation = nullptr;

public:
  void set_continuation(std::coroutine_handle<> continuation) noexcept {
    _continuation = continuation;
  }

  std::coroutine_handle<> continuation() const noexcept {
    return _continuation;
  }
};

struct resumer {
  std::coroutine_handle<> _coroutine;

  bool await_ready() const noexcept {
    return false;
  }

  std::coroutine_handle<>
  await_suspend(std::coroutine_handle<>) const noexcept {
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
struct basic_initial_suspend {
  constexpr detail::basic_suspend<Suspend> initial_suspend() const noexcept {
    return {};
  }

  constexpr bool did_initial_suspend() const noexcept {
    return Suspend;
  }
};

// Keeps track of whether the task is in its initial suspend. This could be
// useful for some types of schedulers. This type always suspends initially. If
// you don't want an initial suspend, use basic_initial_suspend<false>.
struct tracking_initial_suspend {
private:
  std::atomic_flag _flag{};

public:
  struct type {
    tracking_initial_suspend &_suspend;

    constexpr bool await_ready() const noexcept { return false; }
    void await_suspend() noexcept {
      _suspend._flag.test_and_set(std::memory_order_release);
    }
    void await_resume() noexcept {
      _suspend._flag.clear(std::memory_order_release);
    }
  };

  type initial_suspend() noexcept { return {*this}; }

  constexpr bool did_initial_suspend() const noexcept { return true; }

  bool is_initial_suspended() const noexcept {
    return _flag.test(std::memory_order_acquire);
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
struct basic_final_suspend {
  constexpr detail::basic_suspend<Suspend> final_suspend() const noexcept {
    return {};
  }
};

struct final_suspend_with_continuation 
  : detail::with_continuation {
  detail::resumer final_suspend() const noexcept {
    return {this->_continuation};
  }
};

} // namespace suspend
} // namespace wscoro
