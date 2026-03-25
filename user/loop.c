#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
  for(int i = 0; i < 200; i++){
    printf("diagloop: round %d\n", i);

    char *p = sbrk(8192);
    if((long)p < 0){
      printf("diagloop: sbrk failed at round %d\n", i);
      exit(1);
    }

    p[0] = 'a';
    p[4096] = 'b';

    int pid = fork();
    if(pid < 0){
      printf("diagloop: fork failed at round %d\n", i);
      exit(1);
    }

    if(pid == 0){
      p[0] = 'x';
      p[4096] = 'y';
      exit(0);
    } else {
      wait(0);
    }

    if((long)sbrk(-8192) < 0){
      printf("diagloop: shrink failed at round %d\n", i);
      exit(1);
    }
  }

  printf("diagloop: done\n");
  exit(0);
}