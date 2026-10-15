# wscoro - Bring-your-own-executor C++20 coroutine library

**Currently supported compilers: MSVC, GCC, Clang.**

wscoro implements only the essentials needed to use C++20 coroutines. There are
no thread pools or executors; instead wscoro provides types for chaining
coroutines. C++20's coroutine support features are very low-level and wscoro
does not cover all possible cases, however it does support many common
scenarios.

## Usage

There are two main ways to use this library. The first is to write your own
executor (scheduler) and provide it as a template parameter to the appropriate
task types. The second is to use the task types as-is, and use
`co_await wscoro::this_coroutine` to get a handle to the current coroutine and
pass it to your executor, and then `co_await std::suspend_always{}` to wait for
your executor to resume the task. This second usage pattern is demonstrated in
my project [wsudo](https://github.com/parkovski/wsudo), in the CorIO files.

## Details

wscoro provides several different task type templates separated by feature
support:

| Type | Awaitable | Auto-start | `co_await` | `co_yield` | `co_return` |
| ---: | :-------: | :--------: | :--------: | :--------: | :---------: |
| immediate |  ✅  |     ✅     |     ❌     |     ❌     |      ✅     |
| lazy      |  ✅  |     ❌     |     ❌     |     ❌     |      ✅     |
| task      |  ✅  |     ❌     |     ✅     |     ❌     |      ✅     |
| immediate_task |  ✅ |   ✅   |     ✅     |     ❌     |      ✅     |
| generator |  ✅  |     ❌     |     ❌     |     ✅     |      ❌     |
| async_generator | ✅ |  ❌    |     ✅     |     ✅     |      ❌     |
| fire_and_forget  | ❌ |  ✅   |     ✅     |     ❌     |      ❌     |

- **Awaitable:** The resulting task can be awaited with `co_await`.
- **Auto-start:** The resulting task begins automatically when created. Types
  not marked *auto-start* must be started by awaiting them or with a call to
  `coroutine.resume()`.
- **co_await:** The type supports the `co_await` operator to await another
  coroutine.
- **co_yield:** The coroutine can "return" multiple values with `co_yield`.
  Calling `co_await` on these types gives an `std::optional`, where
  `std::nullopt` represents that the generator is finished.
- **co_return:** The coroutine can return a value when finished. Types without
  this feature can only use the `void` form of `co_return` (`co_return;`).

Outside of a coroutine body where `co_await` is supported, the result of a
coroutine must be fetched with `await_resume()`.

It is easy to create your own task type with the building blocks this library
provides. See how the types are defined in
[wscoro.h](https://github.com/parkovski/wscoro/blob/master/include/wscoro/wscoro.h).