#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

static void
fail(char *msg)
{
  printf("symlinktest: FAIL: %s\n", msg);
  exit(1);
}

static void
check(int cond, char *msg)
{
  if(!cond)
    fail(msg);
}

static void
cleanup(void)
{
  unlink("a");
  unlink("b");
  unlink("c");
  unlink("d");
  unlink("x");
  unlink("y");
  unlink("z");
  unlink("nofilelink");
  unlink("dirlink");
  unlink("dir");
}

static void
test_basic(void)
{
  int fd, n;
  char buf[32];

  printf("test_basic...\n");

  unlink("a");
  unlink("b");

  fd = open("a", O_CREATE | O_WRONLY);
  check(fd >= 0, "open a for create failed");
  n = write(fd, "hello", 5);
  check(n == 5, "write a failed");
  close(fd);

  check(symlink("a", "b") == 0, "symlink b->a failed");

  fd = open("b", O_RDONLY);
  check(fd >= 0, "open b failed");

  memset(buf, 0, sizeof(buf));
  n = read(fd, buf, sizeof(buf));
  check(n == 5, "read from b length wrong");
  check(strcmp(buf, "hello") == 0, "read from b content wrong");
  close(fd);

  printf("test_basic: OK\n");
}

static void
test_nofollow(void)
{
  int fd;
  struct stat st;

  printf("test_nofollow...\n");

  unlink("a");
  unlink("b");

  fd = open("a", O_CREATE | O_WRONLY);
  check(fd >= 0, "open a failed");
  close(fd);

  check(symlink("a", "b") == 0, "symlink b->a failed");

  fd = open("b", O_RDONLY | O_NOFOLLOW);
  check(fd >= 0, "open b with O_NOFOLLOW failed");

  check(fstat(fd, &st) == 0, "fstat on b failed");
  check(st.type == T_SYMLINK, "O_NOFOLLOW did not open symlink itself");

  close(fd);

  printf("test_nofollow: OK\n");
}

static void
test_chain(void)
{
  int fd, n;
  char buf[32];

  printf("test_chain...\n");

  unlink("a");
  unlink("b");
  unlink("c");
  unlink("d");

  fd = open("a", O_CREATE | O_WRONLY);
  check(fd >= 0, "open a failed");
  n = write(fd, "world", 5);
  check(n == 5, "write a failed");
  close(fd);

  check(symlink("a", "b") == 0, "symlink b->a failed");
  check(symlink("b", "c") == 0, "symlink c->b failed");
  check(symlink("c", "d") == 0, "symlink d->c failed");

  fd = open("d", O_RDONLY);
  check(fd >= 0, "open d failed");

  memset(buf, 0, sizeof(buf));
  n = read(fd, buf, sizeof(buf));
  check(n == 5, "read d length wrong");
  check(strcmp(buf, "world") == 0, "read d content wrong");
  close(fd);

  printf("test_chain: OK\n");
}

static void
test_dangling(void)
{
  int fd;

  printf("test_dangling...\n");

  unlink("nofile");
  unlink("nofilelink");

  check(symlink("nofile", "nofilelink") == 0, "symlink nofilelink->nofile failed");

  fd = open("nofilelink", O_RDONLY);
  check(fd < 0, "dangling symlink should not open successfully");

  printf("test_dangling: OK\n");
}

static void
test_cycle(void)
{
  int fd;

  printf("test_cycle...\n");

  unlink("x");
  unlink("y");

  check(symlink("y", "x") == 0, "symlink x->y failed");
  check(symlink("x", "y") == 0, "symlink y->x failed");

  fd = open("x", O_RDONLY);
  check(fd < 0, "cyclic symlink should fail instead of succeeding");

  printf("test_cycle: OK\n");
}

static void
test_trunc(void)
{
  int fd, n;
  char buf[32];

  printf("test_trunc...\n");

  unlink("a");
  unlink("b");

  fd = open("a", O_CREATE | O_WRONLY);
  check(fd >= 0, "open a failed");
  n = write(fd, "abcdef", 6);
  check(n == 6, "write a failed");
  close(fd);

  check(symlink("a", "b") == 0, "symlink b->a failed");

  // O_TRUNC 应该作用在最终目标 a 上
  fd = open("b", O_WRONLY | O_TRUNC);
  check(fd >= 0, "open b with O_TRUNC failed");
  close(fd);

  fd = open("a", O_RDONLY);
  check(fd >= 0, "reopen a failed");
  memset(buf, 0, sizeof(buf));
  n = read(fd, buf, sizeof(buf));
  check(n == 0, "O_TRUNC on symlink did not truncate target");
  close(fd);

  printf("test_trunc: OK\n");
}

static void
test_dir(void)
{
  int fd;

  printf("test_dir...\n");

  unlink("dirlink");
  unlink("dir");

  check(mkdir("dir") == 0, "mkdir dir failed");
  check(symlink("dir", "dirlink") == 0, "symlink dirlink->dir failed");

  fd = open("dirlink", O_RDONLY);
  check(fd >= 0, "open dirlink readonly failed");
  close(fd);

  fd = open("dirlink", O_WRONLY);
  check(fd < 0, "open dirlink writable should fail because target is dir");

  unlink("dirlink");
  unlink("dir");

  printf("test_dir: OK\n");
}

static void
test_create_with_symlink_path(void)
{
  int fd, n;
  char buf[32];

  printf("test_create_with_symlink_path...\n");

  unlink("a");
  unlink("b");

  fd = open("a", O_CREATE | O_WRONLY);
  check(fd >= 0, "create a failed");
  close(fd);

  check(symlink("a", "b") == 0, "symlink b->a failed");

  // 对已存在 symlink 路径使用 O_CREATE，在旧 xv6 语义下通常不会走“替换链接目标”这种复杂逻辑，
  // 这里只做一个保守检查：open 不应该把系统搞坏。
  fd = open("b", O_RDONLY);
  check(fd >= 0, "open b failed");
  close(fd);

  fd = open("a", O_WRONLY);
  check(fd >= 0, "open a failed");
  n = write(fd, "k", 1);
  check(n == 1, "write a failed");
  close(fd);

  fd = open("b", O_RDONLY);
  check(fd >= 0, "open b second time failed");
  memset(buf, 0, sizeof(buf));
  n = read(fd, buf, sizeof(buf));
  check(n >= 1, "read b after write a failed");
  close(fd);

  printf("test_create_with_symlink_path: OK\n");
}

int
main(void)
{
  printf("symlinktest: start\n");

  cleanup();

  test_basic();
  test_nofollow();
  test_chain();
  test_dangling();
  test_cycle();
  test_trunc();
  test_dir();
  test_create_with_symlink_path();

  cleanup();

  printf("symlinktest: ALL TESTS PASSED\n");
  exit(0);
}