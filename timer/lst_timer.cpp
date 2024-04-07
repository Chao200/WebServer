#include "lst_timer.h"
#include "../http/http_conn.h"

/* 初始化有序链表 */
sort_timer_lst::sort_timer_lst()
{
    head = NULL;
    tail = NULL;
}

/* 析构有序链表，删除所有定时器节点 */
sort_timer_lst::~sort_timer_lst()
{
    util_timer *tmp = head;
    while (tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

/* 添加定时器 */
void sort_timer_lst::add_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    if (!head)  // 没有头结点
    {
        head = tail = timer;
        return;
    }
    if (timer->expire < head->expire)   // 过期时间小于头结点的，插入到头结点之前
    {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    add_timer(timer, head); // 否则，插入到头结点之后
}

/* 调整计时器的位置 */
void sort_timer_lst::adjust_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    // timer 前面的计时器都是一起减少时间，所以调整后，肯定大于前面的节点
    // 只需要比较后面的接地啊
    util_timer *tmp = timer->next;
    if (!tmp || (timer->expire < tmp->expire))  // 如果 timer 就是尾节点 或 时间小于后者
    {
        return;
    }
    // 根据前一个 if 后者的条件，此时 timer 已经比 tmp 大了，即比后一个大
    if (timer == head)  // 如果是头结点，直接把头指针后移，而将 timer add 到 head 之后
    {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head); // timer 必定大于 head
    }
    else    // 非头结点，把该节点单独提出来，再 add
    {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

/* 删除定时器 */
void sort_timer_lst::del_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    if ((timer == head) && (timer == tail)) // 只有一个节点
    {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    if (timer == head)  // 删除头
    {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    if (timer == tail)  // 删除尾
    {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    // 中间节点
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

/* tick，即时钟周期处理函数，检测超时连接 */
void sort_timer_lst::tick()
{
    if (!head)
    {
        return;
    }
    
    time_t cur = time(NULL);
    util_timer *tmp = head;
    while (tmp)
    {
        if (cur < tmp->expire)  // 比 tmp 小，肯定比后面都小，直接 break
        {
            break;
        }
        tmp->cb_func(tmp->user_data);   // 说明大于后者，调用回调函数清理 tmp
        head = tmp->next;   // 移动 head
        if (head)   // head 不为空，重置前向指针
        {
            head->prev = NULL;
        }
        delete tmp; // 删除 tmp
        tmp = head; // 继续后移
    }
}

/* 在 head 之后按序添加 timer */
void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head)
{
    util_timer *prev = lst_head;
    util_timer *tmp = prev->next;   // 指向下一个比较的节点
    while (tmp)
    {
        if (timer->expire < tmp->expire)    // 找到插入的位置
        {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;         // 继续后移
        tmp = tmp->next;
    }
    if (!tmp)   // 如果 tmp 为空，则说明需要插入到尾结点
    {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}


/* 初始化超时时间 */
void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

/* 对文件描述符设置非阻塞 */
int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/* 向内核事件表注册读事件，ET 模式，选择开启 EPOLLONESHOT */
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

/* 信号处理函数 */
void Utils::sig_handler(int sig)
{
    // 为保证函数的可重入性，保留原来的 errno
    int save_errno = errno; // errno 类似全局变量，由于多线程或进程，可能 errno 会变
    int msg = sig;
    // 通过管道发送给另一个进程
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

/* 设置信号函数 */
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

/* 定时处理任务，重新定时以不断触发 SIGALRM 信号 */
void Utils::timer_handler()
{
    m_timer_lst.tick(); // 定时器周期处理函数
    alarm(m_TIMESLOT);  // 重启计时器
}

/* 向客户端发送错误心理，并关闭连接 */
void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;   // 初始化为 0
int Utils::u_epollfd = 0;   // 初始化为 0

class Utils;

/* 从 epoll 中删除，关闭 Socket、减少用户数量 */
void cb_func(client_data *user_data)
{
    // 从 epoll 中删除
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    // 关闭连接
    close(user_data->sockfd);
    // 减少 HTTP 连接数
    http_conn::m_user_count--;
}
