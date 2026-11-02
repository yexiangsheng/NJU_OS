// test_strcpy.c —— strcpy 单元测试（在宿主机上直接用 gcc 编译运行，不经过 AM 构建）
//
// 编译并运行（在 abstract-machine/klib/tests/ 目录下）：
//   gcc -fno-builtin -I../include -I../../am/include -DARCH_H='"arch/native.h"' \
//       -o test_strcpy test_strcpy.c ../src/string.c && ./test_strcpy
//   (-fno-builtin 必须：否则 gcc 会把常量参数的 strcpy 调用编译期内联，测不到你的实现)
//
// 通过返回 0；有失败项返回非 0，并逐条打印 FAIL。
// 注：'\0' 之后 dst 中的内容标准未规定，本测试不检查（按字拷贝的实现可能写到边界）。

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
char *strcpy(char *dst, const char *src);

static int passed = 0, failed = 0;

#define CHECK(desc, ok) do { \
  if (ok) { passed++; } \
  else { failed++; printf("FAIL  %s\n", (desc)); } \
} while (0)

// ---- 崩溃定位：记录当前正在执行的用例，SIGSEGV/SIGBUS 时打印出来再退出 ----
// 描述在正常上下文中提前格式化好，信号处理器里只做读取和 write(2)
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
                   "\nCRASH: strcpy 在此用例中触发信号 %d: %s\n", sig, cur_case);
  if (n > 0) write(2, tmp, (size_t)n);
  signal(sig, SIG_DFL);
  raise(sig);
}

static char srcbuf[100000 + 32];
static char dstbuf[100000 + 32];
static char refbuf[100000 + 32];

// 用字母模式填充 srcbuf+off 的 [0,len) 并以 '\0' 结尾，refbuf 保存副本
static void fill_src(size_t off, size_t len) {
  for (size_t i = 0; i < len; i++) srcbuf[off + i] = 'a' + (char)((off + i) % 26);
  srcbuf[off + len] = '\0';
  for (size_t i = 0; i <= len; i++) refbuf[i] = srcbuf[off + i];
}

// 往 dst 写入垃圾哨兵，确保每个字节都必须被 strcpy 写过
static void poison_dst(size_t off, size_t len) {
  for (size_t i = 0; i < len + 16; i++) dstbuf[off + i] = (char)0xAA;
}

// 逐字节比较两个以 '\0' 结尾的字符串（避免依赖任何被测库函数）
static int same_str(const char *a, const char *b) {
  size_t i = 0;
  while (a[i] != '\0' && a[i] == b[i]) i++;
  return a[i] == b[i];
}

// 完整检查一次 strcpy(srcbuf+src_off -> dstbuf+dst_off，长度 len)：
//   1. 返回值是 dst
//   2. dst 的 [0, len] 与 src 一致（含结尾 '\0'）
//   3. src 未被修改
static void check_case(size_t len, size_t src_off, size_t dst_off) {
  fill_src(src_off, len);
  poison_dst(dst_off, len);
  set_case("check_case len=%zu src_off=%zu dst_off=%zu", len, src_off, dst_off);

  char *ret = strcpy(dstbuf + dst_off, srcbuf + src_off);

  if (ret == dstbuf + dst_off) {
    passed++;
  } else {
    failed++;
    printf("FAIL  strcpy: 返回值不是 dst (len=%zu src_off=%zu dst_off=%zu)\n",
           len, src_off, dst_off);
  }

  int content_ok = 1;
  size_t bad = 0;
  for (size_t i = 0; i <= len; i++)
    if (dstbuf[dst_off + i] != refbuf[i]) { content_ok = 0; bad = i; break; }
  if (content_ok) {
    passed++;
  } else {
    failed++;
    printf("FAIL  strcpy: dst 内容不一致 (len=%zu src_off=%zu dst_off=%zu 首个差异 idx=%zu%s)\n",
           len, src_off, dst_off, bad, bad == len ? " —— 结尾 '\\0' 未写入" : "");
  }

  int src_ok = 1;
  for (size_t i = 0; i <= len; i++)
    if (srcbuf[src_off + i] != refbuf[i]) { src_ok = 0; break; }
  if (src_ok) {
    passed++;
  } else {
    failed++;
    printf("FAIL  strcpy: src 被修改 (len=%zu src_off=%zu)\n", len, src_off);
  }
}

int main() {
  signal(SIGSEGV, on_crash);
  signal(SIGBUS, on_crash);

  char d[64];
  char *r;

  // 1. 基本字面量
  set_case("字面量 \"hello\"");
  r = strcpy(d, "hello");
  CHECK("返回 dst", r == d);
  CHECK("内容 hello", same_str(d, "hello"));
  CHECK("终止符在 [5]", d[5] == '\0');

  for (int i = 0; i < 8; i++) d[i] = 'x';
  set_case("字面量 空串");
  r = strcpy(d, "");
  CHECK("空串: 返回 dst", r == d);
  CHECK("空串: dst[0] 为 '\\0'", d[0] == '\0');

  set_case("字面量 数字+符号");
  r = strcpy(d, "0123456789!@#$^&*()");
  CHECK("数字+符号: 返回 dst", r == d);
  CHECK("数字+符号: 内容", same_str(d, "0123456789!@#$^&*()"));

  set_case("字面量 UTF-8");
  r = strcpy(d, "你好");
  CHECK("UTF-8: 返回 dst", r == d);
  CHECK("UTF-8: 按字节拷贝", same_str(d, "你好"));

  // 2. 全长度扫描 0..255（覆盖按字拷贝实现的所有边界/差一情况）
  for (size_t len = 0; len <= 255; len++)
    check_case(len, 0, 0);

  // 3. 对齐组合：几个代表长度 × src 偏移 0..7 × dst 偏移 0..7
  size_t lens[] = { 0, 1, 2, 7, 8, 15 };
  for (size_t li = 0; li < sizeof(lens) / sizeof(lens[0]); li++)
    for (size_t so = 0; so < 8; so++)
      for (size_t dof = 0; dof < 8; dof++)
        check_case(lens[li], so, dof);

  // 4. 长字符串
  size_t long_lens[] = { 1023, 1024, 1025, 4095, 4096, 4097, 99999 };
  for (size_t i = 0; i < sizeof(long_lens) / sizeof(long_lens[0]); i++)
    check_case(long_lens[i], 0, 0);

  // 5. 复用同一 dst：长 → 短 → 长
  fill_src(0, 200);
  set_case("复用 dst: 拷入长串(200)");
  strcpy(dstbuf, srcbuf);
  fill_src(0, 3);
  set_case("复用 dst: 拷入短串(3)");
  strcpy(dstbuf, srcbuf);
  CHECK("复用 dst 变短", same_str(dstbuf, srcbuf));
  fill_src(0, 200);
  set_case("复用 dst: 再拷入长串(200)");
  strcpy(dstbuf, srcbuf);
  CHECK("复用 dst 再变长", same_str(dstbuf, srcbuf));

  printf("== strcpy 测试: %d passed, %d failed ==\n", passed, failed);
  return failed ? 1 : 0;
}
