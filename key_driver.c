#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/delay.h>

#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/poll.h>


//1.  定义一个等待队列头并且初始化
wait_queue_head_t wq;


//设备名定义
#define   DEV_NAME    "wrtdev"
#define   BTN_SIZE          4      //设备支持的leds数量

char kbuf[BTN_SIZE + 1]  = {"0000"}; //按键缓冲区

//自定义一个数据结构，
struct mydata {
    int x;
    int y;
    struct work_struct work1;
};
//定义一个 mydata 结构变量
static struct mydata  gvar;

//定义按键对象结构类型
typedef struct button {
    int id;
    int gpio;
    int irq;
    char *name;
} button_t;
//定义按键对象
static button_t  btns[] = {
    {0, EXYNOS4_GPX3(2), IRQ_EINT(26), "key1"},
    {1, EXYNOS4_GPX3(3), IRQ_EINT(27), "key2"},
    {2, EXYNOS4_GPX3(4), IRQ_EINT(28), "key3"},
    {3, EXYNOS4_GPX3(5), IRQ_EINT(29), "key4"}
};

static struct cdev *pcdev;        //cdev结构指针
static dev_t    dev_nr;           //存放第一个设备号
static unsigned count = 2;        //设备数量
static struct device *this_device;
static  struct class *myclass;    //定义一个class结构指针

static  int ev_press = 0;      //按键动作标志

void work_func(struct work_struct*  data) {
    struct mydata* p;
    //普通方法
    // p = (struct mydata*)  ((unsigned long)data - 4 );
    //通用NB方法，建议使用这个方法 这个宏可以通过一个结构体成员的地址反推出该结构体变量首地址
    p = container_of(data, struct mydata, work1);

    //以地址形式输出  work 值
    printk("work:%p\r\n", data);
    printk("x:%d,y:%d\r\n", p->x, p->y);
}

//dev_id 注册时候传递了 1~4这个几数字做指针
irqreturn_t  btn_handler(int irq,  void * dev_id) {
    int dn;
    button_t *pdata;

    pdata = (button_t *)dev_id;        //转换成按键结构
    dn = !gpio_get_value(pdata->gpio); //读取指定IO编号 的电平
    kbuf[pdata->id]  =  dn + '0' ;

    //让条件为真
    ev_press    = 1 ;                  //表示发生了按键动作(松开或按下)
    wake_up(&wq)   ;                   //唤醒休眠的进程

    //调度工作（把工作结构添加到内核共享工作队列上等待执行）
    schedule_work(&gvar.work1);

    return IRQ_HANDLED;
}

//成功返回0，失败返回负数
static  int key_open(struct inode *pinode, struct file *pfile) {
    int minjor = iminor(pinode);  //取出次设备号
    printk("minjor:%d\r\n", minjor);

    return 0;
}

//成功返回0，失败返回负数
static  int key_release(struct inode *pinode, struct file *pfile) {
    return 0;
}

//成功返回读取的字节数量，失败返回负数
static  ssize_t key_read(struct file *pfile, char __user *buf, size_t size, loff_t * off)
{
    int ret  = 0;
    if(size == 0) {
        return 0;
    }

    //修正用户空间传递下来的非法参数
    if(size > BTN_SIZE ) {
        size = BTN_SIZE;  //2
    }

    //没有按键动作
    if(!ev_press) {
        //如果是阻塞方式休眠，否则直接返回错误码
        if( pfile-> f_flags & O_NONBLOCK) { //非阻塞方式
            return -EAGAIN;
        } else {
#if 0
//            while(!ev_press) {
//                msleep(100);
//            }
#endif
            //wait_event(wq, ev_press) ; //休眠 等待  ev_press 为真
            ret = wait_event_interruptible(wq, ev_press) ; //休眠 等待  ev_press 为真
            if(ret < 0) {
                printk("error interruptible return \n");
                return -EAGAIN;
            }
        }
    }

    //能运行到这里说明有按键动作，为了下次读还可以阻塞，必须清标志
    ev_press = 0;

    //复制数据到用户空间
    ret = copy_to_user(buf, kbuf, size);
    if(ret) {
        printk("error copy_to_user \n");
        ret =  -EFAULT;
        goto error_copy_to_user;
    }

    return  size;

error_copy_to_user:
    return ret;
}

