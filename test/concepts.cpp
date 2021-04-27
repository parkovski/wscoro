#include "wscoro/task.h"

#include <catch2/catch_all.hpp>

#include <sstream>

using namespace wscoro;

class NotAwaitable {
  int i;

public:
  bool await_ready() const noexcept { return false; }
  void await_suspend(int i) noexcept { this->i = i; }
  int await_resume() const noexcept { return i; }
};

class DAwaitable {
public:
  bool await_ready() const noexcept { return false; }
  void await_suspend() const noexcept {}
  int await_resume() const noexcept { return 0; }
};

TEST_CASE("(Direct)Awaitable", "[concepts]") {
}
