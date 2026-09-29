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
template<class T = void>
using Task = BasicTask<Promise<
  value::BasicReturn<T>,
  await::EnableAwait<await::ThisCoroutine>,
  exception::AsyncThrow,
  suspend::BasicInitialSuspend<true>,
  suspend::BasicFinalSuspend<true>
>>;

/// Task that begins execution when created.
/// \param T The coroutine's return type.
template<class T = void>
using ImmediateTask = BasicTask<Promise<
  value::BasicReturn<T>,
  await::EnableAwait<await::ThisCoroutine>,
  exception::AsyncThrow,
  suspend::BasicInitialSuspend<false>,
  suspend::BasicFinalSuspend<true>
>>;

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
template<class T>
using AsyncGenerator = BasicGenerator<Promise<
  value::BasicYield<T>,
  await::EnableAwait<await::ThisCoroutine>,
  exception::AsyncThrow,
  suspend::BasicInitialSuspend<true>,
  suspend::BasicFinalSuspend<true>
>>;

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
