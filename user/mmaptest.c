#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define PGSZ 4096

#define CHECK(cond, msg) \
  do { \
    if(!(cond)){ \
      printf("FAIL: %s\n", msg); \
      exit(1); \
    } \
  } while(0)

static char gbuf1[PGSZ * 3];
static char gbuf2[PGSZ * 3];
static char gbuf3[PGSZ * 3];
static char gbuf4[PGSZ * 3];

static void
fillbuf(char *buf, int n, char base)
{
  for(int i = 0; i < n; i++)
    buf[i] = base + (i % 23);
}

static void
clearbuf(char *buf, int n)
{
  for(int i = 0; i < n; i++)
    buf[i] = 0;
}

static void
writefile(char *name, char *buf, int n)
{
  int fd = open(name, O_CREATE | O_TRUNC | O_RDWR);
  CHECK(fd >= 0, "writefile open");
  CHECK(write(fd, buf, n) == n, "writefile write");
  close(fd);
}

static void
readfile(char *name, char *buf, int n)
{
  int fd = open(name, O_RDONLY);
  CHECK(fd >= 0, "readfile open");
  CHECK(read(fd, buf, n) == n, "readfile read");
  close(fd);
}

static void
check_equal(char *a, char *b, int n, char *msg)
{
  for(int i = 0; i < n; i++){
    if(a[i] != b[i]){
      printf("FAIL: %s at %d got %d expect %d\n", msg, i, a[i], b[i]);
      exit(1);
    }
  }
}

static void
test_three_page_sparse_fault(void)
{
  printf("test_three_page_sparse_fault...\n");

  fillbuf(gbuf1, PGSZ * 3, 'a');
  writefile("mm2_a", gbuf1, PGSZ * 3);

  int fd = open("mm2_a", O_RDONLY);
  CHECK(fd >= 0, "open mm2_a");

  char *p = mmap(0, PGSZ * 3, PROT_READ, MAP_PRIVATE, fd, 0);
  CHECK((long)p >= 0, "mmap sparse");

  // only touch middle page first
  CHECK(p[PGSZ] == gbuf1[PGSZ], "middle page read");
  CHECK(p[PGSZ + 123] == gbuf1[PGSZ + 123], "middle page read 2");

  // then first page
  CHECK(p[0] == gbuf1[0], "first page read");

  // then third page
  CHECK(p[PGSZ * 2 + 17] == gbuf1[PGSZ * 2 + 17], "third page read");

  CHECK(munmap(p, PGSZ * 3) == 0, "munmap sparse");
  close(fd);

  printf("test_three_page_sparse_fault: OK\n");
}

