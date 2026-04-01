// user/e1000test.c
#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  int r = e1000test();
  printf("e1000test returned %d\n", r);
  exit(0);
}