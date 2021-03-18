#include "coroutine.h"
#include <assert.h>
#include <cstring>
#include <iostream>
#include <unistd.h>

class Coroutine final {
public:
  Coroutine(Scheduler *S, coroutine_func func, void *ud)
      : sch(S), func(func), ud(ud), cap(0), size(0), status(COROUTINE_READY),
        stack(nullptr) {}

  ~Coroutine() { delete[] stack; }

  void save_stack(char *top) {
    // 这个dummy很关键，是求取整个栈的关键
    // 这个非常经典，涉及到linux的内存分布，栈是从高地址向低地址扩展，因此
    // S->stack + STACK_SIZE就是运行时栈的栈底
    // dummy，此时在栈中，肯定是位于最底的位置的，即栈顶
    // top - &dummy 即整个栈的容量
    char dummy = 0;
    int current_size = top - &dummy;
    assert(current_size <= STACK_SIZE);
    // 当前已分配的大小不够
    if (cap < current_size) {
      delete[] stack;
      cap = current_size;
      stack = new char[cap];
    }
    size = current_size;
    memcpy(stack, &dummy, size);
  }

private:
  // 协程执行的函数
  coroutine_func func;

  // 协程参数
  void *ud;

  // 上下文
  ucontext_t ctx;

  // 协程所属的调度器
  Scheduler *sch;

  // 已分配的内存大小
  ptrdiff_t cap;

  // 当前协程运行实际大小
  ptrdiff_t size;

  // 运行状态
  int status;

  // 栈
  char *stack;

  friend class Scheduler;
};

Scheduler::Scheduler()
    : nco(0), cap(DEFAULT_COROUTINE), running(-1), co(cap, nullptr) {}

Scheduler::~Scheduler() {}

int Scheduler::coroutine_new(coroutine_func func, void *ud) {
  auto _co = new Coroutine(this, func, ud);
  if (nco >= cap) {
    // 目前协程的数量已经大于调度器的容量，那么进行扩容
    int id = cap; // 新的协程的id直接为当前容量的大小
    co.push_back(_co);
    cap = co.size();
    ++nco;
    return id;
  } else {
    int i = 0;
    for (; i < cap; ++i) {
      // 因为前nco有很大概率都非NULL的，直接跳过去更好
      int id = (i + nco) % cap;
      if (co[id] == nullptr) {
        co[id] = _co;
        ++nco;
        return id;
      }
    }
  }
  assert(0);
  return -1;
}

void Scheduler::coroutine_resume(int id) {
  assert(running == -1);
  assert(id >= 0 && id < cap);

  // 取出协程
  auto _co = co[id];
  if (_co == nullptr)
    return;

  int status = _co->status;
  switch (status) {
  case COROUTINE_READY:
    // 初始化ucontext_t结构体，将当前的上下文放到C->ctx里
    // makecontext之前必须调用getcontext初始化context，否则会段错误core
    getcontext(&_co->ctx);
    // 将当前协程的运行时栈的栈顶设置为S->stack
    // 每个协程都这么设置，这就是所谓的共享栈。（注意，这里是栈顶）
    // 当切换上下文进入mainfunc之后协程的栈顶会指向ss_sp + ss_size，并且向下生长
    // makecontext之前必须给uc_stack分配栈空间，否则也会段错
    // 非makecontext创建的上下文不要修改，如main
    _co->ctx.uc_stack.ss_sp = this->stack;
    _co->ctx.uc_stack.ss_size = STACK_SIZE;
    _co->ctx.uc_link = &this->main; // 当前协程执行完之后切换到主协程
    _co->status = COROUTINE_RUNNING;
    this->running = id;

    // 创建协程
    makecontext(&_co->ctx, (void (*)())mainfunc, 1, (void *)this);

    // 将当前的上下文放入S->main中，并将C->ctx的上下文替换到当前上下文
    swapcontext(&this->main, &_co->ctx);
    break;
  case COROUTINE_SUSPEND:
    // 将协程所保存的栈的内容，拷贝到当前运行时栈中
    // 其中C->size在yield时有保存
    memcpy(stack + STACK_SIZE - _co->size, _co->stack, _co->size);
    this->running = id;
    _co->status = COROUTINE_RUNNING;
    swapcontext(&this->main, &_co->ctx);
    break;
  default:
    assert(0);
  }
}

void Scheduler::mainfunc(void *sptr) {
  Scheduler *s = static_cast<Scheduler *>(sptr);
  int id = s->running;
  auto _co = s->co[id];
  _co->func(s, _co->ud);
  delete _co;
  s->co[id] = nullptr;
  --s->nco;
  s->running = -1;
}

/**
 * 将当前正在运行的协程让出，切换到主协程上
 * @param S 协程调度器
 */
void Scheduler::coroutine_yield() {
  // 取出当前正在运行的协程
  int id = running;
  assert(id >= 0);

  auto _co = co[id];
  assert((char *)&_co > stack);

  // 将当前运行的协程的栈内容保存起来
  _co->save_stack(stack + STACK_SIZE);

  // 将当前栈的状态改为 挂起
  _co->status = COROUTINE_SUSPEND;
  running = -1;

  // 从协程切换到主协程中
  swapcontext(&_co->ctx, &main);
}

int Scheduler::coroutine_status(int id) {
  assert(id >= 0 && id < cap);

  if (co[id] == nullptr) {
    return COROUTINE_DEAD;
  }
  return co[id]->status;
}

int Scheduler::coroutine_running() const { return running; }