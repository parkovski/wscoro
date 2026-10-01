#pragma once

#include "suspend.h"

#include <coroutine>
#include <type_traits>

namespace wscoro {

template<class T>
struct SyncScheduler {
  T &_task;

  std::coroutine_handle<>
  operator()(std::coroutine_handle<> continuation) const {
    if constexpr (std::is_base_of_v<detail::Continuation, typename T::promise_type>) {
      _task.promise().set_continuation(continuation);
      return _task.handle();
    } else {
      _task.resume();
      return continuation;
    }
  }
};

} // namespace wscoro