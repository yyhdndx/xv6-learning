#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
  printf("diagnowrite: begin\n");

  char *p = (char *)main;
  printf("diagnowrite: main addr = %p\n", p);
  printf("diagnowrite: about to write text page, crash is expected\n");

  *p = 1;

  printf("diagnowrite: ERROR should not reach here\n");
  exit(1);
}