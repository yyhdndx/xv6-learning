#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/sysinfo.h"

static void
fail(const char *msg)
{
  fprintf(2, "sysinfotest: FAIL: %s\n", msg);
  exit(1);
}

int
main(void)
{
  struct sysinfo info_;

  if(info(&info_) < 0)
    fail("sysinfo returned error");

  if(info_.nproc < 1)
    fail("nproc < 1");

  if(info_.free_mem == 0)
    fail("freemem == 0");

  // 触发一次 sbrk，freemem 应该减少（至少不增加）
  uint64 before = info_.free_mem;
  if(sbrk(4096) == (char*)-1)
    fail("sbrk failed");

  if(info(&info_) < 0)
    fail("sysinfo returned error after sbrk");

  if(info_.free_mem > before)
    fail("freemem increased after allocating memory");

  printf("sysinfotest: OK (freemem=%d, nproc=%d)\n",
         (int)info_.free_mem, (int)info_.nproc);
  exit(0);
}