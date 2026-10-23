#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include "pstree.h"

static const char *version = "0.1.1";

/* ---- 全局状态 ---- */
static proc_info *info;   /* 所有进程，来自 /proc */
static int nproc;
static int *child_idx;    /* 临时缓冲：某个进程的直接孩子下标 */

static int show_pids;     /* -p */
static int numeric_sort;  /* -n */
static int term_cols;     /* 终端宽度，0 表示不截断（输出到管道时） */

void print_version()
{
  printf("pstree (PSmisc) %s\n", version);
}

static void usage(FILE *out)
{
  fprintf(out,
          "Usage: pstree [OPTION...]\n"
          "Display a tree of processes.\n\n"
          "  -p, --show-pids     show process PIDs\n"
          "  -n, --numeric-sort  sort children by PID\n"
          "  -V, --version       print version information\n"
          "  -h, --help          print this help\n");
}

/* ---- 第一步：读 /proc，收集 (pid, ppid, name) ---- */

/* 解析 /proc/<pid>/stat 一行；进程消失（fopen 失败）不算错误，返回 -1 跳过即可 */
static int read_proc(proc_info *p, int pid)
{
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/stat", pid);

  FILE *fp = fopen(path, "r");
  if (fp == NULL)
    return -1;

  char buf[4096];
  if (fgets(buf, sizeof(buf), fp) == NULL)
  {
    fclose(fp);
    return -1;
  }
  fclose(fp);

  /* 格式: pid (comm) S ppid pgrp session ...
   * comm 里可以含空格和括号，所以必须找 *最后一个* ')' 再向后解析 */
  char *lp = strchr(buf, '(');
  char *rp = strrchr(buf, ')');
  if (lp == NULL || rp == NULL || rp < lp)
    return -1;

  size_t len = rp - lp - 1;
  if (len >= sizeof(p->name))
    len = sizeof(p->name) - 1;
  memcpy(p->name, lp + 1, len);
  p->name[len] = '\0';

  /* 状态字符用 %*c 跳过，只取后面的 ppid */
  if (sscanf(rp + 1, " %*c %d", &p->ppid) != 1)
    return -1;

  p->pid = pid;
  return 0;
}

static void *xmalloc(size_t size)
{
  void *p = malloc(size);
  if (p == NULL)
  {
    fprintf(stderr, "pstree: out of memory\n");
    exit(EXIT_FAILURE);
  }
  return p;
}

