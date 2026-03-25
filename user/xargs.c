#include"kernel/types.h"
#include"kernel/stat.h"
#include"user/user.h"

#define MAXARGS 32
#define BUFSIZE 512

/*
在shell中'|'左侧的进程将输出通过pipe给右端进程作为输入
比如

$ find . -name "*.tmp" | xargs rm

就是找到所有.tmp文件，并交给右边删除

指令格式
xargs command [initial arguments]
*/

static int spilt_tokens(char* s,char* out[],int max){
    int n=0;

    while(*s){
        while(*s==' '||*s=='\t'){
            *s++=0;
        }
        if(*s==0||n>=max){
            break;
        }
        out[n++]=s;
        while(*s&&*s!=' '&&*s!='\t'){
            s++;
        }
    }

    return n;
}

int main(int argc,char* argv[]){
    if(argc<2){
        fprintf(2, "usage: xargs command [args...]\n");
        exit(1);
    }

    // 先读xargs后面的参数
    // 复制 argv[1:]
    char* base[MAXARGS];
    int basec=0;
    for(int i=1;i<argc;i++){
        if(basec>=MAXARGS-1){
            fprintf(2,"args exceed\n");
            exit(1);
        }
        base[basec++]=argv[i];
    }

    char buf[BUFSIZE];
    int idx=0;

    // 再read终端中在前面的指令和参数，用0分割
    for(;;){
        char c;
        int rw=read(0,&c,1);    // 从console读入
        if(rw<0){
            fprintf(2,"read error\n");
            exit(1);
        }
        if(rw==0){  // EOF
            if(idx==0){
                break;
            }
            buf[idx]=0;
        }else if(c=='\n'){
            buf[idx]=0;
        }else{
            if(idx<BUFSIZE){
                buf[idx++]=c;
            }
            continue;
        }

        // 准备构造argv
        char* lineArgs[MAXARGS];
        int linec=spilt_tokens(buf,lineArgs,MAXARGS);

        char* execv[MAXARGS];
        int k=0;

        for(int i=0;i<basec;i++){
            execv[k++]=base[i];
        }
        for(int i=0;i<linec;i++){
            execv[k++]=lineArgs[i];
        }

        execv[k]=0;

        int pid=fork();
        if(pid<0){
            fprintf(2,"xargs : fork failed\n");
            exit(1);
        }
        if(pid==0){
            exec(execv[0],execv);
            fprintf(2, "xargs: exec %s failed\n", execv[0]);
            exit(1);
        }else{
            wait(0);
        }

        idx=0;
        if(rw==0) break;    // 要处理EOF，防止下一行的读入出问题
    }
    exit(0);
}
