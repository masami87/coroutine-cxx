#pragma once

#include <functional>
#include <ucontext.h>
#include <vector>

#define COROUTINE_DEAD 0
#define COROUTINE_READY 1
#define COROUTINE_RUNNING 2
#define COROUTINE_SUSPEND 3

#define STACK_SIZE (1024 * 1024)
#define DEFAULT_COROUTINE 64

class Coroutine;
class Scheduler;
using coroutine_func = std::function<void(Scheduler *, void *)>;

// coroutine 调度器
class Scheduler final {
public:
  Scheduler();

  ~Scheduler();

  // 创建一个协程
  int coroutine_new(coroutine_func, void *ud);

  void coroutine_resume(int id);

  void coroutine_yield();

  int coroutine_status(int id);

  int coroutine_running() const;

// private:
  // 运行时栈
  // 栈的地址是由高地址往低地址生长，而数组的首地址又是整个数组的最低地址
  // 因此stack的位置是整个共享栈的栈顶，栈底位置在stack + STACK_SIZE
  char stack[STACK_SIZE];

  ucontext_t main; // 主协程的上下文
  int nco;         // 当前存活的协程个数
  int cap; // 协程管理器的当前最大容量，即可以同时支持多少个协程。如果不够了，则进行扩容
  int running;                 // 正在运行的协程ID
  std::vector<Coroutine *> co; // 一个一维数组，用于存放协程

  static void mainfunc(void *sptr);
};

// 开启协程调度器
Scheduler *coroutine_open(void);

// 切换到对应协程中执行
void coroutine_resume(Scheduler *, int id);

// 返回协程状态
int coroutine_status(Scheduler *, int id);

// 返回协程是否在运行
int coroutine_running(Scheduler *);

// 切出协程
void coroutine_yield(Scheduler *);