///对应的系统调用  poll ,  select 两个函数
unsigned int key_poll(struct file *pfile, struct poll_table_struct *poll_table) {
    int mask = 0;
    poll_wait(pfile, &wq, poll_table);   //这一步不会休眠 

    //根据设备状态设置 mask
    if(ev_press) {
        mask   |= POLLIN | POLLRDNORM;    //什么数值表示可读，写，应该是有标准
    }

    printk("key_poll is call! \r\n");

    //返回设置设备状态
    return  mask;
}

//核心是实现文件操作方法
static  struct file_operations myfops = {
    .open      = xxx_open,
    .release   = xxx_release,
    .read      = xxx_read,
    .poll      = xxx_poll,
};

//模块安装成功返回 0，负数失败
static int __init mydev_init(void)
{
    int ret ;
    int i, j;

    //1)分配cdev结构空间
    pcdev = cdev_alloc();
    if(!pcdev) {
        printk("error : cdev_alloc\r\n");
        ret = -ENOMEM;
        goto error_cdev_alloc;
    }

    //2)申请设备号
    //register_chrdev_region(dev_t from, unsigned count, const char * name)
    ret = alloc_chrdev_region(&dev_nr, 0, count, DEV_NAME);
    if(ret < 0) {
        printk("error : alloc_chrdev_region\r\n");
        goto error_alloc_chrdev_region;
    }

    //3)初始化cdev结构空间
    cdev_init(pcdev, &myfops);

    //4)注册cdev设备
    ret = cdev_add(pcdev, dev_nr, count);
    if(ret < 0) {
        printk("error : cdev_add\r\n");
        goto error_cdev_add;
    }

    //输出主设备号
    printk("major:%d\n", MAJOR(dev_nr));
    printk(" cdev_add  ok\r\n");


    //创建设备类
    myclass = class_create(THIS_MODULE, "myclass");
    if(IS_ERR(myclass)) {
        ret = PTR_ERR(myclass);
        goto error_class_create;
    }

    //创建/dev/目录下设备文件
    for ( i = 0 ; i < count ; i++ ) {
        this_device = device_create(myclass, NULL, dev_nr + i, pcdev, "%s%d", "mybtns", i); // mybtns0 mybtns1
        if(IS_ERR(this_device)) {
            ret = PTR_ERR(this_device);
            goto error_device_create;
        }
    }

    //注册按键中断
    for ( j = 0 ; j < 4 ; j++ ) {
        int flags;
        //设置为双边沿触发,共享中断 IRQF_SHARED
        flags = IRQF_TRIGGER_RISING  |  IRQF_TRIGGER_FALLING;

        ret = request_irq(btns[j].irq, btn_handler, flags, btns[j].name, (void*)&btns[j]);
        if(ret < 0) {
            printk("error request_irq \r\n");
            goto error_request_irq;
        }
    }
	
    //初始化等待队列头
    init_waitqueue_head(&wq);

    gvar.x = 123;
    gvar.y = 456;
    //初始化 work_struct 结构变量
    INIT_WORK(&gvar.work, work_func);
    //输出 mywork 地址
    printk("&mywork:%p\r\n", &gvar.work);
	
    return 0;

error_request_irq:    
    for ( i = 0 ; i < count ; i++ ) {		//删除/dev/目录下的设备文件
        device_destroy(myclass, dev_nr + i);
    }

error_device_create:
    class_destroy(myclass);
error_class_create:
    cdev_del(pcdev);
error_cdev_add:
    unregister_chrdev_region(dev_nr, count);
error_alloc_chrdev_region:
    kfree(pcdev);     //释放分配的空间
error_cdev_alloc:
    return ret;
}

static void  __exit mydev_exit(void)
{
    int i, j;
	
    //取消工作队列
    cancel_work_sync(&gvar.work);
	
    //释放按键中断
    for ( j = 0 ; j < 4 ; j++ ) {
        free_irq(btns[j].irq, (void*)&btns[j]);
    }

    //删除/dev/目录下的设备文件
    for ( i = 0 ; i < count ; i++ ) {
        device_destroy(myclass, dev_nr + i);
    }

    //销毁设备类
    class_destroy(myclass);

    //1) 注销cdev设备
    cdev_del(pcdev);

    //2) 释放设备号
    unregister_chrdev_region(dev_nr, count);

    //3) 释放cdev空间
    kfree(pcdev);

    printk(" unregister_chrdev  ok\r\n");
}


module_init(mydev_init);
module_exit(mydev_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("wrt");
MODULE_DESCRIPTION("misc device");
