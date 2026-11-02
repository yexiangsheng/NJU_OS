// test_strlen.c —— strlen 单元测试（在宿主机上直接用 gcc 编译运行，不经过 AM 构建）
//
// 编译并运行（在 abstract-machine/klib/tests/ 目录下）：
//   gcc -fno-builtin -I../include -I../../am/include -DARCH_H='"arch/native.h"' \
//       -o test_strlen test_strlen.c ../src/string.c && ./test_strlen
//   (-fno-builtin 必须：否则 gcc 会把 strlen("常量") 编译期折叠成常量，测不到你的实现)
//
// 通过返回 0；有失败项返回非 0，并逐条打印 FAIL。

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <signal.h>
#include <unistd.h>

// ---- string.c 引用到的 AM/klib 环境符号，在宿主机上的最小替代 ----
void putch(char ch) { putchar(ch); }
void halt(int code) { exit(code); }
int printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vfprintf(stdout, fmt, ap);
  va_end(ap);
  return r;
}

// 被测函数，实现位于 src/string.c
size_t strlen(const char *s);

static int passed = 0, failed = 0;

#define CHECK(desc, got, want) do { \
  size_t g_ = (size_t)(got), w_ = (size_t)(want); \
  if (g_ == w_) { passed++; } \
  else { failed++; printf("FAIL  %s: got %zu, want %zu\n", (desc), g_, w_); } \
} while (0)

// ---- 崩溃定位：记录当前正在执行的用例，SIGSEGV/SIGBUS 时打印出来再退出 ----
static char cur_case[160];

static void set_case(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(cur_case, sizeof(cur_case), fmt, ap);
  va_end(ap);
}

static void on_crash(int sig) {
  char tmp[224];
  int n = snprintf(tmp, sizeof(tmp),
                   "\nCRASH: strlen 在此用例中触发信号 %d: %s\n", sig, cur_case);
  if (n > 0) write(2, tmp, (size_t)n);
  signal(sig, SIG_DFL);
  raise(sig);
}

static char buf[100000 + 16];
static char ref[sizeof(buf)];

// 用 [0, len) 的字母模式填充 buf 并以 '\0' 结尾，ref 保存副本用于检查不可变性
static void fill(size_t len) {
  for (size_t i = 0; i < len; i++) buf[i] = 'a' + (char)(i % 26);
  buf[len] = '\0';
  for (size_t i = 0; i <= len; i++) ref[i] = buf[i];
}

static int bytes_same(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++)
    if (a[i] != b[i]) return 0;
  return 1;
}

// 一次完整检查：返回值正确 + 不修改字符串内容
static void check_len(size_t len) {
  fill(len);
  set_case("check_len len=%zu", len);
  size_t got = strlen(buf);
  if (got == len) {
    passed++;
  } else {
    failed++;
    printf("FAIL  strlen: len=%zu, want %zu, got %zu\n", len, len, got);
  }
  if (!bytes_same(buf, ref, len + 1)) {
    failed++;
    printf("FAIL  strlen 修改了字符串内容: len=%zu\n", len);
  }
}

int main() {
  signal(SIGSEGV, on_crash);
  signal(SIGBUS, on_crash);

  // 1. 字面量
  set_case("字面量 空串");
  CHECK("空串",       strlen(""), 0);
  set_case("字面量 单字符");
  CHECK("单字符",     strlen("a"), 1);
  set_case("字面量 hello");
  CHECK("普通字符串", strlen("hello"), 5);
  set_case("字面量 含空格");
  CHECK("含空格",     strlen("hello world"), 11);
  set_case("字面量 数字+符号");
  CHECK("数字+符号",  strlen("0123456789!@#$^&*()"), 19);
  set_case("字面量 UTF-8");
  CHECK("UTF-8 按字节计数", strlen("你好"), 6);

  // 2. 遇到第一个 '\0' 即结束，后面的内容应被忽略
  fill(20);
  buf[3] = '\0';
  set_case("中间嵌 \\0");
  CHECK("中间嵌 \\0", strlen(buf), 3);

  // 3. 逐一测试 0..255 的所有长度（覆盖按字读取实现中所有边界/差一情况）
  for (size_t len = 0; len <= 255; len++)
    check_len(len);

  // 4. 长字符串
  size_t long_lens[] = { 1023, 1024, 1025, 4095, 4096, 4097, 99999 };
  for (size_t i = 0; i < sizeof(long_lens) / sizeof(long_lens[0]); i++)
    check_len(long_lens[i]);

  // 5. 重复调用结果一致
  fill(77);
  set_case("重复调用 len=77");
  CHECK("重复调用 1", strlen(buf), 77);
  CHECK("重复调用 2", strlen(buf), 77);

  printf("== strlen 测试: %d passed, %d failed ==\n", passed, failed);
  return failed ? 1 : 0;
}
