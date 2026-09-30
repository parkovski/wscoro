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
using Immediate = BasicTask<Promise<
  value::BasicReturn<T>,
  await::DisableAwait,
  exception::SyncThrow,
  suspend::BasicInitialSuspend<false>,
  suspend::BasicFinalSuspend<true>
>>;

/// A synchronously executing coroutine that waits to begin execution until it
/// is awaited. It cannot await other coroutines.
/// \param T The coroutine's return type.
template<class T = void>
using Lazy = BasicTask<Promise<
  value::BasicReturn<T>,
  await::DisableAwait,
  exception::AsyncThrow,
  suspend::BasicInitialSuspend<true>,
  suspend::BasicFinalSuspend<true>
>>;

/// Standard task type. Begins execution when awaited. Holds a continuation
/// handle to asynchronously resume the awaiter.
/// \param T The coroutine's return type.
/// \param S The scheduler.
template<class T = void, class S = SyncScheduler>
using Task = BasicTask<Promise<
  value::BasicReturn<T>,
  await::EnableAwait<await::ThisCoroutine>,
  exception::AsyncThrow,
  suspend::BasicInitialSuspend<true>,
  suspend::FinalSuspendWithContinuation
>, S>;

/// Task that begins execution when created.
/// \param T The coroutine's return type.
/// \param S The scheduler.
template<class T = void, class S = SyncScheduler>
using ImmediateTask = BasicTask<Promise<
  value::BasicReturn<T>,
  await::EnableAwait<await::ThisCoroutine>,
  exception::AsyncThrow,
  suspend::BasicInitialSuspend<false>,
  suspend::FinalSuspendWithContinuation
>, S>;

/// Synchronous generator.
/// \param T The generator's yield type.
template<class T>
using Generator = BasicGenerator<Promise<
  value::BasicYield<T>,
  await::DisableAwait,
  exception::SyncThrow,
  suspend::BasicInitialSuspend<true>,
  suspend::BasicFinalSuspend<true>
>>;

/// Asynchronous generator.
/// \param T The generator's yield type.
/// \param S The scheduler.
template<class T, class S = SyncScheduler>
using AsyncGenerator = BasicGenerator<Promise<
  value::YieldWithContinuation<T>,
  await::EnableAwait<await::ThisCoroutine>,
  exception::AsyncThrow,
  suspend::BasicInitialSuspend<true>
>, S>;

/// Non-awaitable task. Can await other tasks but can't be awaited itself or
/// return a value.
using FireAndForget = BasicCoroutine<Promise<
  value::BasicReturn<void>,
  await::EnableAwait<await::ThisCoroutine>,
  exception::SyncThrow,
  suspend::BasicInitialSuspend<false>,
  suspend::BasicFinalSuspend<false>
>>;

} // namespace wscoro
