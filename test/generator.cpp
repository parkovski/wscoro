#include "wscoro/task.h"

#include <catch2/catch_all.hpp>

#include <sstream>

using namespace wscoro;

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
    co_yield *co_await fib;
  }
}

template<typename G>
static AutoTask<std::string> get_seq(G generator, int rounds) {
  std::stringstream ss;
  if (rounds > 0) {
    for (int i = 0; i < rounds - 1; i++) {
      ss << *std::move(co_await generator) << " ";
    }
    ss << *co_await generator;
  }
  co_return std::move(ss).str();
}

TEST_CASE("Fibonacci generator (not called)", "[generator]") {
  auto fib_seq = get_seq(fibonacci(), 0);
  CHECK(fib_seq.await_ready()); REQUIRE(fib_seq.await_resume() == "");
}

TEST_CASE("Fibonacci generator (called once)", "[generator]") {
  auto fib_seq = get_seq(fibonacci(), 1);
  CHECK(fib_seq.await_ready());
  REQUIRE(fib_seq.await_resume() == "1");
}

TEST_CASE("Fibonacci generator", "[generator]") {
  auto fib_seq = get_seq(fibonacci(), 7);
  CHECK(fib_seq.await_ready());
  REQUIRE(fib_seq.await_resume() == "1 1 2 3 5 8 13");
}

TEST_CASE("Fibonacci without co_await", "[generator]") {
  auto fib = fibonacci();
  const int seq[] = {1, 1, 2, 3, 5};

  for (int i = 0; i < sizeof(seq)/sizeof(seq[0]); ++i) {
    fib.resume();
    CHECK(!fib.done());
    REQUIRE(fib.await_resume() == seq[i]);
  }
}

TEST_CASE("Async generator (not called)", "[generator]") {
  auto fib_seq = get_seq(async_fib(), 0);
  CHECK(fib_seq.await_ready());
  REQUIRE(fib_seq.await_resume() == "");
}

TEST_CASE("Async generator (called once)", "[generator]") {
  auto fib_seq = get_seq(async_fib(), 1);
  CHECK(fib_seq.await_ready());
  REQUIRE(fib_seq.await_resume() == "1");
}

TEST_CASE("Async generator", "[generator]") {
  auto fib_seq = get_seq(async_fib(), 7);
  CHECK(fib_seq.await_ready());
  REQUIRE(fib_seq.await_resume() == "1 1 2 3 5 8 13");
}
