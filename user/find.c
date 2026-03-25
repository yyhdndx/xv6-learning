#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"

static void find(const char *path, const char *target)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(path, 0)) < 0){
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch(st.type){
    case T_FILE: {
        // 对于文件，找到最后的'/'后面的，直接和target比较即可
        const char *base = path;
        for(const char *q = path; *q; q++){
            if(*q == '/')
                base = q + 1;
        }
        if(strcmp(base, target) == 0)
        printf("%s\n", path);
        break;
    }

    case T_DIR:
        if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)){
            fprintf(2, "find: path too long %s\n", path);
            close(fd);
            return;
        }

        // "buf = path + '/'"
        strcpy(buf, path);
        p = buf + strlen(buf);
        if(p != buf && *(p - 1) != '/')
            *p++ = '/';

        // 遍历目录
        while(read(fd, &de, sizeof(de)) == sizeof(de)){
            if(de.inum == 0) continue;

            // Copy name and NUL-terminate.
            // 这里不能用strcpy(因为不确定name有没有\0),也不能用memcpy，因为不确定src和dest是否有重叠
            char name[DIRSIZ + 1];
            memmove(name, de.name, DIRSIZ);
            name[DIRSIZ] = 0;

            // 目录内部可能有'.'和'..'，跳过，防止无限递归
            if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
                continue;

            // 把当前遍历得到的文件名接入buf
            // 注意，这里p指针的位置在遍历目录的时候是不会变的，所以是不断的用name去做覆盖
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;

            if(stat(buf, &st) < 0){
                fprintf(2, "find: cannot stat %s\n", buf);
                continue;
            }

            // 如果是文件，就直接比较name；否则是目录，就需要递归遍历
            if(st.type == T_DIR){
                find(buf, target);
            } else if(st.type == T_FILE){
                if(strcmp(name, target) == 0)
                    printf("%s\n", buf);
            }
        }
        break;
    }

    close(fd);
}

int main(int argc, char *argv[])
{
    if(argc != 3){
        fprintf(2, "usage: find <path> <filename>\n");
        exit(1);
    }
    find(argv[1], argv[2]);
    exit(0);
}