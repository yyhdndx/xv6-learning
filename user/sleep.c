#include"kernel/types.h"
#include"kernel/stat.h"
#include"user.h"

int main(int argc,char* argv[]){
    if(argc!=2){
        fprintf(2,"usage : sleep n\n");
        exit(1);
    }
    int n=atoi(argv[1]);
    if(n<0){
        fprintf(2,"usage : sleep n\n");
        exit(1);
    }
    pause(n);
    exit(0);
}
