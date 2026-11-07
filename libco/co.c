#include "co.h"
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#define NTHREAD 64
#define STACK_SIZE 4096

enum status {
  T_NEW = 1,
  T_RUNNING,
  T_WAITTING,
  T_DEAD
};

struct co {
  char* name;
  // pthread_t *thread;
  void (*entry)(void*);
  void* args;
  int statu;
  int id;

  jmp_buf context;
  uint8_t stack[STACK_SIZE]; // stack
};

// 切换registers
static inline void stack_switch_call(void* sp, void* entry, uintptr_t arg)
{
  asm volatile(
#if __x86_64__
      "movq %0, %%rsp; movq %2, %%rdi; jmp *%1"
      :
      : "b"((uintptr_t)sp), "d"(entry), "a"(arg)
      : "memory"
#else
      "movl %0, %%esp; movl %2, 4(%0); jmp *%1"
      :
      : "b"((uintptr_t)sp - 8), "d"(entry), "a"(arg)
      : "memory"
#endif
  );
}

struct co co_pool[NTHREAD];
int co_idx = 0;
struct co* cur_co;

struct co* co_start(const char* name, void (*func)(void*), void* arg)
{
  struct co* th = (struct co*)malloc(sizeof(struct co));
  assert(th != NULL);
  th->name = name;
  th->entry = func;
  th->args = arg;
  th->statu = T_NEW;
  co_pool[co_idx++] = *th;
  th->id = co_idx;
  setjmp(th->context);

  return th;
}

// co_wait会等待参数协程执行完成，
// 然后返回当当前协程
void co_wait(struct co* co)
{
  assert(cur_co->name != NULL);
  cur_co = co;
  // switch stack
  stack_switch_call(&cur_co->stack, cur_co->entry, (uintptr_t)cur_co->args);
  assert(co->statu != T_DEAD);
  longjmp(co->context, 1);
  co->statu = T_RUNNING;
  co->entry(co->args);
  co->statu = T_DEAD;
  free(co);
}

// 1.为每一个协程分配独立的stack;rsp
// 2.setjmp保存当前协程的上下文
// 3.stack_switch_call切换到下一个协程的上下文
// 4.longjmp恢复当前协程的上下文
void co_yield ()
{
  struct co* wait_co = NULL;
  int i = 0;
  for (struct co* th = co_pool; (th->id = co_idx) < NTHREAD; th++) {
    if (th->statu == T_NEW) {
      wait_co[i++] = *th;
    }
  }
  assert(i != 0);
  int select_idx = 64 % i;
  cur_co = &co_pool[select_idx];
  stack_switch_call(&cur_co->stack, cur_co->entry, (uintptr_t)cur_co->args);
  longjmp(cur_co->context, 1);
  cur_co->statu = T_RUNNING;
  cur_co->entry(cur_co->args);
  cur_co->statu = T_DEAD;
}
