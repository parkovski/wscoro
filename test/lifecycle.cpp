#include "lifecycle-results.h"

#define WSCORO_LOG g_logger
#include "wscoro/task.h"

#include <catch2/catch_all.hpp>

#include <string>
#include <string_view>
#include <sstream>
#include <memory>
#include <atomic>

using namespace wscoro;

//
// Test infrastructure {{{
//

template<typename T, typename P = void>
concept AwaitSuspend =
  std::is_invocable_v<decltype(&T::await_suspend), T &,
                      std::coroutine_handle<P>> ||
  std::is_invocable_v<decltype(&T::await_suspend), T &,
                      std::coroutine_handle<>> ||
  std::is_invocable_v<decltype(&T::await_suspend), T &>;

template<typename T, typename P = void>
concept AwaitSuspendNoexcept =
  std::is_nothrow_invocable_v<decltype(&T::await_suspend), T &,
                              std::coroutine_handle<P>> ||
  std::is_nothrow_invocable_v<decltype(&T::await_suspend), T &,
                              std::coroutine_handle<>> ||
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

  TraceLogger() noexcept {
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

  explicit TraceLogger(std::string subname) noexcept : TraceLogger() {
    this->subname = std::move(subname);
  }

  TraceLogger(TraceLogger &&o) noexcept
    : state{}, name{std::move(o.name)}, subname{std::move(o.subname)}
  {
    s_counter.fetch_add(1, std::memory_order_acq_rel);
    state = std::move(o.state);
  }

  TraceLogger(const TraceLogger &o)
    : state{}, name{o.name}, subname{o.subname}
  {
    s_counter.fetch_add(1, std::memory_order_acq_rel);
    state = o.state;
  }

  TraceLogger &operator=(TraceLogger &&o) = default;
  TraceLogger &operator=(const TraceLogger &) = default;

  ~TraceLogger() {
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

  template<typename ...Args>
  void operator()(const char *format, Args &&...args) const noexcept {
    std::lock_guard lock(state->mutex);
    state->ss << name << " ";
    if (subname.length()) {
      state->ss << subname << " ";
    }
    state->ss << fmt::format(format, std::forward<Args>(args)...) << "\n";
  }

  // clears the internal buffer, returning what was in it.
  std::string get() {
    std::lock_guard lock(state->mutex);
    auto s = state->ss.str();
    state->ss.str("");
    return s;
  }
};

std::weak_ptr<TraceLogger::State> TraceLogger::s_state;
std::atomic<int> TraceLogger::s_counter;
std::atomic<int> TraceLogger::s_destroy_counter;

template<typename T>
struct Trace;

template<typename T>
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
    logger("destroy");
  }

  bool await_ready() noexcept(noexcept(this->T::await_ready())) {
    logger("await_ready");
    return this->T::await_ready();
  }

  template<typename P>
  decltype(auto)
  await_suspend(std::coroutine_handle<P> handle)
    noexcept(AwaitSuspendNoexcept<T, P> ||
             AwaitSuspendNoexcept<T, typename P::base>)
  {
    static_assert(AwaitSuspend<T, P> ||
                  AwaitSuspend<T, typename P::base>);
    logger("await_suspend");

    if constexpr (
      std::is_invocable_v<decltype(&T::await_suspend), T &,
                          std::coroutine_handle<P>>) {
      return this->T::await_suspend(handle);
    }
    else if constexpr (
      std::is_invocable_v<decltype(&T::await_suspend), T &,
                          std::coroutine_handle<typename P::base>>) {
      return this->T::await_suspend(handle_cast<typename P::base>(handle));
    }
    else {
      return this->T::await_suspend();
    }
  }

  decltype(auto) await_resume() noexcept(noexcept(this->T::await_resume())) {
    logger("await_resume");
    return this->T::await_resume();
  }
};

template<typename T>
TraceAwait(std::string, T &&) -> TraceAwait<T>;

template<CoroutineTraits T>
struct Trace<T> : public T {
  using initial_suspend_type = TraceAwait<std::suspend_never>;
};

template<typename T, CoroutineTraits Traits>
struct Trace<BasicTask<T, Traits>> : public BasicTask<T, Trace<Traits>> {
  TraceLogger logger;

  using base = BasicTask<T, Trace<Traits>>;
  using base_promise_type = typename base::promise_type;

