#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <time.h>

#include "../log/log.h"

class util_timer;

/* 存储用户的数据以及定时器 */
struct client_data
{
    sockaddr_in address;    // IP + 端口
    int sockfd;             // Socket
    util_timer *timer;      // 定时器
};

/* 定时器节点，类似 ListNode */
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire;      // 超时时间

    void (*cb_func)(client_data *); // 超时后执行的清理函数
    client_data *user_data; // 用户数据 + 定时器
    util_timer *prev;   // 前指针
    util_timer *next;   // 后指针
};

/* 有序链表，存储的是定时器节点，即 util_timer */
class sort_timer_lst
{
public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(util_timer *timer);      // 添加节点
    void adjust_timer(util_timer *timer);   // 调整节点
    void del_timer(util_timer *timer);      // 删除节点
    void tick();                            // 执行定时器任务，看是否有超时的节点，并执行删除回收

private:
    // 两个 add_timer 一个给外部接口，一个内部调用
    void add_timer(util_timer *timer, util_timer *lst_head);    // 添加节点

    util_timer *head;   // 头结点
    util_timer *tail;   // 尾节点
};

/* 工具类 */
class Utils
{
public:
    Utils() {}
    ~Utils() {}

    // 初始化超时时间
    void init(int timeslot);

    // 对文件描述符设置非阻塞
    int setnonblocking(int fd);

    // 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    // 信号处理函数
    static void sig_handler(int sig);

    // 设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    // 定时处理任务，即执行 tick 函数，重新定时以不断触发 SIGALRM 信号
    void timer_handler();

    // 向客户端发送错误，并断开连接
    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;           // 管道 fd
    sort_timer_lst m_timer_lst;     // 定时器有序链表
    static int u_epollfd;           // epollfd
    int m_TIMESLOT;                 // 超时时间
};

void cb_func(client_data *user_data);   // 回调函数

#endif
