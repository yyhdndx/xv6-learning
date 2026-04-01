// kernel/e1000.c

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"

#define RX_PKT_BUF_SZ PGSIZE

// E1000 MMIO 寄存器基地址
static volatile uint32 *regs=0;

static struct spinlock e1000_lock;

// TX : 网卡发送队列
// RX : 网卡接受队列
// 16字节对齐
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));

// 当前的TX desc挂在的是哪一个buffer
static char *tx_bufs[TX_RING_SIZE];

// 当前的RX desc给DMA的buffer
static char *rx_bufs[RX_RING_SIZE];

// 上层网络以太帧入口
extern void net_rx(char *buf,int len);

// 写寄存器
static inline void
e1000_write(uint32 reg, uint32 val)
{
  regs[reg] = val;
}

// 读寄存器
static inline uint32
e1000_read(uint32 reg)
{
  return regs[reg];
}

// initial TX ring
static void e1000_init_tx(void) {
  // 清空软件状态
  for(int i = 0; i < TX_RING_SIZE; i++){
    memset(&tx_ring[i], 0, sizeof(tx_ring[i]));
    tx_bufs[i] = 0;

    // 初始时 ring 上所有 TX descriptor 都是“空闲可用”的
    // 把 DD 置位，表示软件可以立刻使用这些槽位
    tx_ring[i].status = E1000_TXD_STAT_DD;
  }

  // 告诉硬件 TX descriptor ring 在哪里
  e1000_write(E1000_TDBAL, (uint64)tx_ring);
  e1000_write(E1000_TDBAH, 0);

  // ring 总字节数
  e1000_write(E1000_TDLEN, sizeof(tx_ring));

  // 初始 head/tail 都设为 0
  e1000_write(E1000_TDH, 0);
  e1000_write(E1000_TDT, 0);

  // 配置发送控制寄存器 TCTL
  e1000_write(E1000_TCTL,
              E1000_TCTL_EN |
              E1000_TCTL_PSP |
              (0x10 << E1000_CT_SHIFT) |
              (0x40 << E1000_COLD_SHIFT));

  // 配置 TIPG：包间隙
  e1000_write(E1000_TIPG, 10 | (8 << 10) | (6 << 20));
}

// initial RX ring
static void e1000_init_rx(void) {
  for(int i = 0; i < RX_RING_SIZE; i++){
    memset(&rx_ring[i], 0, sizeof(rx_ring[i]));

    // 给每个 RX descriptor 准备一块 buffer，供硬件 DMA 写入收到的包。
    rx_bufs[i] = kalloc();
    if(rx_bufs[i] == 0)
      panic("e1000_init_rx: kalloc failed");

    rx_ring[i].addr = (uint64)rx_bufs[i];
    rx_ring[i].status = 0;
  }

  // 告诉硬件 RX descriptor ring 在哪里
  e1000_write(E1000_RDBAL, (uint64)rx_ring);
  e1000_write(E1000_RDBAH, 0);

  // ring 总字节数
  e1000_write(E1000_RDLEN, sizeof(rx_ring));

  // RX head 从 0 开始
  e1000_write(E1000_RDH, 0);

  // RX tail 设为最后一个 descriptor。
  // 这表示 [0 .. RX_RING_SIZE-1] 这些槽位都已经准备好，可以被硬件使用。
  e1000_write(E1000_RDT, RX_RING_SIZE - 1);

  // 设置接收控制寄存器 RCTL。
  // EN   : enable receiver
  // BAM  : 接收广播包
  // SECRC: strip ethernet CRC，去掉尾部 CRC
  // SZ_2048: buffer 大小设成 2048（若你的头文件支持）
  uint32 rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC;

#ifdef E1000_RCTL_SZ_2048
  rctl |= E1000_RCTL_SZ_2048;
#endif

  e1000_write(E1000_RCTL, rctl);

  // 开中断：至少打开“接收描述符写回”和“接收定时器”一类中断。
  // 不同 skeleton 头文件里名字可能略有不同。
#ifdef E1000_IMS_RXT0
  e1000_write(E1000_IMS, E1000_IMS_RXT0);
#elif defined(E1000_IMS_RXDW)
  e1000_write(E1000_IMS, E1000_IMS_RXDW);
#endif
}

