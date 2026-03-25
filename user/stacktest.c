#include "kernel/types.h"
#include "user/user.h"
#include "kernel/riscv.h"

int main() {
    // 测试1：正常堆分配（应该成功）
    char *p = sbrk(4096);
    p[0] = 'a';
    printf("heap write: %c\n", p[0]);
    
    // 测试2：尝试访问栈保护页
    // 获取当前栈指针
    uint64 sp;
    asm volatile("mv %0, sp" : "=r" (sp));
    printf("current sp: %ld\n", sp);
    
    // 计算保护页地址（栈页下方一页）
    uint64 guard_page = PGROUNDDOWN(sp) - 4096;
    printf("guard page: %ld\n", guard_page);
    
    // 尝试写保护页 - 这应该导致段错误
    char *bad = (char*)guard_page;
    *bad = 'x';  // 应该崩溃！
    
    printf("This should not print\n");
    exit(0);
}