#pragma once

#include <coroutine>

namespace wscoro {

template<class T>
struct SyncScheduler {
  T &_task;

  std::coroutine_handle<>
  operator()(std::coroutine_handle<> continuation) const {
    _task.resume();
    return continuation;
  }
};

} // namespace wscoro