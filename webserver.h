#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"

const int MAX_FD = 65536;           // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000; // 最大事件数 1w
const int TIMESLOT = 5;             // 最小超时单位 5s

class WebServer
{
public:
    WebServer();
    ~WebServer();

    // 初始化参数
    void init(int port, string user, string passWord, string databaseName,
              int log_write, int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);

    void thread_pool();     // 线程池
    void sql_pool();        // 数据库连接池
    void log_write();       // 日志写入模式
    void trig_mode();       // 组合模式
    void eventListen();     // 监听
    void eventLoop();       // 工作线程执行的内容
    void timer(int connfd, struct sockaddr_in client_address);  // 对创建的连接设置定时器，并插入到定时器链表
    void adjust_timer(util_timer *timer);                       // 重置定时器
    void deal_timer(util_timer *timer, int sockfd);             // 清理连接操作
    bool dealclientdata();                                      // 处理客户端连接
    bool dealwithsignal(bool &timeout, bool &stop_server);      // 处理信号事件
    void dealwithread(int sockfd);                              // 处理就绪读
    void dealwithwrite(int sockfd);                             // 处理就绪写

public:
    // 基础参数
    int m_port;         // 端口号
    char *m_root;       // 根目录，即  ……/root/
    int m_log_write;    // 日志写入方式，0：同步；1：异步
    int m_close_log;    // 是否关闭日志系统
    int m_actormodel;   // 两种模式

    int m_pipefd[2];    // 进程通信管道
    int m_epollfd;      // epollfd
    http_conn *users;   // 用户连接数组指针

    // 数据库相关
    connection_pool *m_connPool;    // 数据库连接池指针
    string m_user;                  // 登陆数据库用户名
    string m_passWord;              // 登陆数据库密码
    string m_databaseName;          // 使用数据库名
    int m_sql_num;                  // 创建的连接数

    // 线程池相关
    threadpool<http_conn> *m_pool; // 线程池
    int m_thread_num;              // 线程数

    // epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;         // 监听 fd
    int m_OPT_LINGER;       // 是否优雅断开
    int m_TRIGMode;         // 组合模式，即监听和处理连接模式组合
    int m_LISTENTrigmode;   // 监听模式
    int m_CONNTrigmode;     // 处理连接模式

    // 定时器相关
    client_data *users_timer;   // 用户数据 + 定时器
    
    Utils utils;                // 工具类
};
#endif
