//本程序测试阻塞或非阻塞方式打开
//使用方法:
//阻塞方式:./app /dev/mybtns0 0
//非阻塞方式:./app /dev/mybtns0 1
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <poll.h>



//argv[2]: 0 表示阻塞  1表示非阻塞
//./app /dev/mybtn0 0/1
int main(int argc, char * argv[]) {
    int  fd;
    int i;
    int ret;
    int flags;
	
    char  cur_buf[5],  key_buf[5] = {"0000"};

    struct pollfd pfds[1];   

    if(argc != 3) {
        printf("++Usage:%s /dev/xxx   0/1\r\n", argv[0]);
        return -1;
    }

    //根据用户设置打开标志
    if( !strcmp(argv[2], "0") ) {
        flags = O_RDWR;
        printf("block !\r\n");
    } else if( !strcmp(argv[2], "1")) {
        flags = O_RDWR | O_NONBLOCK;
        printf("non block !\r\n");
    } else {
        printf("error  !\r\n");
        printf("Usage:%s /dev/xxx   0/1\r\n", argv[0]);
        return -1;
    }

    //应用每open一次会在内核中创建一个 struct file 结构，
    //这个结构用户空间是看不到的
    //后面可以通过fd来找到  struct file ，所以后面的read,write函数第一个参数都是fd
    fd = open(argv[1], flags);   //默认是阻塞
    if(fd < 0) {
        perror("open");
        return -1;
    }

    //设置一个查询的事件
    pfds[0].fd    = fd;
    pfds[0].events = POLLIN;

    while(1) {
        //查询期望的事件
        //ret = poll(pfds, 1, -1);  //永远等待，直接期望事件发生才返回
        ret = poll(pfds, 1, 2000);  //最多等待2s,没有事件发生也返回
        printf("++++++++++++++++++++\r\n");
        if(ret > 0) {
            //分别判断判断每个fd查询结果
            if(pfds[0].revents & POLLIN) {
                read(fd, cur_buf, 4);
                printf("--------------------\r\n");

                for ( i = 0 ; i < 4 ; i++ ) {
                    if(cur_buf[i] != key_buf[i]) {
                        key_buf[i] = cur_buf[i] ;

                        if(key_buf[i] == '1') {
                            printf("key%d press\n", i + 1);
                        }

                        else {
                            printf("key%d up\n", i + 1);
                        }
                    }
                }
            }
        } else if(ret == 0) {
            printf("time out!\n");
        } else {
            perror("poll");
        }
    }

    close(fd);
    return 0;
}


