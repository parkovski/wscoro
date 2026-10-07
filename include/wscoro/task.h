#pragma once

#include <typeinfo>
#include <type_traits>
#include <coroutine>
#include <optional>
#include <cassert>

namespace wscoro {

namespace detail {

/// Coroutine base type.
/// \param P The promise type.
template<class P>
struct coroutine_base {
  using promise_type = P;

protected:
  std::coroutine_handle<promise_type> _handle;

public:
  explicit coroutine_base(std::coroutine_handle<promise_type> handle) noexcept
    : _handle{handle}
  {}

  coroutine_base(const coroutine_base &) = delete;
  coroutine_base &operator=(const coroutine_base &) = delete;

  coroutine_base(coroutine_base &&o) noexcept : _handle{o._handle} {
    o._handle = nullptr;
  }

  coroutine_base &operator=(coroutine_base &&o) = delete;

  friend void swap(coroutine_base &a, coroutine_base &b) noexcept {
    using std::swap;
    swap(a._handle, b._handle);
  }

  std::coroutine_handle<promise_type>
  handle() const noexcept {
    return _handle;
  }

  promise_type &promise() const noexcept {
    return _handle.promise();
  }

  bool done() const noexcept {
    return !_handle || _handle.done();
  }

  explicit operator bool() const noexcept {
    return _handle != nullptr;
  }

  void operator()() const {
    _handle();
  }

  void resume() const {
    _handle.resume();
  }

  void destroy() {
    _handle.destroy();
    _handle = nullptr;
  }
};

} // namespace detail

/// This type is not awaitable and it does not destroy the coroutine when it
/// finishes - the coroutine is entirely on its own.
/// \param P The coroutine's promise type.
template<class P>
struct basic_coroutine
  : detail::coroutine_base<typename P::template type<basic_coroutine<P>>>
{
  using base =
    detail::coroutine_base<typename P::template type<basic_coroutine<P>>>;

public:
  using typename base::promise_type;
  using outer_promise_type = P;
  using value_type = void;

  using base::base;

  basic_coroutine(basic_coroutine &&) = default;
};

template<class T>
struct sync_scheduler;

template<class T, template<class> class S>
struct basic_task_awaiter;

template<
  class P,
  template<class> class S = sync_scheduler,
  template<class, template<class> class> class A = basic_task_awaiter
>
struct basic_task;

template<class T, template<class> class S>
struct basic_task_awaiter {
protected:
  T &_task;

public:
  basic_task_awaiter(T &task) noexcept
    : _task{task}
  {}

  bool await_ready() const noexcept {
    return _task.done();
  }

  decltype(auto)
  await_suspend(std::coroutine_handle<> continuation) const noexcept {
    return S<T>{_task}(continuation);
  }

  // If exception behavior is to save and rethrow (AsyncThrow) and one
  // was thrown and not caught, it will be rethrown here. For non-void types,
  // this returns the value from the inner coroutine's co_return.
  typename T::value_type await_resume() const {
    auto &promise = _task.promise();
    promise.rethrow_exception();
    return std::move(promise).data();
  }
};

template<
  class P,
  template<class> class S,
  template<class, template<class> class> class A
>
struct basic_task final
  : detail::coroutine_base<typename P::template type<basic_task<P, S, A>>>
{
  using base =
    detail::coroutine_base<typename P::template type<basic_task<P, S, A>>>;

public:
  using typename base::promise_type;
  using outer_promise_type = P;

  // The type produced by awaiting this task.
  using value_type = typename promise_type::value_type;

  using base::base;

  basic_task(basic_task &&) = default;

  ~basic_task() {
    if (this->_handle) {
      this->_handle.destroy();
    }
  }

  A<basic_task, S> operator co_await()
    noexcept(
      std::is_nothrow_constructible_v<A<basic_task, S>, basic_task &>
    ) {
    return {*this};
  }
};

template<class G, template<class> class S>
struct basic_generator_awaiter;

template<
  class P,
  template<class> class S = sync_scheduler,
  template<class, template<class> class> class A = basic_generator_awaiter
>
struct basic_generator;

template<class G, template<class> class S>
struct basic_generator_awaiter {
protected:
  G &_gen;

public:
  basic_generator_awaiter(G &gen) noexcept
    : _gen{gen}
  {}

  bool await_ready() const noexcept {
    return _gen.promise().has_value() || _gen.done();
  }

  decltype(auto)
  await_suspend(std::coroutine_handle<> continuation) const noexcept {
    return S<G>{_gen}(continuation);
  }

  typename G::value_type await_resume() const {
    auto &promise = _gen.promise();
    promise.rethrow_exception();
    if (_gen.done()) {
      return std::nullopt;
    }
    assert(promise.has_value());
    return {std::move(promise).data()};
  }
};

template<
  class P,
  template<class> class S,
  template<class, template<class> class> class A
>
struct basic_generator final
  : detail::coroutine_base<typename P::template type<basic_generator<P, S, A>>>
{
  using base =
    detail::coroutine_base<typename P::template type<basic_generator<P, S, A>>>;

public:
  using typename base::promise_type;
  using outer_promise_type = P;

  using value_type = std::optional<typename promise_type::value_type>;

  using base::base;

  basic_generator(basic_generator &&) = default;

  ~basic_generator() {
    if (this->_handle) {
      this->_handle.destroy();
    }
  }

  A<basic_generator, S> operator co_await()
    noexcept(
      std::is_nothrow_constructible_v<A<basic_generator, S>, basic_generator &>
    ) {
    return {*this};
  }
};

} // namespace wscoro

namespace std {
  template<class P>
  struct hash<::wscoro::detail::coroutine_base<P>> {
    size_t operator()(const ::wscoro::detail::coroutine_base<P> &co) const
      noexcept
    {
      return hash<remove_cvref_t<decltype(co.handle())>>{}(co.handle());
    }
  };

  template<class P>
  struct hash<::wscoro::basic_coroutine<P>> {
    size_t operator()(const ::wscoro::basic_coroutine<P> &co) const
      noexcept
    {
      return hash<remove_cvref_t<decltype(co.handle())>>{}(co.handle());
    }
  };

  template<
    class P,
    template<class> class S,
    template<class, template<class> class> class A
  >
  struct hash<::wscoro::basic_task<P, S, A>> {
    size_t operator()(const ::wscoro::basic_task<P, S, A> &co) const
      noexcept
    {
      return hash<remove_cvref_t<decltype(co.handle())>>{}(co.handle());
    }
  };

  template<
    class P,
    template<class> class S,
    template<class, template<class> class> class A
  >
  struct hash<::wscoro::basic_generator<P, S, A>> {
    size_t operator()(const ::wscoro::basic_generator<P, S, A> &co) const
      noexcept
    {
      return hash<remove_cvref_t<decltype(co.handle())>>{}(co.handle());
    }
  };
}
