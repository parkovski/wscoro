#pragma once

#include <coroutine>
#include <type_traits>

namespace wscoro {

template<class Return, class Catch, class Await,
         class InitialSuspend, class FinalSuspend>
struct Promise {
  using value_type = typename Return::value_type;

  template<class Task>
  struct type : public Return, public Catch, public Await,
                public InitialSuspend, public FinalSuspend
  {
    using value_type = typename Return::value_type;

    Task get_return_object() noexcept(
      std::is_nothrow_constructible_v<Task, std::coroutine_handle<type>>)
    {
      return {std::coroutine_handle<type>::from_promise(*this)};
    }
  };
};

} // namespace wscoro
