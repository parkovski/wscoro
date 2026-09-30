#pragma once

#include <coroutine>

namespace wscoro {

struct SyncScheduler {
  void operator()(std::coroutine_handle<> coroutine) const {
    coroutine.resume();
  }
};

} // namespace wscoro