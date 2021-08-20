#pragma once

#include "task.h"
#include "promise.h"
#include "value.h"
#include "exception.h"
#include "await.h"
#include "suspend.h"

namespace wscoro {

/// A coroutine type that executes immediately and synchronously on creation.
/// It cannot await other coroutines.
/// \param T The coroutine's return type.
template<class T = void>
using Immediate = BasicTask<Promise<
  value::BasicReturn<T>,
  exception::SyncThrow,
  await::DisableAwait,
  suspend::BasicInitialSuspend<false>,
  suspend::BasicFinalSuspend<true>
>>;

/// A synchronously executing coroutine that waits to begin execution until it
/// is awaited. It cannot await other coroutines.
/// \param T The coroutine's return type.
template<class T = void>
using Lazy = BasicTask<Promise<
  value::BasicReturn<T>,
  exception::SyncThrow,
  await::DisableAwait,
  suspend::BasicInitialSuspend<true>,
  suspend::BasicFinalSuspend<true>
>>;

/// Standard task type. Begins execution when awaited. Holds a continuation
/// handle to asynchronously resume the awaiter.
/// \param T The coroutine's return type.
template<class T = void>
using Task = BasicTask<Promise<
  value::BasicReturn<T>,
  exception::AsyncThrow,
  await::EnableAwait<await::ThisCoroutine>,
  suspend::BasicInitialSuspend<true>,
  suspend::FinalSuspendWithContinuation
>>;

/// Task that begins execution when created.
/// \param T The coroutine's return type.
template<class T = void>
using ImmediateTask = BasicTask<Promise<
  value::BasicReturn<T>,
  exception::AsyncThrow,
  await::EnableAwait<await::ThisCoroutine>,
  suspend::BasicInitialSuspend<false>,
  suspend::FinalSuspendWithContinuation
>>;

/// Synchronous generator.
/// \param T The generator's yield type.
template<class T>
using Generator = BasicGenerator<Promise<
  value::BasicYield<T>,
  exception::SyncThrow,
  await::DisableAwait,
  suspend::BasicInitialSuspend<true>,
  suspend::BasicFinalSuspend<true>
>>;

/// Asynchronous generator.
/// \param T The generator's yield type.
template<class T>
using AsyncGenerator = BasicGenerator<Promise<
  value::YieldWithContinuation<T>,
  exception::AsyncThrow,
  await::EnableAwait<await::ThisCoroutine>,
  suspend::BasicInitialSuspend<true>,
  suspend::FinalSuspendWithContinuation
>>;

/// Non-awaitable task. Can await other tasks but can't be awaited itself or
/// return a value.
using FireAndForget = BasicCoroutine<Promise<
  value::BasicReturn<void>,
  exception::SyncThrow,
  await::EnableAwait<>,
  suspend::BasicInitialSuspend<false>,
  suspend::BasicFinalSuspend<false>
>>;

} // namespace wscoro
