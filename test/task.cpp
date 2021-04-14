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

struct TraceLogger {
  std::string name;
  mutable std::stringstream ss;

  TraceLogger() noexcept {}
  TraceLogger(TraceLogger &&) = default;
  TraceLogger &operator=(TraceLogger &&) = default;

  template<typename ...Args>
  void operator()(const char *format, Args &&...args) const noexcept {
    ss << name << ": ";
    ss << fmt::format(format, std::forward<Args>(args)...);
    ss << "\n";
  }
};
using LogRef = std::reference_wrapper<const TraceLogger>;

template<typename T>
struct Trace;

template<typename T>
struct TraceAwait : private T {
  LogRef logger;
  std::string kind;

  TraceAwait(const TraceLogger &logger, std::string kind) noexcept
    : logger{logger}, kind{std::move(kind)}
  {}

  bool await_ready() noexcept(noexcept(this->T::await_ready())) {
    logger("{} await_ready", kind);
    return this->T::await_ready();
  }

  template<typename P>
  decltype(auto)
  await_suspend(std::coroutine_handle<P> h)
      noexcept(
        std::is_nothrow_invocable_v<
          decltype(&T::await_suspend), T &,
          std::coroutine_handle<typename P::base>
        > ||
        std::is_nothrow_invocable_v<
          decltype(&T::await_suspend), T &,
          std::coroutine_handle<>
        > ||
        std::is_nothrow_invocable_v<
          decltype(&T::await_suspend), T &
        >
      )
  {
    logger("{} await_suspend", kind);

    using await_suspend_t = decltype(&T::await_suspend);

    if constexpr (std::is_invocable_v<
        await_suspend_t, T &, std::coroutine_handle<typename P::base>
    >) {
      return this->T::await_suspend(
        std::coroutine_handle<typename P::base>::from_promise(h.promise())
      );
    } else if constexpr (std::is_invocable_v<
        await_suspend_t, T &, std::coroutine_handle<>>
    ) {
      return this->T::await_suspend(h);
    } else if constexpr (std::is_invocable_v<await_suspend_t, T &>) {
      return this->T::await_suspend();
    } else {
      throw std::runtime_error("T::await_suspend not valid");
    }
  }

  decltype(auto) await_resume() noexcept(noexcept(this->T::await_resume())) {
    logger("{} await_resume", kind);
    return this->T::await_resume();
  }
};

template<CoroutineTraits T>
struct Trace<T> : public T {
  using initial_suspend_type = TraceAwait<std::suspend_never>;
#if 0
  using is_generator = typename T::is_generator;
  using is_async = typename T::is_async;
  using is_awaiter = typename T::is_awaiter;
  using exception_behavior = typename T::exception_behavior;
  using move_result = typename T::move_result;
#endif
};

static int next_task_id = 0;
template<typename T, CoroutineTraits Traits>
struct Trace<BasicTask<T, Traits>> : public BasicTask<T, Trace<Traits>> {
  LogRef logger;

  using base = BasicTask<T, Trace<Traits>>;
  using base_promise_type = typename base::promise_type;

  struct promise_type;

private:
  static std::coroutine_handle<base_promise_type>
  to_base(std::coroutine_handle<promise_type> ch) noexcept {
    return std::coroutine_handle<base_promise_type>::from_promise(
      ch.promise());
  }

public:
  explicit Trace(std::coroutine_handle<promise_type> coroutine,
                 const TraceLogger &logger) noexcept
    : BasicTask<T, Trace<Traits>>{to_base(coroutine)}, logger{logger}
  {
    logger("task init");
  }

  Trace(const Trace &) = delete;
  Trace &operator=(const Trace &) = delete;
  Trace(Trace &&) = default;
  Trace &operator=(Trace &&) = default;

  ~Trace() {
    logger("task destroy");
  }

  struct promise_type : base_promise_type {
    int id;
    TraceLogger logger;

    using base = base_promise_type;

    promise_type() noexcept
      : id{next_task_id++}
    {
      logger.name = "#" + std::to_string(id);
      logger("promise init");
    }

    ~promise_type() {
      logger("promise destroy");
    }

    Trace get_return_object()
      noexcept(std::is_nothrow_constructible_v<
        Trace, std::coroutine_handle<promise_type>>)
    {
      Trace task(
        std::coroutine_handle<promise_type>::from_promise(*this),
        logger);
      return task;
    }

    auto initial_suspend() const noexcept {
      return TraceAwait<typename Traits::initial_suspend_type>{
        logger, "initial"
      };
    }

    auto final_suspend() const noexcept {
      return TraceAwait<decltype(base::final_suspend())>{
        logger, "final"
      };
    }
  };
};

template<typename TaskT>
Trace<TaskT> lifecycle_a() {
  co_await std::suspend_always{};
}

template<typename TaskT>
std::string test_lifecycle_a() {
  auto task = lifecycle_a<TaskT>();
  int resume = 0;
  while (!task.done()) {
    task.logger("resume {}", resume);
    task.resume();
  }
  task.logger("done");
  return task.logger.get().ss.str();
}

template<typename Gen>
Trace<Gen> gen(int n) {
  for (int i = 0; i < n; ++i) {
    co_yield i + 1;
  }
}

template<typename Gen>
Trace<Task<>> lifecycle_g(Gen &g) {
  while (!g.done()) {
    co_await g;
  }
}

template<typename Gen>
std::string test_lifecycle_g() {
  auto g = gen<Gen>(3);
  auto lc = lifecycle_g(g);
  lc.resume();
  return g.logger.get().ss.str();
}

template<typename T>
Trace<T> lifecycle_s() {
  co_return;
}

template<typename T>
std::string test_lifecycle_s() {
  auto task = lifecycle_s<T>();
  if (!task.done()) {
    task.resume();
  }
  return task.logger.get().ss.str();
}

TEST_CASE("Lifecycle", "[task]") {
  // test_lifecycle_s<Immediate<>>();
  // test_lifecycle_s<Lazy<>>();
  FAIL_CHECK(test_lifecycle_a<Task<>>());
  // test_lifecycle_a<AutoTask<>>();
  // test_lifecycle_g<Generator<int>>();
  // test_lifecycle_g<AsyncGenerator<int>>();
  //test_lifecycle<FireAndForget>();
}