  struct promise_type;

private:
  static std::coroutine_handle<base_promise_type>
  to_base(std::coroutine_handle<promise_type> ch) noexcept {
    return std::coroutine_handle<base_promise_type>::from_promise(
      ch.promise());
  }

  template<typename U, typename Tr>
  constexpr static inline bool returns_void =
    std::disjunction<typename Tr::is_generator, std::is_void<U>>::value;

public:
  explicit Trace(std::coroutine_handle<promise_type> coroutine) noexcept
    : BasicTask<T, Trace<Traits>>{to_base(coroutine)}, logger{"task"}
  {
    logger("init promise={}", coroutine.promise().logger.name);
  }

  Trace(const Trace &) = delete;
  Trace &operator=(const Trace &) = delete;
  Trace(Trace &&) = default;
  Trace &operator=(Trace &&) = default;

  ~Trace() {
    logger("destroy");
  }

  bool await_ready() const
    noexcept(
      std::is_nothrow_invocable_v<decltype(&base::await_ready), base &>
    )
  {
    logger("await_ready");
    return this->base::await_ready();
  }

  std::coroutine_handle<>
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

  typename base::value_type
  await_resume()
    noexcept(
      std::is_nothrow_invocable_v<decltype(&base::await_resume), base &>
    )
  {
    logger("await_resume");
    return this->base::await_resume();
  }

  struct promise_type : base_promise_type {
    TraceLogger logger;

    using base = base_promise_type;

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
      return TraceAwait<typename Traits::initial_suspend_type>{
        "initial_suspend"
      };
    }

    auto final_suspend() const noexcept {
      return TraceAwait<decltype(base::final_suspend())>{"final_suspend"};
    }

    template<typename U = T>
    decltype(auto)
    yield_value(std::enable_if_t<!std::is_void_v<U>, T> &&value)
      noexcept(noexcept(
        static_cast<base *>(this)->yield_value(std::move(value))))
    {
      logger("yield.move");
      return TraceAwait{"yield", this->base::yield_value(std::move(value))};
    }

    template<typename U = T>
    decltype(auto)
    yield_value(const std::enable_if_t<!std::is_void_v<U>, T> &value)
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

template<int N = 1, int Resume = N, typename TaskT = void>
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

    while (true) {
      if (!task.await_ready()) {
        if (auto c = task.await_suspend(nullptr)) {
          c.resume();
        }

        for (int resume = 0; resume < Resume; ++resume) {
          CHECK(!task.done());
          scope.logger("resume count={}", resume);
          task.resume();
        }
      }

      if constexpr (TaskT::is_generator::value) {
        // FIXME - generator optional
        // if (auto r = task.await_resume(); r.has_value()) {
        //   if (result.length()) result += " ";
        //   result += std::to_string(r.value());
        // }
        if (task.done()) {
          scope.logger("generator done");
          break;
        }
        if (result.length()) {
          result += " ";
        }
        result += std::to_string(task.await_resume());
        scope.logger("generator yield");
      } else {
        CHECK(task.done());
        result = std::to_string(task.await_resume());
        break;
      }
    }
  }
  scope.logger("result={}", result);
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
    CHECK(task.done());
    co_yield counter;
  }
  co_return;
}

Trace<FireAndForget> lifecycle_f(int n) {
  co_await ([]() -> Lazy<> { co_return; })();
  co_return;
}

TEST_CASE("Lifecycle", "[lifecycle]") {
  CHECK(test_lifecycle_a<1, 0>(&lifecycle_s<Immediate<int>>) ==
          lcresult<Immediate<>>);

  CHECK(test_lifecycle_a<2, 0>(&lifecycle_s<Lazy<int>>) ==
          lcresult<Lazy<>>);

  CHECK(test_lifecycle_a<3>(&lifecycle_a<Task<int>>) ==
        lcresult<Task<>>);

  CHECK(test_lifecycle_a<3, 2>(&lifecycle_a<AutoTask<int>>) ==
        lcresult<AutoTask<>>);

  CHECK(test_lifecycle_a<3, 0>(&lifecycle_y<Generator<int>>) ==
        lcresult<Generator<int>>);

  CHECK(test_lifecycle_a<3, 0>(&lifecycle_ay<AsyncGenerator<int>>) ==
        lcresult<AsyncGenerator<int>>);

  // REQUIRE(test_lifecycle_a(&lifecycle_f<FireAndForget<>>) ==
  //         lcresult<FireAndForget<>>);
}

//
// }}} Test implementation
