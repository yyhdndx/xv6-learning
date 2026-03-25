#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "user/user.h"

static void
mark(const char *s)
{
  printf("== %s ==\n", s);
}

static void
test_heap_one_page(void)
{
  mark("heap_one_page: begin");
  char *p = sbrk(4096);
  if((long)p < 0){
    printf("heap_one_page: sbrk failed\n");
    exit(1);
  }
  p[0] = 'A';
  p[4095] = 'Z';
  printf("heap_one_page: ok %c %c\n", p[0], p[4095]);
  mark("heap_one_page: end");
}

static void
test_heap_two_pages(void)
{
  mark("heap_two_pages: begin");
  char *p = sbrk(8192);
  if((long)p < 0){
    printf("heap_two_pages: sbrk failed\n");
    exit(1);
  }
  p[0] = 'B';
  p[4096] = 'C';
  p[8191] = 'D';
  printf("heap_two_pages: ok %c %c %c\n", p[0], p[4096], p[8191]);
  mark("heap_two_pages: end");
}

static void
test_sparse_faults(void)
{
  mark("sparse_faults: begin");
  char *p = sbrk(10 * 4096);
  if((long)p < 0){
    printf("sparse_faults: sbrk failed\n");
    exit(1);
  }

  p[0] = 'a';
  p[3 * 4096] = 'b';
  p[7 * 4096] = 'c';
  p[9 * 4096 + 100] = 'd';

  printf("sparse_faults: ok %c %c %c %c\n",
         p[0], p[3 * 4096], p[7 * 4096], p[9 * 4096 + 100]);
  mark("sparse_faults: end");
}

static void
test_fork_after_lazy(void)
{
  mark("fork_after_lazy: begin");
  char *p = sbrk(3 * 4096);
  if((long)p < 0){
    printf("fork_after_lazy: sbrk failed\n");
    exit(1);
  }

  p[0] = 'x';
  p[4096] = 'y';
  p[8192] = 'z';

  int pid = fork();
  if(pid < 0){
    printf("fork_after_lazy: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    printf("fork_after_lazy(child): read %c %c %c\n", p[0], p[4096], p[8192]);
    p[0] = 'u';
    p[4096] = 'v';
    p[8192] = 'w';
    printf("fork_after_lazy(child): write %c %c %c\n", p[0], p[4096], p[8192]);
    exit(0);
  } else {
    wait(0);
    printf("fork_after_lazy(parent): still %c %c %c\n", p[0], p[4096], p[8192]);
  }

  mark("fork_after_lazy: end");
}

static void
test_shrink_then_touch(void)
{
  mark("shrink_then_touch: begin");
  char *p = sbrk(2 * 4096);
  if((long)p < 0){
    printf("shrink_then_touch: sbrk grow failed\n");
    exit(1);
  }

  p[0] = 'm';
  p[4096] = 'n';
  printf("shrink_then_touch: before shrink %c %c\n", p[0], p[4096]);

  // shrink one page
  if((long)sbrk(-4096) < 0){
    printf("shrink_then_touch: sbrk shrink failed\n");
    exit(1);
  }

  printf("shrink_then_touch: touch first page\n");
  printf("shrink_then_touch: first=%c\n", p[0]);

  // 下面这一句如果实现正确，通常应该触发异常并杀死进程；
  // 所以把它放到最后，单独判断。
  printf("shrink_then_touch: touching freed page next, crash is expected\n");
  p[4096] = 'q';

  printf("shrink_then_touch: ERROR should not reach here\n");
  mark("shrink_then_touch: end");
}

static void
test_guard_page(void)
{
  mark("guard_page: begin");

  uint64 sp;
  asm volatile("mv %0, sp" : "=r" (sp));
  uint64 guard = PGROUNDDOWN(sp) - 4096;

  printf("guard_page: sp=%ld guard=%ld\n", sp, guard);
  printf("guard_page: crash is expected next\n");

  char *bad = (char*)guard;
  *bad = 'G';

  printf("guard_page: ERROR should not reach here\n");
  mark("guard_page: end");
}

static void
test_exec_after_lazy(void)
{
  mark("exec_after_lazy: begin");

  char *p = sbrk(4 * 4096);
  if((long)p < 0){
    printf("exec_after_lazy: sbrk failed\n");
    exit(1);
  }

  p[0] = '1';
  p[4096] = '2';
  p[8192] = '3';
  p[12288] = '4';

  int pid = fork();
  if(pid < 0){
    printf("exec_after_lazy: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    char *argv[] = { "echo", "exec_after_lazy_child", 0 };
    exec("echo", argv);
    printf("exec_after_lazy(child): exec failed\n");
    exit(1);
  } else {
    wait(0);
    printf("exec_after_lazy(parent): ok\n");
  }

  mark("exec_after_lazy: end");
}

int
main(int argc, char *argv[])
{
  if(argc < 2){
    printf("usage: diaglazy <case>\n");
    printf("cases:\n");
    printf("  heap1\n");
    printf("  heap2\n");
    printf("  sparse\n");
    printf("  fork\n");
    printf("  shrink\n");
    printf("  guard\n");
    printf("  exec\n");
    exit(1);
  }

  if(strcmp(argv[1], "heap1") == 0){
    test_heap_one_page();
  } else if(strcmp(argv[1], "heap2") == 0){
    test_heap_two_pages();
  } else if(strcmp(argv[1], "sparse") == 0){
    test_sparse_faults();
  } else if(strcmp(argv[1], "fork") == 0){
    test_fork_after_lazy();
  } else if(strcmp(argv[1], "shrink") == 0){
    test_shrink_then_touch();
  } else if(strcmp(argv[1], "guard") == 0){
    test_guard_page();
  } else if(strcmp(argv[1], "exec") == 0){
    test_exec_after_lazy();
  } else {
    printf("unknown case: %s\n", argv[1]);
    exit(1);
  }

  printf("diaglazy: done\n");
  exit(0);
}