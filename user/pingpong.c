#include "kernel/types.h"
#include "user/user.h"

#define RD 0 //pipe的读端
#define WT 1 //pipe的写端

int main()
{
    char buf = 'P'; 

    int fd_p2c[2];
    int fd_c2p[2];

    pipe(fd_c2p);
    pipe(fd_p2c);

    int pid = fork();
    int exit_status = 0;

    if(pid < 0){
        fprintf(2, "fork error!\n");
        close(fd_p2c[RD]);
        close(fd_p2c[WT]);
        close(fd_c2p[RD]);
        close(fd_c2p[WT]);
        exit(1);
    }else if(pid == 0){

        // 关闭不需要的管道
        close(fd_p2c[WT]);
        close(fd_c2p[RD]);

        // 从父进程中读取数据
        if(read(fd_p2c[RD], &buf, sizeof(char)) != sizeof(char)){
            fprintf(2, "child read() error!\n");
            exit_status = 1; // 标记出错
        }else{
            fprintf(2, "%d: received ping ~ \n", getpid());
        }

        // 向父进程中写入数据
        if(write(fd_c2p[WT], &buf, sizeof(char)) != sizeof(char)){
            fprintf(2, "child write error!\n");
            exit_status = 1;
        }
        close(fd_p2c[RD]);
        close(fd_c2p[WT]);

        exit(exit_status);
    }else{
        close(fd_c2p[WT]);
        close(fd_p2c[RD]);

        // 向子进程中写入数据
        if(write(fd_p2c[WT], &buf, sizeof(char)) != sizeof(char)){
            fprintf(2, "parent write() error!\n");
            exit_status = 1;
        }

        // 从子进程中读取数据
        if(read(fd_c2p[RD], &buf, sizeof(char)) != sizeof(char)){
            fprintf(2, "parent read() error!\n");
            exit_status = 1;
        }else{
            fprintf(2, "%d: received pong!\n", getpid());
        }
        close(fd_c2p[RD]);
        close(fd_p2c[WT]);
        exit(exit_status);
    }
}