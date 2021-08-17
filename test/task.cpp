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
  auto final_inc = scope_exit([&]() noexcept { ++counter; });
  ++counter;
  co_await std::suspend_always{};
  ++counter;
  co_return 1;
}

TEST_CASE("Basic DelayTask suspension", "[task]") {
  int counter = 0;
  auto get_one = ::get_one<DelayTask>(counter);
  REQUIRE(counter == 0);

  get_one.resume();
  REQUIRE(!get_one.done());
  REQUIRE(counter == 1);

  get_one.resume();
  REQUIRE(get_one.done());
  REQUIRE(counter == 3);

  REQUIRE(get_one.await_resume() == 1);
}

TEST_CASE("Basic Task suspension", "[task]") {
  int counter = 0;
  auto get_one = ::get_one<Task>(counter);
  REQUIRE(counter == 1);

  get_one.resume();
  REQUIRE(get_one.done());
  REQUIRE(counter == 3);

  REQUIRE(get_one.await_resume() == 1);
}

Lazy<void> increment(int &x) {
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