// 中断
void e1000_intr(void) {
  if(regs == 0)
    return;

  // 读 ICR 一般既是取中断原因，也是 acknowledge。
#ifdef E1000_ICR
  e1000_read(E1000_ICR);
#endif

  e1000_receive();
}

void e1000_init(volatile uint32 *base) {
  regs = base;
  if(regs == 0)
    panic("e1000_init: null mmio base");

  initlock(&e1000_lock, "e1000");

#ifdef E1000_CTL_RST
  e1000_write(E1000_CTL, e1000_read(E1000_CTL) | E1000_CTL_RST);
#endif

  e1000_init_tx();
  e1000_init_rx();
}

// 发送帧 packet
// 注意这里只是将包缓存歇息写到一个发送描述符内部，然后通知网卡硬件从DMA读入并发送
// TX是一个环形的任务队列，每一个位置存储一个描述符
int e1000_transmit(char *buf,int len){
  if(regs==0||buf==0||len<=0){
    return -1;
  }
  acquire(&e1000_lock);
  // TDT是指向下一个软件需要填写的描述符的位置
  uint32 idx=e1000_read(E1000_TDT);

  // DD : descripter done 表示当前这个硬件处理是否完成
  // 如果发现最后的位置都没有完成，说明是真的没有空位置了
  if((tx_ring[idx].status & E1000_TXD_STAT_DD) == 0){
    release(&e1000_lock);
    return -1;
  }

  // 如果DD=1，说明这里已经处理过了，直接回收
  if(tx_bufs[idx]!=0){
    kfree(tx_bufs[idx]);
    tx_bufs[idx]=0;
  }

  // 填写当前的descripter
  tx_ring[idx].addr=(uint64)buf;
  tx_ring[idx].length=len;

  // EOP: 这个 descriptor 就是整个 packet 的结束 
  // RS : 发送完成后要求硬件回写 DD，方便软件回收 
  tx_ring[idx].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;
  tx_ring[idx].status=0;
  // 这里我们在软件层面上需要手动管理bufs的内存的释放问题，硬件肯定是不管的
  // 记录buf，然后等下次这个位置被调用的时候做kfree
  // 那为什么不能说我们直接在写descripter之后就来kfree()呢？
  // 显然这是一个异步的问题，肯定是得等硬件完成转发之后咱们才能kfree()
  tx_bufs[idx]=buf;

  // 通知硬件提交动作前推
  // 有新的任务
  e1000_write(E1000_TDT,(idx+1)%TX_RING_SIZE);

  release(&e1000_lock);
  return 0;
}

void e1000_receive(void){
  if(regs==0){
    return ;
  }

  acquire(&e1000_lock);
  for(;;){
    // RDT指向最后一个已经还给硬件的descripter
    // 所以我们通过找下一位来检查新包
    uint32 idx=(e1000_read(E1000_RDT)+1)%RX_RING_SIZE;
    if((rx_ring[idx].status & E1000_RXD_STAT_DD)==0){
      // 没有新的包
      break ;
    }

    char *buf=rx_bufs[idx];
    int len=rx_ring[idx].length;

    char *new_buffer=kalloc();
    if(new_buffer==0){
      panic("e1000_recv : kalloc() failed");
    }

    rx_bufs[idx]=new_buffer;
    rx_ring[idx].addr=(uint64)new_buffer;
    rx_ring[idx].status=0;

    // 把descripter还给硬件
    e1000_write(E1000_RDT,idx);

    release(&e1000_lock);
    // printf("e1000_receive: enter\n");
    // printf("e1000_receive: idx=%d status=%d\n", idx, rx_ring[idx].status);
    net_rx(buf,len);
    acquire(&e1000_lock);
  }

  release(&e1000_lock);
}
