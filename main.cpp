#include "coroutine.h"
#include <iostream>

struct args {
  int n;
};

static void foo(Scheduler *S, void *ud) {
  auto arg = static_cast<args *>(ud);
  int start = arg->n;
  int i;
  for (i = 0; i < 5; i++) {
    printf("coroutine %d : %d\n", S->coroutine_running(), start + i);
    // 切出当前协程
    S->coroutine_yield();
  }
}
int main(int, char **) {
  auto S = new Scheduler;

  args arg1 = {42};
  args arg2 = {100};

  int co1 = S->coroutine_new(foo, &arg1);
  int co2 = S->coroutine_new(foo, &arg2);
  printf("main start\n");
  while (S->coroutine_status(co1) && S->coroutine_status(co2)) {
    // 使用协程 co1
    S->coroutine_resume(co1);
    // 使用协程 co2
    S->coroutine_resume(co2);
  }

  return 0;
}
