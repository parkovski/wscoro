#include "lifecycle-results.h"

#include "wscoro/wscoro.h"

#include <catch2/catch_all.hpp>

#include <string>
#include <string_view>
#include <sstream>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <iostream>

using namespace wscoro;

//
// Test infrastructure {{{
//

template<typename T, typename P = void>
concept AwaitSuspend =
  std::is_invocable_v<decltype(&T::await_suspend), T &,
                      std::coroutine_handle<P>> ||
  std::is_invocable_v<decltype(&T::await_suspend), T &>;

template<typename T, typename P = void>
concept AwaitSuspendNoexcept =
  std::is_nothrow_invocable_v<decltype(&T::await_suspend), T &,
                              std::coroutine_handle<P>> ||
  std::is_nothrow_invocable_v<decltype(&T::await_suspend), T &>;

// Creates a function object that casts a coroutine_handle<Q> to
// coroutine_handle<P>.
template<typename P>
struct handle_cast_t {
  // Identity function - target type is same as source type.
  constexpr
  std::coroutine_handle<P>
  operator()(std::coroutine_handle<P> handle) const noexcept {
    return handle;
  }

  // If P is a base class of some Q, we can create a coroutine_handle<P>
  // from a coroutine_handle<Q>.
  template<typename Q>
  std::enable_if_t<std::is_base_of_v<P, Q>, std::coroutine_handle<P>>
  operator()(std::coroutine_handle<Q> handle) const noexcept {
    return std::coroutine_handle<P>::from_promise(handle.promise());
  }
};

// Type erasure - any typed handle can become coroutine_handle<void>.
template<>
struct handle_cast_t<void> {
  // Returns the implicitly casted handle (operator coroutine_handle<>()).
  template<typename Q>
  std::coroutine_handle<>
  operator()(std::coroutine_handle<Q> handle) const noexcept {
    return handle;
  }
};

template<typename P>
static inline constexpr handle_cast_t<P> handle_cast = handle_cast_t<P>{};

struct TraceLogger {
private:
  struct State {
    std::stringstream ss;
    std::mutex mutex;
  };
  static std::weak_ptr<State> s_state;
  static std::atomic<int> s_counter;
  static std::atomic<int> s_destroy_counter;

  std::shared_ptr<State> state;

public:
  std::string name;
  std::string subname;

  static int get_counter() noexcept {
    return s_counter.load(std::memory_order_acquire);
  }

  static int get_destroy_counter() noexcept {
    return s_destroy_counter.load(std::memory_order_acquire);
  }

  static int get_state_counter() noexcept {
    return static_cast<int>(s_state.use_count());
  }

  explicit TraceLogger(std::string subname) noexcept
    : subname{std::move(subname)}
  {
    int my_id = -1;
    if ((state = s_state.lock())) {
      my_id = s_counter.fetch_add(1, std::memory_order_acq_rel);
    } else {
      // be very careful about the ordering of these statements.
      state = std::make_shared<State>();
      s_state = std::weak_ptr{state};
      std::atomic_thread_fence(std::memory_order_acq_rel);
      my_id = s_counter.fetch_add(1, std::memory_order_relaxed);
      if (auto state2 = s_state.lock(); state2 != state) {
        state = state2;
      }
    }
    name = std::to_string(my_id);
  }

  TraceLogger() noexcept : TraceLogger(std::string{}) {}

  TraceLogger(TraceLogger &&o) = default;

  TraceLogger(const TraceLogger &o) noexcept
    : state{o.state}, name{o.name}, subname{o.subname}
  {
    s_counter.fetch_add(1, std::memory_order_acq_rel);
  }

  TraceLogger &operator=(TraceLogger &&o) noexcept {
    state = std::move(o.state);
    name = std::move(o.name);
    subname = std::move(o.subname);
    s_counter.fetch_sub(1, std::memory_order_acq_rel);
    return *this;
  }

  TraceLogger &operator=(const TraceLogger &) = default;

