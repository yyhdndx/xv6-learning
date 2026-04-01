#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "sysinfo.h"
#include "e1000_dev.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

// uint64
// sys_sbrk(void)
// {
//   int n;
//   struct proc* p=myproc();
//   uint64 oldsz,newsz;

//   argint(0,&n);
//   oldsz = p->sz;

//   // expand
//   if(n>=0){
//     if(oldsz+n>=PLIC) return -1;
//     // lazy allocation : only increase p->sz
//     p->sz=oldsz+n;
//     return oldsz;
//   }
//   // shink
//   if(oldsz < (uint64)(-n))
//     return -1;
//   newsz=uvmdealloc(p->pagetable,oldsz,oldsz+n);
//   if(newsz>=oldsz){
//     return -1;
//   }

//   kvmunmap_user_range(p->kpagetable,oldsz,newsz);
//   p->sz=newsz;
//   sfence_vma();

//   return oldsz;
// }

uint64
sys_sbrk(void)
{
  uint64 oldsz;
  int n, t;
  struct proc *p = myproc();

  argint(0, &n);
  argint(1, &t);
  oldsz = p->sz;

  // negative sbrk: if it shrinks too much, make it a no-op.
  if(n < 0){
    if((uint64)(-n) > oldsz){
      return oldsz;
    }
    if(growproc(n) < 0)
      return -1;
    return oldsz;
  }

  if(t == SBRK_EAGER){
    if(growproc(n) < 0)
      return -1;
    return oldsz;
  }

  // lazy grow
  if(oldsz + n < oldsz)
    return -1;
  if(oldsz + n > PLIC)
    return -1;

  p->sz = oldsz + n;
  return oldsz;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  // backtrace();
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// Lab2 sys_trace
// trace the system call
uint64 sys_trace(void){
  int mask;
  argint(0,&mask);
  if(mask<0){
    return -1;
  }
  myproc()->trace_mask=mask;
  return 0;
}

uint64 sys_info(void){
  uint64 uaddr;
  argaddr(0,&uaddr);
  if(uaddr<0){
    return -1;
  }

  struct sysinfo ret;
  ret.free_mem=free_mem();
  ret.nproc=nproc();
  if(copyout(myproc()->pagetable,uaddr,(char*)&ret,sizeof(ret))<0){
    return -1;
  }
  return 0;
}

uint64 sys_sigalarm(void){
  int ticks=0;
  uint64 handler;
  struct proc* p=myproc();

  argint(0,&ticks);
  argaddr(1,&handler);

  p->alarm_interval=ticks;
  p->alarm_handler=handler;
  p->alarm_ticks=0;
  p->alarm_active=0;

  return 0;
}

uint64 sys_sigreturn(void){
  struct proc* p=myproc();
  memmove(p->trapframe,&p->alarm_tf,sizeof(struct trapframe));  // 将alarm_tf给当前的trapframe
  p->alarm_active=0;  // 返回，去掉重入标记
  return 0;
}

uint64
sys_e1000test(void)
{
  static uint32 fake_regs[4096];
  char *buf1, *buf2;
  int r;

  // 1. init smoke test
  memset(fake_regs, 0, sizeof(fake_regs));
  e1000_init(fake_regs);

  // 2. transmit invalid-arg test
  if(e1000_transmit(0, 64) != -1)
    return -1;
  if(e1000_transmit((char*)1, 0) != -1)
    return -1;

  // 3. first valid transmit should succeed
  buf1 = kalloc();
  if(buf1 == 0)
    return -1;
  memset(buf1, 0xab, 64);

  r = e1000_transmit(buf1, 64);
  if(r != 0)
    return -1;

  // 4. second transmit should fail in fake-mmio environment
  //
  // 因为 fake_regs 不会模拟硬件回写 DD：
  // 第一次发送后驱动会把当前 desc.status 置 0，
  // 同时把 TDT 推到下一个位置；但下一个位置初始仍是 DD=1，
  // 所以前几个发送其实还能继续成功，直到 ring 被填满。
  // 因此这里不检查“第二次一定失败”，而是循环把 ring 填满后，
  // 再检查一次失败。
  for(int i = 0; i < TX_RING_SIZE - 1; i++){
    buf2 = kalloc();
    if(buf2 == 0)
      return -1;
    memset(buf2, 0xcd, 64);
    if(e1000_transmit(buf2, 64) != 0)
      return -1;
  }

  buf2 = kalloc();
  if(buf2 == 0)
    return -1;
  memset(buf2, 0xef, 64);

  if(e1000_transmit(buf2, 64) != -1)
    return -1;

  // 5. receive/intr smoke test: should not panic
  e1000_receive();
  // printf("???\n");
  // e1000_intr();

  return 0;
}