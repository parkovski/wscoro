#pragma once

#include <stdexcept>

namespace wscoro::exception {

// Ignores unhandled exceptions.
struct IgnoreExceptions {
  constexpr void unhandled_exception() const noexcept {}
  constexpr void rethrow_exception() const noexcept {}
};

// Stores the current exception to rethrow once control returns to the
// coroutine's caller.
struct AsyncThrow {
private:
  std::exception_ptr _exception = nullptr;

public:
  void unhandled_exception() noexcept {
    _exception = std::current_exception();
  }

  void rethrow_exception() const {
    if (_exception) {
      std::rethrow_exception(_exception);
    }
  }
};

// Immediately rethrows unhandled exceptions.
struct SyncThrow {
  [[noreturn]] void unhandled_exception() const {
    std::rethrow_exception(std::current_exception());
  }

  void rethrow_exception() const noexcept {}
};

} // namespace wscoro::exception
