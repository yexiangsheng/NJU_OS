#include <stdio.h>
#include <stdlib.h>

void hanoi(int n, char from, char to, char via)
{
  if (n == 1)
    printf("%c -> %c\n", from, to);
  else {
    hanoi(n - 1, from, via, to);
    hanoi(1, from, to, via);
    hanoi(n - 1, via, to, from);
  }
  return;
}

int main(int argc, char* argv[])
{
  hanoi(atoi(argv[1]), argv[2][0], argv[3][0], argv[4][0]);
  return 0;
}
