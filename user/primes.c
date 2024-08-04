#include "kernel/types.h"
#include "user/user.h"

#define RD 0
#define WR 1

// 左邻居：表示当前进程从管道中读取数据；从前一个进程接收数据
// 右邻居：表示将数据写入管道中的进程，向下一个进程发送数据

int lpipe_first_data(int lpipe[2], int *dst){
    // 
    if(read(lpipe[RD], dst, sizeof(int)) == sizeof(int)){
        fprintf(2, "prime %d \n", *dst);
        return 0;
    }
    return -1;
}

void transmit_data(int lpipe[2], int rpipe[2], int first){
    int data;
    while(read(lpipe[RD], &data, sizeof(int)) == sizeof(int)){
        if(data % first){
            write(rpipe[WR], &data, sizeof(int));
        }
    }
    close(lpipe[RD]);
    close(rpipe[WR]);
}

void primes(int lpipe[2]){
    close(lpipe[WR]); // 关闭写入端口，确保不再写入
    int first;

    // 如果能从管道中获取到第一个数据
    if(lpipe_first_data(lpipe, &first) == 0){
        int p[2];
        pipe(p); // 创建当前进程的新管道

        // 将数据传递给右邻居，忽略能被first整除的数据
        transmit_data(lpipe, p, first);

        if(fork() == 0){
            primes(p); // 递归调用 primes 函数处理新的管道
        }else{
            close(p[RD]); // 关闭读取端口，确保不再读取
            wait(0); // 等待子进程结束
        }
    }

    exit(0);
}

int main(){
    int p[2];
    pipe(p);

    for(int i = 4; i <= 35; i++){
        write(p[WR], &i, sizeof(int));
    }

    if(fork() == 0){
        primes(p); // 在子进程中调用primes函数处理管道数据
    }else{
        close(p[WR]);
        close(p[RD]);
        wait(0); // 等待进程结束
    }
    exit(0);
}