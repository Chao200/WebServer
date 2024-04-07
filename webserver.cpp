#include "webserver.h"

/* 得到资源的根目录，创建 HTTP 连接和定时器数组 */ 
WebServer::WebServer()
{
    // root 文件夹路径
    char server_path[200];
    getcwd(server_path, 200);   // 当前文件路径
    char root[6] = "/root";     // 存放请求资源的地方
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);       // m_root 为存放资源的目录

    // http_conn 类对象，存储到数组
    users = new http_conn[MAX_FD];  // http_conn* 指针

    // 定时器，为每个用户连接分配一个定时器类，存储到数组
    users_timer = new client_data[MAX_FD];  // client_data* 指针
}

/* 清理资源，包括 Socket、epoll、pipe、用户连接、定时器、线程池 */
WebServer::~WebServer()
{
    close(m_epollfd);       // 关闭 epoll
    close(m_listenfd);      // 关闭监听
    close(m_pipefd[1]);     // 关闭 pipe 写端
    close(m_pipefd[0]);     // 关闭 pipe 读端
    delete[] users;         // 删除用户 HTTP 连接数据
    delete[] users_timer;   // 删除用户定时器
    delete m_pool;          // 删除线程池
}

/* 根据参数初始化变量 */
void WebServer::init(int port, string user, string passWord, string databaseName, int log_write,
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;                 // 端口号
    m_user = user;                 // 用户名
    m_passWord = passWord;         // 用户密码
    m_databaseName = databaseName; // 数据库名
    m_sql_num = sql_num;           // sql 连接数
    m_thread_num = thread_num;     // 线程数
    m_log_write = log_write;       // 日志写入方式，0：同步；1：异步
    m_OPT_LINGER = opt_linger;     // 是否优雅关闭连接 1：使用；0：不使用
    m_TRIGMode = trigmode;         // listendf 和 connfd 触发方式
    m_close_log = close_log;       // 是否关闭 log
    m_actormodel = actor_model;    // Reactor(1) 还是 Proactor(0)
}

/* 组合模式，监听和连接 */
void WebServer::trig_mode()
{
    // LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    // LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    // ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    // ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

/* 根据参数，决定是否使用日志系统，以及日志类型是同步(1) 还是异步(1) */
void WebServer::log_write()
{
    if (0 == m_close_log)
    {
        // 初始化日志
        if (1 == m_log_write) // 异步写日志，循环队列大小默认 800
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else // 同步写日志
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

/* 创建数据库连接池 */
void WebServer::sql_pool()
{
    // 初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    // 初始化数据库读取表
    users->initmysql_result(m_connPool);
}

/* 创建线程池 */
void WebServer::thread_pool()
{
    // 线程池，需要传入模式、数据库连接池、线程数
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

/* 监听事件 */
void WebServer::eventListen()
{
    // 网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0); // 创建 Socket
    assert(m_listenfd >= 0);

    // 优雅关闭连接
    if (0 == m_OPT_LINGER) // 不使用
    {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER) // 使用优雅关闭
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    int flag = 1; // 复用
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    utils.init(TIMESLOT); // 初始化定时器

    // epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    // 将监听注册到 epoll
    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    // 当前的 epollfd
    http_conn::m_epollfd = m_epollfd;

    // 用管道实现进程间通信
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);             // 非阻塞写
    utils.addfd(m_epollfd, m_pipefd[0], false, 0); // 注册读事件

    utils.addsig(SIGPIPE, SIG_IGN);                  // 向一个已经关闭的套接字写数据时触发，不会终止，而是返回错误
    utils.addsig(SIGALRM, utils.sig_handler, false); // 调用信号处理函数，且不可重入
    utils.addsig(SIGTERM, utils.sig_handler, false); // 调用信号处理函数，且不可重入

    alarm(TIMESLOT); // 启动定时器

    // 工具类，信号描述符基础操作
    Utils::u_pipefd = m_pipefd;   // 管道
    Utils::u_epollfd = m_epollfd; // epoll
}

/* 对创建的连接设置定时器，并插入到定时器链表 */
void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    // 初始化该连接的参数信息
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    // 初始化 client_data 结构体数据信息
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;

    // 创建该连接的定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func; // 回调函数，连接超时时触发
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT; // 设置超时时间为当前时间往后 15s
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer); // 插入定时器链表
}

// 若有数据传输，则将定时器往后延迟 3 个单位，即重置
// 并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

/* 调用回调函数，进行清理，并删除定时器 */
void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]); // 调用定时器的回调函数，从 epoll 中删除，关闭 Socket、减少用户数量
    if (timer)
    {
        utils.m_timer_lst.del_timer(timer); // 删除该连接的定时器
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

/* 根据 LT 和 ET 模式来处理到来的新连接 */
bool WebServer::dealclientdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    if (0 == m_LISTENTrigmode) // LT
    {
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD)
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);
    }

    else // ET
    {
        while (1) // 循环处理
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

/* 不断监听管道上的信号，响应定时器超时和停止服务器事件 */
bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM: // 超时
            {
                timeout = true;
                break;
            }
            case SIGTERM: // 停止服务器
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

/* 根据不同的模式处理读事件 */
void WebServer::dealwithread(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    if (1 == m_actormodel) // reactor，直接将读写时间放入请求队列
    {
        if (timer)
        {
            adjust_timer(timer); // 重置定时器
        }

        // 若监测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0); // 读 0 写 1

        while (true)
        {
            if (1 == users[sockfd].improv) // 请求是否被处理
            {
                if (1 == users[sockfd].timer_flag) // 是否超时
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else // proactor，先读取，再放入请求队列
    {
        if (users[sockfd].read_once()) // 处理读事件
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            // 若监测到读事件，将该事件放入请求队列
            m_pool->append_p(users + sockfd);

            if (timer)
            {
                adjust_timer(timer); // 重置定时器
            }
        }
        else // 读取失败，清理操作
        {
            deal_timer(timer, sockfd);
        }
    }
}

/* 根据不同的模式处理写事件 */
void WebServer::dealwithwrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    if (1 == m_actormodel) // reactor
    {
        if (timer)
        {
            adjust_timer(timer); // 重置定时器
        }

        m_pool->append(users + sockfd, 1); // 1 表示写

        while (true) // 与处理就绪读一样
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else // proactor
    {
        if (users[sockfd].write()) // 处理写事件
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
            {
                adjust_timer(timer); // 重置
            }
        }
        else // 写失败，清理
        {
            deal_timer(timer, sockfd);
        }
    }
}

/* 工作线程所需要执行的任务 */
void WebServer::eventLoop()
{
    bool timeout = false;     // 超时
    bool stop_server = false; // 停止

    while (!stop_server)
    {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1); // 等待事件发生
        if (number < 0 && errno != EINTR)                                 // 不是信号中断引发的错误
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd; // 变化的 Socket

            // 处理新到的客户连接
            if (sockfd == m_listenfd) // 如果是监听端口变化，说明有新的连接
            {
                bool flag = dealclientdata();
                if (false == flag)
                    continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            // 处理信号，即管道触发的就绪读事件
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            // 处理客户连接上接收到的数据，即读就绪事件
            else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            // 处理写就绪事件
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }
        if (timeout) // 超时处理
        {
            utils.timer_handler(); // 移除超时连接，并重启定时器

            LOG_INFO("%s", "timer tick"); // 将超时记录到日志系统

            timeout = false; // 重置超时
        }
    }
}
