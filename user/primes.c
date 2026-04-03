// #pragma GCC diagnostic ignored "-Winfinite-recursion"
#include"kernel/types.h"
#include"kernel/stat.h"
#include"user/user.h"
/*
和欧拉筛是一个道理，不过这里是递归处理了
每一级先选出一个质数p，然后这个p的倍数全部划分为合数
不过递归是单独开了一个进程
*/

static void run_filter(int in_fd){  // in_fd为上一级的pfd[0]
    int p,n;

    // 读入第一个数作为本级prime
    // 递归出口，如果没有数据就退出
    // 同时这里也是关闭上级的pfd[0]
    if(read(in_fd,&p,sizeof(int))!=sizeof(int)){
        close(in_fd);
        exit(0);
    }

    printf("prime %d\n",p);

    int pfd[2];
    if(pipe(pfd)<0){
        fprintf(2,"pipe alloc failed\n");
        close(in_fd);
        exit(1);
    }

    int pid=fork();
    if(pid<0){
        fprintf(2,"fork failed\n");
        close(in_fd);
        close(pfd[0]);
        close(pfd[1]);
        exit(1);
    }

    if(pid==0){
        // child : next filter , reads from pfd[0]
        close(pfd[1]);
        close(in_fd);   // 同样的，文件的句柄也会在父子进程中被留存下来，都需要关闭
        run_filter(pfd[0]);
        // NO return 
    }else{
        // parent : current filter , writes to pfd[1]
        close(pfd[0]);
        while(read(in_fd,&n,sizeof(int))==sizeof(int)){
            if(n%p!=0){
                if(write(pfd[1],&n,sizeof(int))!=sizeof(int)){
                    fprintf(2,"parent write failed\n");
                    break;
                }
            }
        }

        close(in_fd);
        close(pfd[1]);
        wait(0);
        exit(0);
    }
}

int main(int argc,char* argv[]){
    int pfd[2];
    if(pipe(pfd)<0){
        fprintf(2,"pipe alloc failed\n");
        exit(1);
    }
    int pid=fork();
    if(pid<0){
        fprintf(2,"fork failed\n");
        close(pfd[0]);
        close(pfd[1]);
        exit(1);
    }

    if(pid==0){
        // child : first filter
        close(pfd[1]);
        run_filter(pfd[0]);
    }else{
        // parent : generator
        close(pfd[0]);
        for(int i=2;i<=35;i++){
            if(write(pfd[1],&i,sizeof(int))!=sizeof(int)){
                fprintf(2,"write failed\n");
                break;
            }
        }
        close(pfd[1]);  // EOF
        wait(0);
        exit(0);
    }
    return 0;
}
