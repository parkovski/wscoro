#pragma once

#include "task.h"
#include "promise.h"
#include "value.h"
#include "exception.h"
#include "await.h"
#include "suspend.h"
#include "scheduler.h"

namespace wscoro {

/// A coroutine type that executes immediately and synchronously on creation.
/// It cannot await other coroutines.
/// \param T The coroutine's return type.
template<class T = void>
using immediate = basic_task<promise<
  value::basic_return<T>,
  await::disable_await,
  exception::sync_throw,
  suspend::basic_initial_suspend<false>,
  suspend::basic_final_suspend<true>
>, ready_scheduler>;

/// A synchronously executing coroutine that waits to begin execution until it
/// is awaited. It cannot await other coroutines.
/// \param T The coroutine's return type.
/// \param S The scheduler.
template<class T = void, template<class> class S = sync_scheduler>
using lazy = basic_task<promise<
  value::basic_return<T>,
  await::disable_await,
  exception::async_throw,
  suspend::basic_initial_suspend<true>,
  suspend::basic_final_suspend<true>
>, S>;

/// Standard task type. Begins execution when awaited. Holds a continuation
/// handle to asynchronously resume the awaiter.
/// \param T The coroutine's return type.
/// \param S The scheduler.
template<class T = void, template<class> class S = sync_scheduler>
using task = basic_task<promise<
  value::basic_return<T>,
  await::enable_await<await::this_coroutine>,
  exception::async_throw,
  suspend::basic_initial_suspend<true>,
  suspend::final_suspend_with_continuation
>, S>;

/// Task that begins execution when created.
/// \param T The coroutine's return type.
/// \param S The scheduler.
template<class T = void>
using immediate_task = basic_task<promise<
  value::basic_return<T>,
  await::enable_await<await::this_coroutine>,
  exception::async_throw,
  suspend::basic_initial_suspend<false>,
  suspend::final_suspend_with_continuation
>, sync_scheduler>;

/// Synchronous generator.
/// \param T The generator's yield type.
template<class T, template<class> class S = sync_scheduler>
using generator = basic_generator<promise<
  value::basic_yield<T>,
  await::disable_await,
  exception::sync_throw,
  suspend::basic_initial_suspend<true>,
  suspend::basic_final_suspend<true>
>, S>;

/// Asynchronous generator.
/// \param T The generator's yield type.
/// \param S The scheduler.
template<class T, template<class> class S = sync_scheduler>
using async_generator = basic_generator<promise<
  value::yield_with_continuation<T>,
  await::enable_await<await::this_coroutine>,
  exception::async_throw,
  suspend::basic_initial_suspend<true>
>, S>;

/// Non-awaitable task. Can await other tasks but can't be awaited itself or
/// return a value.
using fire_and_forget = basic_coroutine<promise<
  value::basic_return<void>,
  await::enable_await<await::this_coroutine>,
  exception::sync_throw,
  suspend::basic_initial_suspend<false>,
  suspend::basic_final_suspend<false>
>>;

} // namespace wscoro
