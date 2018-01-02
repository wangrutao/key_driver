#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

//./app /dev/mybtns0
int main(int argc, char * argv[]) {
    int fd;
    int i, ret;
    char cur_buf[5], key_buf[5] = {"0000"};
    
    if(argc != 2) {
        printf("Usage:%s /dev/xxx\r\n", argv[0]);
        return -1;
    }

    //应用每open一次会在内核中创建一个 struct file 结构，
    //这个结构用户空间是看不到的
    //后面可以通过fd来找到  struct file ，所以后面的read,write函数第一个参数都是fd
    fd = open(argv[1], O_RDWR);
    if(fd < 0) {
        perror("open");
        return -1;
    }

    while(1) {
      read(fd,cur_buf,4);
      for ( i = 0 ; i < 4 ; i++ ) {
          if(cur_buf[i] != key_buf[i]) {
               key_buf[i] = cur_buf[i] ;
               if(key_buf[i] == '1') {
                  printf("key%d press\n",i+1);
               } else {
                  printf("key%d up\n",i+1);
               }
          }
		  }
		
    }

    close(fd);
}