  ~TraceLogger() {
    if (!state) {
      // got moved out
      return;
    }
    // use seq_cst here b/c the dependency on 2 different atomic variables.
    int destroy_counter = ++s_destroy_counter;
    int counter = s_counter.load();
    if (counter == destroy_counter) {
      assert(state.use_count() <= 1 && "concurrency");
      // will free as we're the only owner, and s_state becomes invalid.
      state.reset();
      std::atomic_thread_fence(std::memory_order_acq_rel);
      s_counter.fetch_sub(counter, std::memory_order_relaxed);
      s_destroy_counter.fetch_sub(counter, std::memory_order_relaxed);
    }
  }

  operator bool() const noexcept {
    return (bool)state;
  }

  template<typename ...Args>
  void operator()(Args &&...args) const noexcept {
    assert(state);
    std::lock_guard lock(state->mutex);
    state->ss << name << " ";
    if (subname.length()) {
      state->ss << subname << " ";
    }
    (state->ss << ... << args) << "\n";
  }

  // clears the internal buffer, returning what was in it.
  std::string get() {
    std::lock_guard lock(state->mutex);
    auto s = state->ss.str();
    state->ss.str("");
    return s;
  }

  std::string get_noclear() {
    std::lock_guard lock(state->mutex);
    return state->ss.str();
  }
};

std::weak_ptr<TraceLogger::State> TraceLogger::s_state;
std::atomic<int> TraceLogger::s_counter;
std::atomic<int> TraceLogger::s_destroy_counter;

template<class T>
struct Trace;

template<class T>
struct TraceAwait : private T {
  TraceLogger logger;

  TraceAwait(std::string kind) noexcept
    : logger{std::move(kind)}
  {
    logger("init");
  }

  TraceAwait(std::string kind, T &&o) noexcept
    : T{std::move(o)}, logger{std::move(kind)}
  {
    logger("init.move");
  }

  ~TraceAwait() {
    if (logger) logger("destroy");
  }

  bool await_ready() const
    noexcept(std::is_nothrow_invocable_v<decltype(&T::await_ready), T &>)
  {
    logger("await_ready");
    return this->T::await_ready();
  }

  template<
    class AS = decltype(&T::await_suspend),
    class = std::enable_if_t<std::is_invocable_v<AS, T &>>
  >
  decltype(auto) await_suspend() noexcept(AwaitSuspendNoexcept<T>) {
    return this->T::await_suspend();
  }

  template<
    class AS = decltype(&T::await_suspend),
    class = std::enable_if_t<
      std::is_invocable_v<AS, T &, std::coroutine_handle<>>
    >
  >
  decltype(auto) await_suspend(std::coroutine_handle<> handle)
    noexcept(AwaitSuspendNoexcept<T>)
  {
    logger("await_suspend");
    return this->T::await_suspend(handle);
  }

  decltype(auto) await_resume()
    noexcept(std::is_nothrow_invocable_v<decltype(&T::await_resume), T &>)
  {
    logger("await_resume");
    return this->T::await_resume();
  }
};

template<class T>
TraceAwait(std::string, T &&) -> TraceAwait<T>;

template<class TaskT>
struct Trace {
  TaskT task;
  TraceLogger logger;

  using task_type = TaskT;
  using task_promise_type = typename task_type::promise_type;

  struct promise_type;

private:
  static std::coroutine_handle<task_promise_type>
  to_base(std::coroutine_handle<promise_type> ch) noexcept {
    return std::coroutine_handle<task_promise_type>::from_promise(
      ch.promise());
  }

public:
  explicit Trace(std::coroutine_handle<promise_type> coroutine) noexcept
    : task{to_base(coroutine)}, logger{"task"}
  {
    logger("init promise=", coroutine.promise().logger.name);
  }

  Trace(Trace &&) = default;
  Trace &operator=(Trace &&) = delete;

  Trace(const Trace &) = delete;
  Trace &operator=(const Trace &) = delete;

  ~Trace() {
    if (logger) logger("destroy");
  }

  template<class = decltype(task.operator co_await())>
  auto operator co_await() {
    return TraceAwait{"awaiter", task.operator co_await()};
  }

#if 0
  template<typename B = base, typename = decltype(&B::await_ready)>
  bool await_ready() const
    noexcept(
      std::is_nothrow_invocable_v<decltype(&base::await_ready), base &>
    )
  {
    logger("await_ready");
    return this->base::await_ready();
  }

