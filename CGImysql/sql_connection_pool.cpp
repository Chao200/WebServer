#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>

#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool()
{
	m_CurConn = 0;	// 正在使用的个数
	m_FreeConn = 0; // 空闲的个数
}

/* 获得单例模式得到的数据库连接池 */
connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}

// 构造初始化，数据库地址、用户名、密码、数据库名、端口号、最大连接数、是否关闭日志
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)
{
	m_url = url;					// 数据库地址
	m_Port = Port;					// 端口
	m_User = User;					// 用户名
	m_PassWord = PassWord;			// 密码
	m_DatabaseName = DBName;		// 数据库名
	m_close_log = close_log;		// 是否关闭日志系统

	for (int i = 0; i < MaxConn; i++)	// 创建 MaxConn 个数据库连接
	{
		MYSQL *con = NULL;
		con = mysql_init(con);

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		connList.push_back(con);	// 添加到池中
		++m_FreeConn;	// 增加空闲连接
	}

	reserve = sem(m_FreeConn); // reserve 是信号量对象，使用 m_FreeConn 初始化信号量

	m_MaxConn = m_FreeConn;	// 最大可用的连接数，就是创建完池后的空闲连接大小
}

/* 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数 */
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL;

	if (0 == connList.size())
		return NULL;

	reserve.wait();	// P 操作，减 1

	lock.lock();

	con = connList.front();	// 取出一个数据库连接
	connList.pop_front();

	--m_FreeConn;	// 减少可用
	++m_CurConn;	// 增加正在使用的个数

	lock.unlock();
	return con;
}

/* 用完后，释放当前使用的连接，回到池子 */
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if (NULL == con)
		return false;

	lock.lock();

	connList.push_back(con);	// 回到池子
	++m_FreeConn;	// 增加空闲连接
	--m_CurConn;	// 减少正在使用连接

	lock.unlock();

	reserve.post();	// V 操作，加 1
	return true;
}

/* 析构函数调用，销毁数据库连接池 */
void connection_pool::DestroyPool()
{

	lock.lock();
	if (connList.size() > 0)
	{
		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);
		}
		m_CurConn = 0;
		m_FreeConn = 0;
		connList.clear();
	}

	lock.unlock();
}

/* 当前空闲的连接数 */
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;
}

/* 析构函数，销毁池子 */
connection_pool::~connection_pool()
{
	DestroyPool();
}

/* RAII */
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool)
{
	*SQL = connPool->GetConnection();	// 通过调用函数，从池子得到一个连接

	conRAII = *SQL;			// 连接赋值给变量
	poolRAII = connPool;	// 池子赋值给变量
}

connectionRAII::~connectionRAII()
{
	poolRAII->ReleaseConnection(conRAII);
}