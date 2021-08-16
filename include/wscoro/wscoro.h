#pragma once

#include "task.h"
#include "promise.h"
#include "value.h"
#include "exception.h"
#include "await.h"
#include "suspend.h"

namespace wscoro {

template<class T>
using Immediate = BasicTask<Promise<
  value::BasicReturn<T>,
  exception::SyncThrow,
  await::DisableAwait,
  suspend::BasicInitialSuspend<false>,
  suspend::BasicFinalSuspend<true>
>>;

template<class T>
using Lazy = BasicTask<Promise<
  value::BasicReturn<T>,
  exception::SyncThrow,
  await::DisableAwait,
  suspend::BasicInitialSuspend<true>,
  suspend::BasicFinalSuspend<true>
>>;

template<class T>
using Task = BasicTask<Promise<
  value::BasicReturn<T>,
  exception::AsyncThrow,
  await::EnableAwait<>,
  suspend::BasicInitialSuspend<false>,
  suspend::FinalSuspendWithContinuation
>>;

template<class T>
using DelayTask = BasicTask<Promise<
  value::BasicReturn<T>,
  exception::AsyncThrow,
  await::EnableAwait<>,
  suspend::BasicInitialSuspend<true>,
  suspend::FinalSuspendWithContinuation
>>;

template<class T>
using Generator = BasicGenerator<Promise<
  value::BasicYield<T>,
  exception::SyncThrow,
  await::DisableAwait,
  suspend::BasicInitialSuspend<true>,
  suspend::BasicFinalSuspend<false>
>>;

template<class T>
using AsyncGenerator = BasicGenerator<Promise<
  value::YieldWithContinuation<T>,
  exception::AsyncThrow,
  await::EnableAwait<>,
  suspend::BasicInitialSuspend<true>,
  suspend::FinalSuspendWithContinuation
>>;

using FireAndForget = BasicCoroutine<Promise<
  value::BasicReturn<void>,
  exception::SyncThrow,
  await::EnableAwait<>,
  suspend::BasicInitialSuspend<false>,
  suspend::BasicFinalSuspend<false>
>>;

} // namespace wscoro
