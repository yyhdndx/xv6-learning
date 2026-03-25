#include"kernel/types.h"
#include"kernel/stat.h"
#include"user/user.h"

/*
父子进程之间互相发送，通过使用系统调用
*/
/*
process->fd->file->pipe
fork()复制fd引用，不复制pipe内存
所以每一个进程都拥有对于pipe的两个文件的引用
因此每一个file的引用数为2
如果不close，文件永远无法关闭
*/

int main(int argc,char* argv[]){
    int p2c[2];
    int c2p[2];
    char buf;

    if(pipe(p2c)<0||pipe(c2p)<0){
        fprintf(2,"pipe alloc failed\n");
        exit(1);
    }

    int pid=fork();
    if(pid<0){
        fprintf(2,"fork failed\n");
    }

    if(pid==0){
        // child
        close(p2c[1]);   // 子进程不会写
        close(c2p[0]);   // 子进程不会读
       
        if(read(p2c[0],&buf,1)!=1){
            fprintf(2,"child read failed\n");
            exit(1);
        }
        printf("%d: child received ping\n",getpid());

        if(write(c2p[1],"x",1)!=1){
            fprintf(2,"child write failed\n");
            exit(1);
        }

        close(p2c[0]);
        close(c2p[1]);
        exit(0);
    }else{
        // parent
        close(p2c[0]);  //同上
        close(c2p[1]);

        if(write(p2c[1],"y",1)!=1){
            fprintf(2,"parent write failed\n");
            exit(1);
        }

        if(read(c2p[0],&buf,1)!=1){
            fprintf(2,"parent read failed\n");
            exit(1);
        }
        printf("%d: parent received pong\n",getpid());

        close(p2c[1]);
        close(c2p[0]);
        wait(0);
        exit(0);
    }
}