#include "wscoro/task.h"

#include <catch2/catch_all.hpp>

#include <sstream>

using namespace wscoro;

template<typename TaskB>
TaskB suspend_b(std::stringstream &s) {
  s << ", [B0]";
  co_await std_::suspend_always{};
  s << "B1";
}

template<typename TaskA, typename TaskB>
TaskA suspend_a(std::stringstream &s, TaskB &b) {
  s << "A0";
  b = suspend_b<TaskB>(s);
  s << ", A1]";
  co_await b;
  s << ", [A2";
}

// dosn't get the continuation
template<typename TaskA, typename TaskB>
void test_suspend_step(TaskA &ta, int sa, TaskB &tb, int sb,
                       std::stringstream &s) {
  const auto loop = [&s](auto &t, int st, auto a_done) {
    bool a_once = false;
    for (int i = 0; i < st; ++i) {
      CHECK(!t.done());
      t.resume();
      if (a_done() && !a_once) {
        a_once = true;
        s << ", AF]";
      }
    }
  };

  loop(ta, sa, [] { return false; });
  if (tb) {
    s << ", B: [";
    loop(tb, sb, [&] { return ta.done(); });
    CHECK(tb.done());
    s << ", BF]";
  } else if (sb > 0) {
    FAIL_CHECK("expected task tb with " << sb << " suspensions");
  }
  CHECK(ta.done());
}

template<typename TaskA, typename TaskB>
void test_suspend(int steps_a, int steps_b, const char *expected) {
  std::stringstream s;
  // Extend the lifetime of this task so we can verify when it finishes.
  TaskB b{nullptr};
  s << "A: [";
  auto a = suspend_a<TaskA, TaskB>(s, b);
  test_suspend_step(a, steps_a, b, steps_b, s);
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
