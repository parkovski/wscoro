#pragma once

#include <coroutine>
#include <type_traits>

namespace wscoro {

template<class Return, class Await, class... Mixins>
struct promise {
  template<class Task>
  struct type :
    public Return,
    public Await::template type<type<Task>>,
    public Mixins...
  {
    using value_type = typename Return::value_type;

    Task get_return_object() noexcept(
      std::is_nothrow_constructible_v<Task, std::coroutine_handle<type>>)
    {
      return Task{std::coroutine_handle<type>::from_promise(*this)};
    }
  };
};

} // namespace wscoro