static void
test_shared_partial_unmap_reverse(void)
{
  printf("test_shared_partial_unmap_reverse...\n");

  fillbuf(gbuf1, PGSZ * 3, 'b');
  writefile("mm2_b", gbuf1, PGSZ * 3);

  int fd = open("mm2_b", O_RDWR);
  CHECK(fd >= 0, "open mm2_b");

  char *p = mmap(0, PGSZ * 3, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  CHECK((long)p >= 0, "mmap shared reverse");

  p[0] = 'X';
  p[PGSZ + 5] = 'Y';
  p[PGSZ * 2 + 9] = 'Z';

  // first unmap tail page
  CHECK(munmap(p + PGSZ * 2, PGSZ) == 0, "munmap tail page");

  // remaining pages still accessible
  CHECK(p[0] == 'X', "page0 after tail unmap");
  CHECK(p[PGSZ + 5] == 'Y', "page1 after tail unmap");

  // then unmap head page
  CHECK(munmap(p, PGSZ) == 0, "munmap head page");

  // remaining middle page still accessible
  CHECK(p[PGSZ + 5] == 'Y', "middle page after head unmap");

  // finally unmap middle page
  CHECK(munmap(p + PGSZ, PGSZ) == 0, "munmap middle final");

  close(fd);

  clearbuf(gbuf2, PGSZ * 3);
  readfile("mm2_b", gbuf2, PGSZ * 3);

  CHECK(gbuf2[0] == 'X', "writeback page0");
  CHECK(gbuf2[PGSZ + 5] == 'Y', "writeback page1");
  CHECK(gbuf2[PGSZ * 2 + 9] == 'Z', "writeback page2");

  printf("test_shared_partial_unmap_reverse: OK\n");
}

static void
test_private_fork_isolation(void)
{
  printf("test_private_fork_isolation...\n");

  fillbuf(gbuf1, PGSZ * 2, 'c');
  writefile("mm2_c", gbuf1, PGSZ * 2);

  int fd = open("mm2_c", O_RDWR);
  CHECK(fd >= 0, "open mm2_c");

  char *p = mmap(0, PGSZ * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  CHECK((long)p >= 0, "mmap private fork isolation");

  int pid = fork();
  CHECK(pid >= 0, "fork private isolation");

  if(pid == 0){
    p[0] = 'Q';
    p[PGSZ + 10] = 'R';
    CHECK(p[0] == 'Q', "child private self read");
    CHECK(p[PGSZ + 10] == 'R', "child private self read2");
    munmap(p, PGSZ * 2);
    close(fd);
    exit(0);
  } else {
    int st = 0;
    wait(&st);
    CHECK(st == 0, "child exit status");

    // parent should still see original data
    CHECK(p[0] == gbuf1[0], "parent unchanged page0");
    CHECK(p[PGSZ + 10] == gbuf1[PGSZ + 10], "parent unchanged page1");

    CHECK(munmap(p, PGSZ * 2) == 0, "parent munmap private");
    close(fd);

    clearbuf(gbuf2, PGSZ * 2);
    readfile("mm2_c", gbuf2, PGSZ * 2);
    check_equal(gbuf1, gbuf2, PGSZ * 2, "private fork leaked to file");
  }

  printf("test_private_fork_isolation: OK\n");
}

static void
test_shared_fork_visibility(void)
{
  printf("test_shared_fork_visibility...\n");

  fillbuf(gbuf1, PGSZ * 2, 'd');
  writefile("mm2_d", gbuf1, PGSZ * 2);

  int fd = open("mm2_d", O_RDWR);
  CHECK(fd >= 0, "open mm2_d");

  char *p = mmap(0, PGSZ * 2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  CHECK((long)p >= 0, "mmap shared fork");

  int pid = fork();
  CHECK(pid >= 0, "fork shared");

  if(pid == 0){
    p[1] = 'K';
    p[PGSZ + 20] = 'L';
    CHECK(munmap(p, PGSZ * 2) == 0, "child munmap shared");
    close(fd);
    exit(0);
  } else {
    int st = 0;
    wait(&st);
    CHECK(st == 0, "child shared status");

    // parent may still see its own copied page contents depending on implementation,
    // but after parent munmap + file read, file must contain child's writes.
    CHECK(munmap(p, PGSZ * 2) == 0, "parent munmap shared");
    close(fd);

    clearbuf(gbuf2, PGSZ * 2);
    readfile("mm2_d", gbuf2, PGSZ * 2);
    CHECK(gbuf2[1] == 'K', "shared fork file byte1");
    CHECK(gbuf2[PGSZ + 20] == 'L', "shared fork file byte2");
  }

  printf("test_shared_fork_visibility: OK\n");
}

static void
test_many_small_mmaps(void)
{
  printf("test_many_small_mmaps...\n");

  fillbuf(gbuf1, PGSZ, 'e');
  writefile("mm2_e", gbuf1, PGSZ);

  int fds[8];
  char *ps[8];

  for(int i = 0; i < 8; i++){
    fds[i] = open("mm2_e", O_RDONLY);
    CHECK(fds[i] >= 0, "open many small");
    ps[i] = mmap(0, PGSZ, PROT_READ, MAP_PRIVATE, fds[i], 0);
    CHECK((long)ps[i] >= 0, "mmap many small");
    CHECK(ps[i][i] == gbuf1[i], "touch many small");
  }

  for(int i = 7; i >= 0; i--){
    CHECK(munmap(ps[i], PGSZ) == 0, "munmap many small");
    close(fds[i]);
  }

  printf("test_many_small_mmaps: OK\n");
}

static void
test_interleaved_files_shared_private(void)
{
  printf("test_interleaved_files_shared_private...\n");

  fillbuf(gbuf1, PGSZ * 2, 'f');
  fillbuf(gbuf2, PGSZ * 2, 'G');
  writefile("mm2_f1", gbuf1, PGSZ * 2);
  writefile("mm2_f2", gbuf2, PGSZ * 2);

  int fd1 = open("mm2_f1", O_RDWR);
  int fd2 = open("mm2_f2", O_RDWR);
  CHECK(fd1 >= 0, "open f1");
  CHECK(fd2 >= 0, "open f2");

  char *p1 = mmap(0, PGSZ * 2, PROT_READ | PROT_WRITE, MAP_SHARED, fd1, 0);
  char *p2 = mmap(0, PGSZ * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd2, 0);
  CHECK((long)p1 >= 0, "mmap f1");
  CHECK((long)p2 >= 0, "mmap f2");

  p1[10] = 'S';
  p1[PGSZ + 11] = 'T';

  p2[10] = 'U';
  p2[PGSZ + 11] = 'V';

  CHECK(munmap(p2, PGSZ * 2) == 0, "munmap private f2");
  CHECK(munmap(p1, PGSZ * 2) == 0, "munmap shared f1");

  close(fd1);
  close(fd2);

  clearbuf(gbuf3, PGSZ * 2);
  clearbuf(gbuf4, PGSZ * 2);
  readfile("mm2_f1", gbuf3, PGSZ * 2);
  readfile("mm2_f2", gbuf4, PGSZ * 2);

  CHECK(gbuf3[10] == 'S', "shared persisted 1");
  CHECK(gbuf3[PGSZ + 11] == 'T', "shared persisted 2");

  CHECK(gbuf4[10] == gbuf2[10], "private not persisted 1");
  CHECK(gbuf4[PGSZ + 11] == gbuf2[PGSZ + 11], "private not persisted 2");

  printf("test_interleaved_files_shared_private: OK\n");
}

static void
test_repeated_map_unmap_cycles(void)
{
  printf("test_repeated_map_unmap_cycles...\n");

  fillbuf(gbuf1, PGSZ, 'h');
  writefile("mm2_g", gbuf1, PGSZ);

  for(int round = 0; round < 20; round++){
    int fd = open("mm2_g", O_RDWR);
    CHECK(fd >= 0, "open repeated cycle");

    char *p = mmap(0, PGSZ, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    CHECK((long)p >= 0, "mmap repeated cycle");

    CHECK(p[0] == gbuf1[0] || round > 0, "read repeated cycle");
    p[round % 64] = 'a' + (round % 26);

    CHECK(munmap(p, PGSZ) == 0, "munmap repeated cycle");
    close(fd);
  }

  printf("test_repeated_map_unmap_cycles: OK\n");
}

int
main(int argc, char *argv[])
{
  printf("mmaptest2 main called\n");

  test_three_page_sparse_fault();
  test_shared_partial_unmap_reverse();
  test_private_fork_isolation();
  test_shared_fork_visibility();
  test_many_small_mmaps();
  test_interleaved_files_shared_private();
  test_repeated_map_unmap_cycles();

  printf("mmaptest2: ALL OK\n");
  exit(0);
}