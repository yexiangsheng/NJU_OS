#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include "pstree.h"

static inline void print_version()
{
  fprintf(stdout, "pstree (PSmisc) %s", version);
}
void get_info()
{
}
void show_pids()
{
}
static inline void numeric_sort()
{
}

int parse_arg(int argc, char *argv[])
{
  static struct option long_options[] = {
      {"version", no_argument, NULL, 'V'},
      {"show-pids", no_argument, NULL, 'p'},
      {"numeric-sort", no_argument, NULL, 'n'},
      {0, 0, 0, 0}};
  int opt;
  while ((opt = getopt_long(argc, argv, "p:n:V", long_options, NULL)) != -1)
  {
    switch (opt)
    {
    case 'V':
      print_version();
      break;
    case 'p':
      break;
    case 'n':
      break;
    default:
      fprintf(stderr, "Usage: %s [OPTION...] IMAGE [args]\n\n", argv[0]);
      exit(0);
    }
  }
  return 0;
}

int main(int argc, char *argv[])
{
  for (int i = 0; i < argc; i++)
  {
    assert(argv[i]);
    printf("argv[%d] = %s\n", i, argv[i]);
  }
  assert(!argv[argc]);

  // 1. getopt解析参数
  if (argc != 1)
  {
    parse_arg(argc, argv);
  }
  else
  {
    printf("please input argment: pstree [OPTION...]");
    return EXIT_FAILURE;
  }

  // 2. switch根据参数调用函数

  return EXIT_SUCCESS;
}
