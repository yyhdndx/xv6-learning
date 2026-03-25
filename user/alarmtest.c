#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

volatile int alarm_count = 0;
volatile int in_handler = 0;

void
periodic(void)
{
  alarm_count++;
  printf("alarm! count=%d\n", alarm_count);
  sigreturn();
}

void
slow_handler(void)
{
  if(in_handler){
    printf("REENTERED HANDLER!\n");
    exit(1);
  }

  in_handler = 1;
  printf("slow handler start\n");

  // 故意多跑一会儿，看看会不会重入
  for(volatile int i = 0; i < 200000000; i++){
    ;
  }

  printf("slow handler end\n");
  in_handler = 0;
  sigreturn();
}

void
busy_loop(int n)
{
  volatile int x = 0;
  for(int i = 0; i < n; i++){
    x += i;
  }
}

void
test0(void)
{
  printf("test0 start\n");
  alarm_count = 0;

  sigalarm(2, periodic);

  // 持续占用 CPU，给 timer interrupt 机会触发
  busy_loop(200000000);

  sigalarm(0, 0);

  if(alarm_count > 0){
    printf("test0 passed: alarm_count=%d\n", alarm_count);
  } else {
    printf("test0 failed: alarm not triggered\n");
  }
}

void
test1(void)
{
  printf("test1 start\n");
  alarm_count = 0;

  sigalarm(2, periodic);

  int sum = 0;
  for(int i = 0; i < 1000000000; i++){
    sum += (i & 1);
  }

  sigalarm(0, 0);

  // 这里只要程序能正常跑完，且 alarm 触发过，说明基本恢复正常
  if(alarm_count > 0){
    printf("test1 passed: sum=%d alarm_count=%d\n", sum, alarm_count);
  } else {
    printf("test1 failed: no alarm\n");
  }
}

void
test2(void)
{
  printf("test2 start\n");
  alarm_count = 0;
  in_handler = 0;

  sigalarm(2, slow_handler);

  busy_loop(300000000);

  sigalarm(0, 0);

  if(in_handler){
    printf("test2 failed: handler state corrupted\n");
  } else {
    printf("test2 done\n");
  }
}

int
main(int argc, char *argv[])
{
  test0();
  test1();
  test2();
  exit(0);
}