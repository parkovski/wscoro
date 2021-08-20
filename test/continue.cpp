#include "wscoro/wscoro.h"

#include <catch2/catch_all.hpp>

using namespace wscoro;

template<class F>
struct ScopeExit {
  F f;

  ~ScopeExit() {
    f();
  }
};

template<class F>
ScopeExit<F> scope_exit(F f) {
  return ScopeExit<F>{std::move(f)};
}

static void step(int n, unsigned &flags) {
  flags |= 1 << n;
}

static Task<> get_second_task(unsigned &flags) {
  auto onexit = scope_exit([&] () { step(2, flags); });
  step(1, flags);
  co_return;
}

static Task<> get_first_task(unsigned &flags) {
  auto onexit = scope_exit([&] () { step(5, flags); });

  // First resume.
  step(0, flags);
  co_await std::suspend_always{};

  // Second resume
  co_await get_second_task(flags);
  // Continued from get_second_task.
  step(3, flags);
  co_await std::suspend_always{};

  // Third resume.
  step(4, flags);
  co_return;
}

TEST_CASE("Task continuation", "[continue]") {
  unsigned flags = 0;
  auto ta = get_first_task(flags);
  REQUIRE(flags == 0);
  ta.resume();
  REQUIRE(flags == 1);
  ta.resume();
  REQUIRE(flags == 15);
  ta.resume();
  REQUIRE(flags == 63);
  REQUIRE(ta.done());
}
