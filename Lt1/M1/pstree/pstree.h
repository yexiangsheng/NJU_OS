#ifndef __PSTREE_H__
#define __PSTREE_H__

typedef struct
{
  int pid;
  int ppid;
  char name[256];
} proc_info;

void print_version(void);
int parse_arg(int argc, char *argv[]);

#endif
