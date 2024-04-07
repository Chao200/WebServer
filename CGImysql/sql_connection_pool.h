#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>

#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

/* 数据库连接池 */
class connection_pool
{
public:
	MYSQL *GetConnection();				 // 获取数据库连接
	bool ReleaseConnection(MYSQL *conn); // 释放连接
	int GetFreeConn();					 // 获取连接
	void DestroyPool();					 // 销毁所有连接

	// 单例模式
	static connection_pool *GetInstance();

	// 初始化变量，数据库地址、用户名、密码、数据库名、端口号、最大连接数、是否关闭日志
	void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log);

private:
	connection_pool();
	~connection_pool();

	int m_MaxConn;	// 所拥有的最大连接数
	int m_CurConn;	// 当前已使用的连接数
	int m_FreeConn; // 当前空闲的连接数
	locker lock;
	list<MYSQL *> connList; // 连接池，list 存储
	sem reserve;	// 信号量，表示可用连接的资源数

public:
	string m_url;		   // 主机地址
	string m_Port;		   // 数据库端口号
	string m_User;		   // 登陆数据库用户名
	string m_PassWord;	   // 登陆数据库密码
	string m_DatabaseName; // 使用数据库名
	int m_close_log;	   // 日志开关
};

/* RAII */
class connectionRAII
{

public:
	connectionRAII(MYSQL **con, connection_pool *connPool);
	~connectionRAII();

private:
	MYSQL *conRAII;
	connection_pool *poolRAII;
};

#endif
