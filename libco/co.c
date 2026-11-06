#include "co.h"
#include <stdlib.h>
#define NTHREAD 64

enum status
{
  T_LIVE = 1,
  T_RUNNING,
  T_WAITTING,
  T_DEAD
};

struct co
{
  char name[8];
  // pthread_t *thread;
  void (*entry)(void *);
  void *args;
  int statu;
  int id;
};

struct co co_pool[NTHREAD];
int co_idx = 0;
struct co *cur_co = (struct co *){"current", NULL, NULL, 0, 0};

struct co *co_start(const char *name, void (*func)(void *), void *arg)
{
  struct co *th = (struct co *)malloc(sizeof(struct co));
  th->name = name;
  th->entry = func;
  th->args = arg;
  th->statu = T_WAITTING;
  co_pool[co_idx++] = *th;
  th->id = co_idx;

  return th;
}

// co_wait会等待参数协程执行完成，
// 然后返回当当前协程
void co_wait(struct co *co)
{
  assert(cur_co->name != "current");
  cur_co = co;
  assert(co->statu != T_DEAD);
  co->entry(co->args);
  co->statu = T_DEAD;
  free(co);
}

// co_yield会将当前协程的regs保留在它的stack中
// 并且从当前T_WAITTING中随机选择一个协程执行
void co_yield()
{
  struct co *wait_co;
  int i = 0;
  for (struct co *th = co_pool; (th->id = co_idx) < NTHREAD; th++)
  {
    if (th->statu == T_WAITTING)
    {
      wait_co[i++] = *th;
    }
  }
  assert(i != 0);
  int select_idx = 64 % i;
  cur_co = &co_pool[select_idx];
  cur_co->entry(cur_co->args);
  cur_co->statu = T_DEAD;
}
