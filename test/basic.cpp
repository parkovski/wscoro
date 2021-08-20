#include "wscoro/wscoro.h"

#include <catch2/catch_all.hpp>

using namespace wscoro;

template<class Ts>
Ts add_one(int x) {
  co_return x + 1;
}

// BasicTask
static_assert(std::is_move_constructible_v<Task<>>,
              "Task should be movable");
static_assert(!std::is_copy_constructible_v<Task<>>,
              "Task should not be copiable");
// BasicGenerator
static_assert(std::is_move_constructible_v<Generator<int>>,
              "Generator should be movable");
static_assert(!std::is_copy_constructible_v<Generator<int>>,
              "Generator should not be copiable");
// BasicCoroutine
static_assert(std::is_move_constructible_v<FireAndForget>,
              "FireAndForget should be movable");
static_assert(!std::is_copy_constructible_v<FireAndForget>,
              "FireAndForget should not be copiable");

template<class C>
C get_coroutine() {
  co_return;
}

TEMPLATE_TEST_CASE("Move coroutine", "[basic]", (Task<>), (Generator<int>),
                   FireAndForget) {
  auto co1 = get_coroutine<TestType>();
  auto co2 = std::move(co1);
  REQUIRE( !co1);
  REQUIRE(!!co2);
}

TEMPLATE_TEST_CASE("co_return", "[basic][task]",
                   (Immediate<int>), (Lazy<int>), (Task<int>),
                   (ImmediateTask<int>)) {
  auto t = add_one<TestType>(1);
  if constexpr (std::is_base_of_v<wscoro::suspend::BasicInitialSuspend<true>,
                                  typename TestType::promise_type>) {
    REQUIRE(!t.await_ready());
    t.await_suspend(std::noop_coroutine()).resume();
  }
  REQUIRE(t.await_ready());
  REQUIRE(t.await_resume() == 2);
}

template<class G>
G inc_twice(int x) {
  co_yield x + 1;
  co_yield x + 2;
}

TEMPLATE_TEST_CASE("co_yield", "[basic][generator]",
                   (Generator<int>), (AsyncGenerator<int>)) {
  auto t = inc_twice<TestType>(1);
  REQUIRE(!t.await_ready());
  t.resume();
  REQUIRE(t.await_ready());
  REQUIRE(*t.await_resume() == 2);
  t.resume();
  REQUIRE(t.await_ready());
  REQUIRE(*t.await_resume() == 3);
  t.resume();
  REQUIRE(t.await_ready());
  REQUIRE(t.await_resume() == std::nullopt);
}

FireAndForget inc_ref(int &x) {
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

TEST_CASE("Basic ImmediateTask suspension", "[basic][task]") {
  int counter = 0;
  auto get_one = ::get_one<ImmediateTask>(counter);
  REQUIRE(counter == 1);

  get_one.resume();
  REQUIRE(get_one.done());
  REQUIRE(counter == 3);

  REQUIRE(get_one.await_resume() == 1);
}
