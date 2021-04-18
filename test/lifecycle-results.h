#pragma once

#include "wscoro/task.h"

template<typename TaskT>
struct lcresult_t;

template<typename TaskT>
constexpr const char *const lcresult = lcresult_t<TaskT>::value;

#define LCRESULT_0(TEMPLATE, TASK, PARAM) \
  }; \
  template TEMPLATE \
  struct lcresult_t<wscoro::TASK PARAM> { \
  static constexpr const char *const value

#define LCRESULT(TASK)  LCRESULT_0(<typename T>, TASK, <T>)
#define LCRESULTV(TASK) LCRESULT_0(<>, TASK,)

struct not_what_you_think_t {

LCRESULT(Immediate) =
#include "lc-immediate.txt"

LCRESULT(Lazy) =
#include "lc-lazy.txt"

LCRESULT(Task) =
#include "lc-task.txt"

LCRESULT(AutoTask) =
#include "lc-autotask.txt"

LCRESULT(Generator) =
#include "lc-generator.txt"

LCRESULT(AsyncGenerator) =
#include "lc-asyncgenerator.txt"

LCRESULTV(FireAndForget) = "";
// #include "lc-fireandforget.txt"

};
