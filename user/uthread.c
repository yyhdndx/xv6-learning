/*
考虑一下你实现的是什么
我们只是简单写了一下用户态线程的线程库，本质上是运行在一个CPU上面的多个线程
不存在并行，只是纯粹的栈切换
也完全不需要进行usertrap陷入内核，完全运行在用户态

切换 != 调度
调度是需要陷入内核，由内核线程来完成调度的
*/

#include "kernel/types.h"
#include "user/user.h"


#define MAX_THREAD  4
#define STACK_SIZE  4096    // 4KB

enum thread_state {
    FREE,
    RUNNABLE,
    RUNNING,
    BLOCKED,
    ZOMBIE,
};

struct context{
    uint64 ra;  // 控制流PC
    uint64 sp;  // 栈指针

    uint64 s0;
    uint64 s1;
    uint64 s2;
    uint64 s3;
    uint64 s4;
    uint64 s5;
    uint64 s6;
    uint64 s7;
    uint64 s8;
    uint64 s9;
    uint64 s10;
    uint64 s11;
};

struct thread{
    int tid;
    char stack[STACK_SIZE];
    enum thread_state state;
    struct context context;

    void (*func)(void*);    // 线程入口
    void *arg;

    struct thread *joiner;
};

struct thread all_thread[MAX_THREAD];
struct thread* cur_thread=0;
int nxt_tid=1;  // 0号是main线程

extern void thread_switch(struct context *old, struct context *new);    // 线程切换

int thread_self(void){
    return cur_thread->tid;
}

// 线程入口
void thread_stub(void){
    if(cur_thread==0||cur_thread->func==0){
        printf("panic: invalid current thread\n");
        exit(1);
    }
    cur_thread->func(cur_thread->arg);
    thread_exit();
    exit(1);
}

void thread_schedule(void){
    int i,start,cur_idx;
    struct thread* nxt=0;
    struct thread* prev=cur_thread;

    if(prev==0){
        start=0;
    }else{
        cur_idx=prev-all_thread;
        start=(cur_idx+1)%MAX_THREAD;
    }

    // 先找别人
    for(i=0;i<MAX_THREAD;i++){
        int idx=(start+i)%MAX_THREAD;
        if(&all_thread[idx]!=prev&&all_thread[idx].state==RUNNABLE){
            nxt=&all_thread[idx];
            break;
        }
    }

    // 如果没有可以运行的线程，接着运行自己
    if(nxt==0&&prev&&prev->state==RUNNABLE){
        nxt=prev;
    }

    if(nxt==0){
        exit(0);
    }

    cur_thread=nxt;
    nxt->state=RUNNABLE;

    if (prev==0) {
        struct context dummy;
        memset(&dummy, 0, sizeof(dummy));
        thread_switch(&dummy, &nxt->context);
        return;
    }

    if(prev!=nxt){
        thread_switch(&prev->context,&nxt->context);
    }
}

void thread_yield(void){
    cur_thread->state=RUNNABLE;
    thread_schedule();
}

void thread_exit(void){
    struct thread *t=cur_thread;
    t->state=ZOMBIE;
    // 当前的线程停止运行了，如果有joiner的话就去唤醒
    // 其实这里是不需要锁的，因为我们这里不是并行的，每一段时间只有一个线程在运行
    if(t->joiner&&t->joiner->state==BLOCKED){
        t->joiner->state=RUNNABLE;
    }
    thread_schedule();
    exit(0);
}

int thread_create(void(*func)(void*), void *arg){
    struct thread* t;

    for(t=all_thread;t<all_thread+MAX_THREAD;t++){
        if(t->state==FREE){
            break;
        }
    }

    if(t==all_thread+MAX_THREAD){
        printf("panic: no free thread\n");
        return -1;
    }

    memset(t,0,sizeof(*t));

    t->tid=nxt_tid++;
    t->state=RUNNABLE;
    t->func=func;
    t->arg=arg;
    t->joiner=0;

    uint64 sp=(uint64)(t->stack+STACK_SIZE);
    sp&=~0XFUL;

    t->context.sp=sp;
    t->context.ra=(uint64)thread_stub;

    return t->tid;
}

// join指定tid的线程
int thread_join(int tid){
    struct thread *target=0;
    struct thread *t;

    if(cur_thread==0){
        return -1;
    }
    if(tid==cur_thread->tid){
        return -1;
    }

    for(t=all_thread;t<all_thread+MAX_THREAD;t++){
        if(t->state!=FREE&&t->tid==tid){
            target=t;
            break;
        }
    }

    if(target==0){
        return -1;
    }

    // 这里我们假设只支持一个joiner
    // 检查joiner是不是自己
    // 如果不是的话就跳出
    if(target->joiner&&target->joiner!=cur_thread){
        return -1;
    }

    if (target->state != ZOMBIE) {
        target->joiner = cur_thread;
        cur_thread->state = BLOCKED;
        thread_schedule();
    }

    // 回收
    // exit是变成ZOMBIE，join负责收尸变成FREE
    memset(&target->context,0,sizeof(target->context));
    target->func=0;
    target->arg=0;
    target->joiner=0;
    target->state=FREE;
    target->tid=0;

    return 0;
}