static void get_info()
{
  DIR *proc_dir = opendir("/proc");
  if (proc_dir == NULL)
  {
    fprintf(stderr, "pstree: cannot open /proc: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }

  int cap = 64;
  info = xmalloc(cap * sizeof(proc_info));

  struct dirent *entry;
  while ((entry = readdir(proc_dir)) != NULL)
  {
    /* 名字全是数字的目录才是进程 */
    const char *s = entry->d_name;
    int is_pid = s[0] != '\0';
    for (const char *q = s; *q != '\0'; q++)
      if (!isdigit((unsigned char)*q))
      {
        is_pid = 0;
        break;
      }
    if (!is_pid)
      continue;

    proc_info p;
    if (read_proc(&p, atoi(s)) == 0)
    {
      if (nproc == cap)
      {
        cap *= 2;
        info = realloc(info, cap * sizeof(proc_info));
        if (info == NULL)
        {
          fprintf(stderr, "pstree: out of memory\n");
          exit(EXIT_FAILURE);
        }
      }
      info[nproc++] = p; /* d_name 缓冲区会被复用，p.name 已是拷贝 */
    }
  }
  closedir(proc_dir);

  child_idx = xmalloc((nproc > 0 ? nproc : 1) * sizeof(int));
}

/* ---- 第二步：建树 ---- */

static int find_proc(int pid)
{
  for (int i = 0; i < nproc; i++)
    if (info[i].pid == pid)
      return i;
  return -1;
}

/* qsort 比较器：-n 按 pid，默认按名字（名字相同再按 pid，保证稳定输出） */
static int cmp_child(const void *a, const void *b)
{
  int i = *(const int *)a;
  int j = *(const int *)b;
  if (numeric_sort)
    return info[i].pid - info[j].pid;
  int r = strcmp(info[i].name, info[j].name);
  return r != 0 ? r : info[i].pid - info[j].pid;
}

/* 找 ppid 的所有直接孩子，放进 child_idx 并排序，返回个数 */
static int gather_children(int ppid)
{
  int n = 0;
  for (int i = 0; i < nproc; i++)
    if (info[i].ppid == ppid)
      child_idx[n++] = i;
  qsort(child_idx, n, sizeof(int), cmp_child);
  return n;
}

/* ---- 第三步：打印 ---- */

static void init_term_cols()
{
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
    term_cols = ws.ws_col;
  else
  {
    const char *c = getenv("COLUMNS");
    if (c != NULL)
      term_cols = atoi(c);
  }
  if (term_cols <= 0)
    term_cols = 0; /* 输出到管道：不截断 */
}

/* 按终端宽度截断一行（UTF-8 多字节字符不劈开），超出部分用 + 结尾 */
static void print_line(const char *s)
{
  if (term_cols <= 0)
  {
    printf("%s\n", s);
    return;
  }

  int col = 0;
  const char *p = s;
  while (*p != '\0' && col < term_cols - 1)
  {
    if ((*p & 0xc0) != 0x80) /* 只在多字节序列首字节处记一列 */
      col++;
    p++;
  }
  fwrite(s, 1, p - s, stdout);
  if (*p != '\0')
    putchar('+');
  putchar('\n');
}

/* stem 是传给整个子树的竖线前缀，is_last 决定本节点用 └─ 还是 ├─ */
static void print_tree(int idx, const char *stem, int is_last)
{
  char label[300];
  if (show_pids)
    snprintf(label, sizeof(label), "%s(%d)", info[idx].name, info[idx].pid);
  else
    snprintf(label, sizeof(label), "%s", info[idx].name);

  char line[4096];
  if (stem[0] == '\0')
    snprintf(line, sizeof(line), "%s", label);
  else
    snprintf(line, sizeof(line), "%s%s%s", stem, is_last ? "└─" : "├─", label);
  print_line(line);

  char child_stem[4096];
  snprintf(child_stem, sizeof(child_stem), "%s%s", stem,
           is_last ? "   " : "│  ");

  int n = gather_children(info[idx].pid);
  for (int i = 0; i < n; i++)
    print_tree(child_idx[i], child_stem, i == n - 1);
}

int parse_arg(int argc, char *argv[])
{
  static struct option long_options[] = {
      {"version", no_argument, NULL, 'V'},
      {"show-pids", no_argument, NULL, 'p'},
      {"numeric-sort", no_argument, NULL, 'n'},
      {"help", no_argument, NULL, 'h'},
      {0, 0, 0, 0}};

  int opt;
  while ((opt = getopt_long(argc, argv, "pnVh", long_options, NULL)) != -1)
  {
    switch (opt)
    {
    case 'p':
      show_pids = 1;
      break;
    case 'n':
      numeric_sort = 1;
      break;
    case 'V':
      print_version();
      exit(EXIT_SUCCESS);
    case 'h':
      usage(stdout);
      exit(EXIT_SUCCESS);
    default: /* 非法参数：提示到 stderr，非零退出码 */
      usage(stderr);
      exit(EXIT_FAILURE);
    }
  }
  return 0;
}

int main(int argc, char *argv[])
{
  parse_arg(argc, argv);

  get_info();
  init_term_cols();

  /* ppid 为 0（或父进程已消失）的进程就是树根（systemd、kthreadd 等） */
  int *roots = xmalloc((nproc > 0 ? nproc : 1) * sizeof(int));
  int nroots = 0;
  for (int i = 0; i < nproc; i++)
    if (info[i].ppid == 0 || find_proc(info[i].ppid) < 0)
      roots[nroots++] = i;
  qsort(roots, nroots, sizeof(int), cmp_child);

  for (int i = 0; i < nroots; i++)
    print_tree(roots[i], "", 1);

  free(roots);
  free(child_idx);
  free(info);
  return EXIT_SUCCESS;
}
