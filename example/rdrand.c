#include <stdint.h>
#include <stdio.h>

int main()
{
  uint64_t val;
  asm volatile("rdrand %0"
               : "=r"(val));
  printf("%016lx\n", val);
}
