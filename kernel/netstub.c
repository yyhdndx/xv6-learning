// kernel/netstub.c
#include "types.h"
#include "riscv.h"
#include "defs.h"

volatile int net_rx_count = 0;
volatile int net_rx_last_len = 0;

void
net_rx(char *buf, int len)
{
  net_rx_count++;
  net_rx_last_len = len;
  printf("net_rx: len=%d\n", len);
  kfree(buf);
}