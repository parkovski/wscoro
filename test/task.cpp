#define WSCORO_LOG g_logger
#include "wscoro/task.h"

#include <catch2/catch_all.hpp>

#include <string>
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

TEST_CASE("Async generator", "[task]") {
  auto fib_seq = get_seq(async_fib(), 7);
  REQUIRE(fib_seq.done());
  REQUIRE(fib_seq.await_resume() == "1 1 2 3 5 8 13");
}
