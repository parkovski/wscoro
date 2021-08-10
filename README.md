# wscoro - Bare bones C++20 coroutine library

**This is an early alpha and the current API should not be considered stable.**

**Currently supported compilers: MSVC, GCC.**

wscoro implements only the essentials needed to use C++20 coroutines. There are
no thread pools or executors; instead wscoro provides types for chaining
coroutines. C++20's coroutine support features are very low-level and wscoro
does not cover all possible cases, however it does support many common
scenarios.

## Usage

wscoro provides several different task type templates separated by feature
support:

| Type | Awaitable | Auto-start | `co_await` | `co_yield` | `co_return` |
| ---: | :-------: | :--------: | :--------: | :--------: | :---------: |
| Immediate |  ✅  |     ✅     |     ❌     |     ❌     |      ✅     |
| Lazy      |  ✅  |     ❌     |     ❌     |     ❌     |      ✅     |
| Task      |  ✅  |     ❌     |     ✅     |     ❌     |      ✅     |
| AutoTask  |  ✅  |     ✅     |     ✅     |     ❌     |      ✅     |
| Generator |  ✅  |     ❌     |     ❌     |     ✅     |      ❌     |
| AsyncGenerator | ✅ |  ❌     |     ✅     |     ✅     |      ❌     |
| FireAndForget  | ❌ |  ✅     |     ✅     |     ❌     |      ❌     |

- **Awaitable:** The resulting task can be awaited with `co_await`.
- **Auto-start:** The resulting task begins automatically when created. Types
  not marked *auto-start* must be started with a call to `coroutine.resume()`.
- **co_await:** The type supports the `co_await` operator to await another
  coroutine.
- **co_yield:** The coroutine can "return" multiple values with `co_yield`.
  Calling `co_await` on these types gives an `std::optional`, where
  `std::nullopt` represents that the generator is finished.
- **co_return:** The coroutine can return a value when finished. Types without
  this feature can only use the `void` form of `co_return` (`co_return;`).

Outside of a coroutine body where `co_await` is supported, the result of a
coroutine must be fetched with `await_resume()`.