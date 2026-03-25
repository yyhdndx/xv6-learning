#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
  printf("diagspawn: begin\n");

  for(int i = 0; i < 20; i++){
    int pid = fork();
    if(pid < 0){
      printf("diagspawn: fork failed at round %d\n", i);
      exit(1);
    }

    if(pid == 0){
      char *argv[] = { "echo", "spawn-ok", 0 };
      exec("echo", argv);
      printf("diagspawn(child): exec failed at round %d\n", i);
      exit(1);
    } else {
      wait(0);
      printf("diagspawn: round %d ok\n", i);
    }
  }

  printf("diagspawn: done\n");
  exit(0);
}