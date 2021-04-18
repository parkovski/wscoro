#define WSCORO_LOG g_logger
#include "wscoro/task.h"

#include <catch2/catch_all.hpp>

#include <string>
#include <string_view>
#include <sstream>

using namespace wscoro;

Immediate<int> add_one(int value) {
  co_return value + 1;
}

TEST_CASE("Test Immediate", "[task]") {
  auto two = add_one(1);
  REQUIRE(two.done());
  REQUIRE(two.await_resume() == 2);
}

template<template<typename> typename TTask>
static TTask<int> get_one(int &counter) {
  auto final_inc = scope_exit([&] { ++counter; });
  ++counter;
  co_await std::suspend_always{};
  ++counter;
  co_return 1;
}

TEST_CASE("Basic Task suspension", "[task]") {
  int counter = 0;
  auto get_one = ::get_one<Task>(counter);
  REQUIRE(counter == 0);

  get_one.resume();
  REQUIRE(!get_one.done());
  REQUIRE(counter == 1);

  get_one.resume();
  REQUIRE(get_one.done());
  REQUIRE(counter == 3);

  REQUIRE(get_one.await_resume() == 1);
}

TEST_CASE("Basic AutoTask suspension", "[task]") {
  int counter = 0;
  auto get_one = ::get_one<AutoTask>(counter);
  REQUIRE(counter == 1);

  get_one.resume();
  REQUIRE(get_one.done());
  REQUIRE(counter == 3);

  REQUIRE(get_one.await_resume() == 1);
}

Lazy<> increment(int &x) {
  x += 1;
  co_return;
}

TEST_CASE("Basic Lazy use", "[task]") {
  int x = 1;
  auto inc = increment(x);
  REQUIRE(x == 1);
  inc.resume();
  REQUIRE(inc.done());
  REQUIRE(x == 2);
}

static Generator<int> fibonacci() {
  int a0 = 1;
  int a1 = 1;
  while (true) {
    co_yield a0;
    co_yield a1;
    a0 += a1;
    a1 += a0;
  }
}

static AsyncGenerator<int> async_fib() {
  auto fib = fibonacci();
  while (true) {
    co_yield co_await fib;
  }
}

template<typename G>
static AutoTask<std::string> get_seq(G generator, int rounds) {
  std::stringstream ss;
  if (rounds > 0) {
    for (int i = 0; i < rounds - 1; i++) {
      ss << co_await generator << " ";
    }
    ss << co_await generator;
  }
  co_return std::move(ss).str();
}

TEST_CASE("Fibonacci generator (not called)", "[task]") {
  auto fib_seq = get_seq(fibonacci(), 0);
  REQUIRE(fib_seq.done());
  REQUIRE(fib_seq.await_resume() == "");
}

TEST_CASE("Fibonacci generator (called once)", "[task]") {
  auto fib_seq = get_seq(fibonacci(), 1);
  REQUIRE(fib_seq.done());
  REQUIRE(fib_seq.await_resume() == "1");
}

TEST_CASE("Fibonacci generator", "[task]") {
  auto fib_seq = get_seq(fibonacci(), 7);
  REQUIRE(fib_seq.done());
  REQUIRE(fib_seq.await_resume() == "1 1 2 3 5 8 13");
}

TEST_CASE("Fibonacci without co_await", "[task]") {
  auto fib = fibonacci();
  const int seq[] = {1, 1, 2, 3, 5};

  for (int i = 0; i < sizeof(seq)/sizeof(seq[0]); ++i) {
    fib.resume();
    REQUIRE(!fib.done());
    REQUIRE(fib.await_resume() == seq[i]);
  }
}

TEST_CASE("Async generator (not called)", "[task]") {
  auto fib_seq = get_seq(async_fib(), 0);
  REQUIRE(fib_seq.done());
  REQUIRE(fib_seq.await_resume() == "");
}

TEST_CASE("Async generator (called once)", "[task]") {
  auto fib_seq = get_seq(async_fib(), 1);
  REQUIRE(fib_seq.done());
  REQUIRE(fib_seq.await_resume() == "1");
}

TEST_CASE("Async generator", "[task]") {
  auto fib_seq = get_seq(async_fib(), 7);
  REQUIRE(fib_seq.done());
  REQUIRE(fib_seq.await_resume() == "1 1 2 3 5 8 13");
}

template<typename TaskB>
TaskB suspend_b(std::stringstream &s) {
  s << ", [B0]";
  co_await std::suspend_always{};
  s << "B1";
}

template<typename TaskA, typename TaskB>
TaskA suspend_a(std::stringstream &s, TaskB **task_b) {
  s << "A0";
  auto b = suspend_b<TaskB>(s);
  *task_b = &b;
  s << ", A1]";
  co_await b;
  s << ", [A2";
}

template<typename TaskA, typename TaskB>
void test_suspend_step(TaskA &ta, int sa, TaskB **tb, int sb,
                       std::stringstream &s) {
  const auto loop = [&s](auto &t, int st, int su, auto a_done) {
    bool a_once = false;
    for (int i = 0; i < st; ++i) {
      REQUIRE(!t.done());
      t.resume();
      if (a_done() && !a_once) {
        a_once = true;
        s << ", AF]";
      }
    }
  };

  loop(ta, sa, sb, [] { return false; });
  if (tb && *tb) {
    s << ", B: [";
    loop(**tb, sb, 0, [&] { return ta.done(); });
    REQUIRE((**tb).done());
    s << ", BF]";
  } else if (sb > 0) {
    FAIL("expected task tb with " << sb << " suspensions");
  }
  REQUIRE(ta.done());
}

template<typename TaskA, typename TaskB>
void test_suspend(int steps_a, int steps_b, const char *expected) {
  std::stringstream s;
  TaskB *b = nullptr;
  s << "A: [";
  auto a = suspend_a<TaskA, TaskB>(s, &b);
  test_suspend_step(a, steps_a, &b, steps_b, s);
  REQUIRE(s.str() == expected);
}

TEST_CASE("Suspension", "[task]") {
  test_suspend<
    AutoTask<>, AutoTask<>>(0, 1,
    "A: [A0, [B0], A1], B: [B1, [A2, AF], BF]");
  test_suspend<
    Task<>, AutoTask<>>(1, 1,
    "A: [A0, [B0], A1], B: [B1, [A2, AF], BF]");

  test_suspend<
    AutoTask<>, Task<>>(0, 1,
    "A: [A0, A1], [B0], B: [B1, [A2, AF], BF]");
  test_suspend<
    Task<>, Task<>>(1, 1,
    "A: [A0, A1], [B0], B: [B1, [A2, AF], BF]");
}
