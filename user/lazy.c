#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

static void
ok(char *msg)
{
  printf("[OK] %s\n", msg);
}

static void
fail(char *msg)
{
  printf("[FAIL] %s\n", msg);
  exit(1);
}

int
main(int argc, char *argv[])
{
  char *oldsz, *p, *q;
  int fd, n;

  printf("lazycheck: start\n");

  //
  // Test 1:
  // sbrk one page; do not touch it immediately.
  // If lazy allocation works, sbrk should succeed without eager allocation.
  //
  oldsz = sbrk(0);
  p = sbrk(4096);
  if(p == (char *)-1)
    fail("sbrk(4096) failed");
  if(p != oldsz)
    fail("sbrk returned unexpected old break");
  ok("sbrk one page");

  //
  // Test 2:
  // first write to the new page should fault in a page and succeed.
  //
  p[0] = 'A';
  p[4095] = 'Z';
  if(p[0] != 'A' || p[4095] != 'Z')
    fail("write/read on lazy page failed");
  ok("first write to lazy page");

  //
  // Test 3:
  // allocate a second page; first read should also be fine after fault-in.
  // reading zero-filled memory is a good signal.
  //
  q = sbrk(4096);
  if(q == (char *)-1)
    fail("second sbrk(4096) failed");

  if(q[0] != 0 || q[123] != 0 || q[4095] != 0)
    fail("new lazy page is not zero-filled");
  ok("first read from lazy page");

  //
  // Test 4:
  // pass a lazy page to read(); kernel copyout should be able to handle it.
  //
  fd = open("README", O_RDONLY);
  if(fd < 0)
    fail("open README failed");

  // allocate one more page, but don't touch it first.
  q = sbrk(4096);
  if(q == (char *)-1)
    fail("third sbrk(4096) failed");

  n = read(fd, q, 16);
  close(fd);
  if(n < 0)
    fail("read into lazy page failed");
  if(n == 0)
    fail("README unexpectedly empty");
  ok("read() into lazy page");

  //
  // Test 5:
  // shrink one page, then touching the released page should kill the child.
  // Put this in a child so the whole test program can continue.
  //
  q = sbrk(0);
  if(sbrk(-4096) == (char *)-1)
    fail("sbrk(-4096) failed");
  ok("shrink one page");

  int pid = fork();
  if(pid < 0)
    fail("fork failed");

  if(pid == 0){
    // q points to the old break before shrink, so q-1 is in the page just removed.
    volatile char x = q[-1];
    printf("unexpected read after shrink: %d\n", x);
    exit(1);
  }

  int st = 0;
  wait(&st);
  if(st == 0)
    fail("child survived access to deallocated page");
  ok("access after shrink kills child");

  //
  // Test 6:
  // access clearly beyond current sbrk range should kill child.
  //
  char *cur = sbrk(0);
  pid = fork();
  if(pid < 0)
    fail("fork failed");

  if(pid == 0){
    volatile char x = cur[4096];   // one page beyond current break
    printf("unexpected out-of-range access: %d\n", x);
    exit(1);
  }

  st = 0;
  wait(&st);
  if(st == 0)
    fail("child survived out-of-range access");
  ok("out-of-range access kills child");

  printf("lazycheck: done\n");
  exit(0);
}