  template<typename B = base, typename = decltype(&B::await_suspend)>
  decltype(auto)
  await_suspend(std::coroutine_handle<> continuation)
    noexcept(AwaitSuspendNoexcept<base>)
  {
    logger("await_suspend");
    if constexpr (
      std::is_invocable_v<decltype(&base::await_suspend), base &,
                          std::coroutine_handle<>>) {
      return this->base::await_suspend(continuation);
    } else {
      return this->base::await_suspend();
    }
  }

  template<typename B = base, typename = decltype(&B::await_resume)>
  decltype(auto)
  await_resume() noexcept(
    std::is_nothrow_invocable_v<decltype(&base::await_resume), base &>
  )
  {
    logger("await_resume");
    return this->base::await_resume();
  }
#endif

  struct promise_type : task_promise_type {
    TraceLogger logger;

    using base = task_promise_type;

    promise_type() noexcept
      : logger{"promise"}
    {
      logger("init");
    }

    ~promise_type() {
      logger("destroy");
    }

    Trace get_return_object()
      noexcept(std::is_nothrow_constructible_v<
        Trace, std::coroutine_handle<promise_type>>)
    {
      Trace task(std::coroutine_handle<promise_type>::from_promise(*this));
      return task;
    }

    auto initial_suspend() const noexcept {
      return TraceAwait<decltype(base::initial_suspend())>{"initial_suspend"};
    }

    auto final_suspend() const noexcept {
      return TraceAwait<decltype(base::final_suspend())>{"final_suspend"};
    }

    template<typename U = typename task_type::value_type,
             typename = std::enable_if_t<!std::is_void_v<U>>>
    decltype(auto)
    yield_value(U &&value)
      noexcept(noexcept(
        static_cast<base *>(this)->yield_value(std::move(value))))
    {
      logger("yield.move");
      return TraceAwait{"yield", this->base::yield_value(std::move(value))};
    }

    template<typename U = typename task_type::value_type,
             typename = std::enable_if_t<!std::is_void_v<U>>>
    decltype(auto)
    yield_value(U const &value)
      noexcept(noexcept(static_cast<base *>(this)->yield_value(value)))
    {
      logger("yield.copy");
      return TraceAwait{"yield", this->base::yield_value(value)};
    }
  };
};

//
// }}} Test infrastructure
//
// ---------------------------------------------------------------------------
//
// Test implementation {{{
//

template<int N, int Resume, bool IsGenerator, class TaskT>
std::string test_lifecycle_a(Trace<TaskT> (*lifecycle)(int)) {
  union ManualScope {
    TraceLogger logger;
    ManualScope() noexcept {}
    ~ManualScope() {}
  };

  std::string result;
  ManualScope scope;
  new (&scope.logger) TraceLogger("executor");
  scope.logger("init");
  {
    auto task = lifecycle(N);
    auto awaiter = task.operator co_await();

    int resume = 0;
    while (true) {
      if (awaiter.await_ready()) {
        if constexpr (IsGenerator) {
          auto r = awaiter.await_resume();
          if (r.has_value()) {
            scope.logger("generator yield");
            if (result.length()) result += " ";
            result += std::to_string(*r);
          } else {
            CHECK(task.task.done());
            break;
          }
        } else {
          CHECK(task.task.done());
          result = std::to_string(awaiter.await_resume());
          break;
        }
      } else {
        auto c = awaiter.await_suspend(std::noop_coroutine());
        if (c != std::noop_coroutine()) {
          //resume = 0;
          //while (!c.done()) {
            scope.logger("resume count=", resume++);
            c.resume();
          //}
          /*for (int resume = 0; resume < Resume; ++resume) {
            CHECK(!task.task.done());
            scope.logger("resume count=", resume);
            c.resume();
          }*/
        } else if (task.task.done()) {
          break;
        } else {
          task.task.resume();
          if constexpr (IsGenerator) {
            auto r = awaiter.await_resume();
            if (!r.has_value()) {
              break;
            }
            scope.logger("generator yield");
            if (result.length()) result += " ";
            result += std::to_string(*r);
          } else {
            result = awaiter.await_resume();
            break;
          }
        }
      }
    }
  }
  scope.logger("result=", result);
  scope.logger("destroy");
  std::string s = scope.logger.get();
  scope.logger.~TraceLogger();
  REQUIRE(TraceLogger::get_state_counter() == 0);
  return s;
}

