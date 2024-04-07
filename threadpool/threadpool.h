#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

/* 创建线程池 */
template <typename T>
class threadpool
{
public:
    // 模式、数据库连接池、线程数、最大 HTTP 请求数
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request, int state); // 添加 Reactor 模式请求
    bool append_p(T *request);          // 添加 Proactor 模式请求

private:
    // 工作线程运行的函数，它不断从工作队列中取出任务并执行
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number;         // 线程池中的线程数
    int m_max_requests;          // 请求队列中允许的最大请求数
    pthread_t *m_threads;        // 描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue;  // 请求队列
    locker m_queuelocker;        // 保护请求队列的互斥锁
    sem m_queuestat;             // 信号量，是否有任务需要处理
    connection_pool *m_connPool; // 数据库连接池
    int m_actor_model;           // 模型切换
};

// 构造函数，初始化 模式、数据库连接池、线程数、最大 HTTP 请求数
template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_number, int max_requests) : m_actor_model(actor_model), m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL), m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    
    // 创建线程池数组
    m_threads = new pthread_t[m_thread_number];

    if (!m_threads)
        throw std::exception();

    // 创建线程池
    for (int i = 0; i < thread_number; ++i)
    {   // 需要传入 this，才能调用静态 worker
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
        // 线程结束后，自动回收内存
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

/* 析构，回收线程池 */
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}

/* Reactor模式 添加请求到队列 */
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state; // 读 0 写 1，Reactor 模式需要标记读写
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post(); // V 操作，加 1
    return true;
}

/* Proactor模式 添加请求到队列 */
template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request); // Proactor 模式不需要标记读写
    m_queuelocker.unlock();
    m_queuestat.post(); // V 操作，加 1
    return true;
}

/* 工作线程需要执行的 */
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

/* 从请求队列取出请求，解析后回送响应 */
template <typename T>
void threadpool<T>::run()
{
    while (true)
    {
        // 信号量，减 1
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty())    // 请求队列为空，continue
        {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();  // 取出一个请求
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if (!request)
            continue;
        
        if (1 == m_actor_model) // Reactor 模式
        {
            if (0 == request->m_state)  // 读任务
            {
                if (request->read_once())
                {
                    request->improv = 1;    // 读完成
                    // 从数据库连接池，取出一个连接
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();     // 处理请求
                }
                else
                {
                    request->improv = 1;        // 读完成
                    request->timer_flag = 1;    // 超时
                }
            }
            else    // 写任务
            {
                if (request->write())
                {
                    request->improv = 1;            // 写完成
                }
                else
                {
                    request->improv = 1;            // 写完成
                    request->timer_flag = 1;        // 超时
                }
            }
        }
        else    // Proactor 模式
        {
            // 从数据库连接池，取出一个连接
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process(); // 处理请求
        }
    }
}
#endif
