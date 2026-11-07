#include "co.h"
#include <assert.h>
#include <stdio.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>

#define NTHREAD 64
// glibc 对无缓冲流(setbuf(stdout,NULL))的 printf 会走 buffered_vfprintf，
// 它在栈上分配 BUFSIZ=8192 的缓冲区，一次 printf 就要 ~9KB 栈，所以不能小
#define STACK_SIZE 65536

enum status
{
  T_NEW = 1,  // ???已创建，还没在自己的栈上运行过
  T_RUNNABLE, // ???已启动，在 co_yield 处让出 CPU，等待恢复
  T_RUNNING,  // ???正在运行（cur 指向它）
  T_DEAD      // ???entry 已返回
};

struct co
{
  const char *name;
  void (*entry)(void *);
  void *args;
  enum status status;

  struct co *waiter;                                      // 是否由其他协程在等待当前协程
  jmp_buf context;                                        // registers
  uint8_t stack[STACK_SIZE] __attribute__((aligned(16))); // stack
};

struct co co_pool[NTHREAD];          // 协程池
int co_cnt = 0;                      // 已创建的协程数（槽位只增不复用，测试 6 个足够）
int cur = -1;                        // ???当前运行的协程下标；-1 表示 main
static jmp_buf main_ctx;             // ???main 在 co_wait 中让出 CPU 时保存的现场
static struct co *main_waits = NULL; // ???main 的 co_wait 正在等待谁

// 切换registers（框架给定）
static inline void stack_switch_call(void *sp, void *entry, uintptr_t arg)
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

// work: entry的包装函数
// note:协程真正的入口包装。entry 是被 jmp 进来的，假栈上没有合法的返回地址，
//      因此这个函数绝对不能 return：跑完 entry 后标记自己死亡，
//      然后永远留在调度循环里（调度器不会再选中 DEAD 协程）。
static void co_wrapper(void *co_)
{
  struct co *co = co_;
  co->entry(co->args);
  co->status = T_DEAD;
  // 返回调用co_wait的函数
  if (main_waits == co)
  {
    cur = -1; // co_wait只能返回到main中
    longjmp(main_ctx, 1);
  }
  // 保险操作：yield
  while (1)
  {
    co_yield();
  }
}

// work:从 cur 之后开始轮转，找一个 NEW/RUNNABLE 的协程；找不到返回 -1
static int next_runnable(void)
{
  for (int k = 1; k <= NTHREAD; k++)
  {
    int i = (cur + k) % NTHREAD; // cur == -1 时从 0 开始
    // printf("i = %d\n", i);
    if (i < co_cnt &&
        (co_pool[i].status == T_NEW || co_pool[i].status == T_RUNNABLE))
    {
      return i;
    }
  }
  return -1;
}

// work:切到下标 i 的协程。此函数不会再返回到调用者的执行流：
// 新协程第一次在自己的栈上启动；老协程恢复到它上次 co_yield 的地方。
static void switch_to(int i)
{
  struct co *co = &co_pool[i];
  int is_new = (co->status == T_NEW);
  co->status = T_RUNNING; // 由于协程没有中断的说法，所以switch_to就相当于RUNNING
  cur = i;

  if (is_new)
  {
    void *sp = co->stack + STACK_SIZE;
#if __x86_64__
    sp -= 8; // jmp 不压返回地址，手动凑出入口处 rsp % 16 == 8，
             // 模拟“刚被 call 过”的对齐状态，否则 printf 的 movaps 会炸
#endif
    stack_switch_call(sp, co_wrapper, (uintptr_t)co);
  }
  else
  {
    longjmp(co->context, 1);
  }
}

// work:创建协程但不执行
// 1. (create) use malloc to create obj in mem
// 2. (log) initialize
// eg:
// struct co* co1=co_start("thread-1",func1,arg1)
// struct co* co2=co_start("thread-2",func2,arg2)
struct co *co_start(const char *name, void (*func)(void *), void *arg)
{
  assert(co_cnt < NTHREAD);
  struct co *co = (struct co *)malloc(sizeof(struct co));
  co->name = name;
  co->entry = func;
  co->args = arg;
  co->status = T_NEW; // 为什么要有T_NEW的类型?
  co_pool[co_cnt++] = *co;
  return co;
}

// work:co_wait 会阻塞调用者，直到参数协程执行完成（T_DEAD）才返回。
// 1. setjmp:保存当前协程的registers
// 2. next_runnable:获取要切换的协程的
// 3. switch_to:切换协程
// note:本实现只支持 main 调用 co_wait（测试即如此）。
void co_wait(struct co *co)
{
  assert(co != NULL && cur == -1); // cur==-1表示只能从main中调用co_wait
  while (co->status != T_DEAD)     // ???
  {
    main_waits = co;           // 可以换成waiter
    if (setjmp(main_ctx) == 0) // setjmp(cur_ctx)
    {
      // 目标还活着 ⇒ 至少它自己是可运行的
      int next = next_runnable();
      assert(next >= 0); // TODO: Failed???
      switch_to(next);
    }
    // 被协程 longjmp 回来后，重新检查目标状态
  }
  main_waits = NULL;
  free(co);
}

// work:退出当前协程
// 1. 为每一个协程分配独立的 stack
// 2. 在协程自己的执行流上 setjmp 保存上下文（保存位置的正确性是关键）
// 3. 把自己标记为 RUNNABLE，轮转选下一个协程：
//    新协程用 stack_switch_call 在它栈顶启动，老协程 longjmp 恢复
// 4. 没有可运行的协程时 longjmp 回 main
void co_yield()
{
  int self = cur;
  assert(self >= 0); // co_yield 只应在协程里调用

  if (setjmp(co_pool[self].context) == 0)
  {
    if (co_pool[self].status == T_RUNNING)
    {
      co_pool[self].status = T_RUNNABLE;
    }
    // 死协程的最后一次 yield 保持 T_DEAD，不会被再次调度

    int next = next_runnable();
    if (next < 0)
    {
      cur = -1;             // 接下来运行的是 main ??? 需要修改成其他协程
      longjmp(main_ctx, 1); // 全部协程跑完，回到 main
    }
    switch_to(next);
  }
  // setjmp 第二次返回（被别人恢复）时，从这里继续执行
}