template<typename TaskT>
Trace<TaskT> lifecycle_s(int n) {
  co_return n;
}

template<typename TaskT>
Trace<TaskT> lifecycle_a(int n) {
  while (n--) {
    co_await TraceAwait<std::suspend_always>{"suspend_always"};
  }
  co_return n; // -1
}

template<typename TaskG>
Trace<TaskG> lifecycle_y(int n) {
  for (int i = 0; i < n; ++i) {
    co_yield i + 1;
  }
}

template<typename TaskG, typename TaskA = Task<int>>
Trace<TaskG> lifecycle_ay(int n) {
  int counter = 0;
  for (int i = 0; i < n; ++i) {
    auto task = lifecycle_a<TaskA>(0);
    counter += co_await task;
    CHECK(task.task.done());
    co_yield counter;
  }
  co_return;
}

Trace<FireAndForget> lifecycle_f(std::string &result) {
  result += "A";
  co_await lifecycle_a<Task<int>>(0);
  result += " B";
  using namespace std::chrono_literals;
  std::this_thread::sleep_for(5ms);
  result += " C";
  co_return;
}

std::string test_lifecycle_f() {
  union ManualScope {
    struct ScopeImpl {
      TraceLogger logger;
      Trace<FireAndForget> task;
    } impl;
    ManualScope() noexcept {}
    ~ManualScope() {}
  };

  std::string result;
  ManualScope scope;
  new (&scope.impl.logger) TraceLogger("executor");
  scope.impl.logger("init");
  {
    new (&scope.impl.task) Trace<FireAndForget>{lifecycle_f(result)};
    auto coro = scope.impl.task.task.handle();
    scope.impl.task.~Trace<FireAndForget>();
    coro.resume();
  }
  scope.impl.logger("result=", result);
  scope.impl.logger("destroy");
  std::string s = scope.impl.logger.get();
  scope.impl.logger.~TraceLogger();
  REQUIRE(TraceLogger::get_state_counter() == 0);
  return s;
}

TEST_CASE("Lifecycle Immediate", "[lifecycle]") {
  REQUIRE(test_lifecycle_a<1, 0, false>(&lifecycle_s<Immediate<int>>) ==
          lcresult<Immediate<>>);
}

TEST_CASE("Lifecycle Lazy", "[lifecycle]") {
  REQUIRE(test_lifecycle_a<2, 0, false>(&lifecycle_s<Lazy<int>>) ==
          lcresult<Lazy<>>);
}

TEST_CASE("Lifecycle Task", "[lifecycle]") {
  REQUIRE(test_lifecycle_a<3, 3, false>(&lifecycle_a<Task<int>>) ==
          lcresult<Task<>>);
}

TEST_CASE("Lifecycle ImmediateTask", "[lifecycle]") {
  REQUIRE(test_lifecycle_a<3, 3, false>(&lifecycle_a<ImmediateTask<int>>) ==
          lcresult<ImmediateTask<>>);
}

TEST_CASE("Lifecycle Generator", "[lifecycle]") {
  REQUIRE(test_lifecycle_a<3, 0, true>(&lifecycle_y<Generator<int>>) ==
          lcresult<Generator<int>>);
}

TEST_CASE("Lifecycle AsyncGenerator", "[lifecycle]") {
  REQUIRE(test_lifecycle_a<3, 3, true>(&lifecycle_ay<AsyncGenerator<int>>) ==
          lcresult<AsyncGenerator<int>>);
}

TEST_CASE("Lifecycle FireAndForget", "[lifecycle]") {
  REQUIRE(test_lifecycle_f() == lcresult<FireAndForget>);
}

//
// }}} Test implementation
