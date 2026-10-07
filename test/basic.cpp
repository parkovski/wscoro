#include "wscoro/wscoro.h"

#include <catch2/catch_all.hpp>

using namespace wscoro;

template<class Ts>
Ts add_one(int x) {
  co_return x + 1;
}

// BasicTask
static_assert(std::is_move_constructible_v<task<>>,
              "task should be movable");
static_assert(!std::is_copy_constructible_v<task<>>,
              "task should not be copiable");
// BasicGenerator
static_assert(std::is_move_constructible_v<generator<int>>,
              "generator should be movable");
static_assert(!std::is_copy_constructible_v<generator<int>>,
              "generator should not be copiable");
// BasicCoroutine
static_assert(std::is_move_constructible_v<fire_and_forget>,
              "fire_and_forget should be movable");
static_assert(!std::is_copy_constructible_v<fire_and_forget>,
              "fire_and_forget should not be copiable");

template<class C>
C get_coroutine() {
  co_return;
}

TEMPLATE_TEST_CASE("Move coroutine", "[basic]", (task<>), (generator<int>),
                   fire_and_forget) {
  auto co1 = get_coroutine<TestType>();
  auto co2 = std::move(co1);
  REQUIRE( !co1);
  REQUIRE(!!co2);
}

TEMPLATE_TEST_CASE("co_return", "[basic][task]",
                   (immediate<int>), (lazy<int>), (task<int>),
                   (immediate_task<int>)) {
  auto t = add_one<TestType>(1);
  auto awaiter = t.operator co_await();
  if constexpr (std::is_base_of_v<wscoro::suspend::basic_initial_suspend<true>,
                                  typename TestType::promise_type>) {
    REQUIRE(!awaiter.await_ready());
    awaiter.await_suspend(std::noop_coroutine()).resume();
  }
  REQUIRE(awaiter.await_ready());
  REQUIRE(awaiter.await_resume() == 2);
}

template<class G>
G inc_twice(int x) {
  co_yield x + 1;
  co_yield x + 2;
}

TEMPLATE_TEST_CASE("co_yield", "[basic][generator]",
                   (generator<int>), (async_generator<int>)) {
  auto t = inc_twice<TestType>(1);
  auto awaiter = t.operator co_await();
  REQUIRE(!awaiter.await_ready());
  t.resume();
  REQUIRE(awaiter.await_ready());
  REQUIRE(*awaiter.await_resume() == 2);
  t.resume();
  REQUIRE(awaiter.await_ready());
  REQUIRE(*awaiter.await_resume() == 3);
  t.resume();
  REQUIRE(awaiter.await_ready());
  REQUIRE(awaiter.await_resume() == std::nullopt);
}

fire_and_forget inc_ref(int &x) {
  ++x;
  co_return;
}

TEST_CASE("forget", "[basic][forget]") {
  int x = 1;
  inc_ref(x);
  REQUIRE(x == 2);
}

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

template<template<typename> typename TTask>
static TTask<int> get_one(int &counter) {
  auto final_inc = scope_exit([&]() noexcept { ++counter; });
  ++counter;
  co_await std::suspend_always{};
  ++counter;
  co_return 1;
}

TEST_CASE("Basic Task suspension", "[basic][task]") {
  int counter = 0;
  auto get_one = ::get_one<task>(counter);
  REQUIRE(counter == 0);

  get_one.resume();
  REQUIRE(!get_one.done());
  REQUIRE(counter == 1);

  get_one.resume();
  REQUIRE(get_one.done());
  REQUIRE(counter == 3);

  REQUIRE(get_one.operator co_await().await_resume() == 1);
}

TEST_CASE("Basic ImmediateTask suspension", "[basic][task]") {
  int counter = 0;
  auto get_one = ::get_one<immediate_task>(counter);
  REQUIRE(counter == 1);

  get_one.resume();
  REQUIRE(get_one.done());
  REQUIRE(counter == 3);

  REQUIRE(get_one.operator co_await().await_resume() == 1);
}

task<std::coroutine_handle<>> get_this_coroutine() {
  co_return co_await this_coroutine;
}

TEST_CASE("Get current coroutine handle", "[basic][task]") {
  auto t = get_this_coroutine();
  t.resume();
}
