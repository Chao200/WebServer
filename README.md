# WebServer

## 概述

Linux 下 C++ 轻量级 Web 服务器

* 使用 **线程池 + 非阻塞socket + epoll(ET和LT均实现) + 事件处理(Reactor和模拟Proactor均实现)** 的并发模型
* 使用**状态机**解析HTTP请求报文，支持解析**GET和POST**请求
* 访问服务器数据库实现web端用户**注册、登录**功能，可以请求服务器**图片和视频文件**
* 实现**同步/异步日志系统**，记录服务器运行状态
* 经Webbench压力测试可以实现**上万的并发连接**数据交换


## 服务器环境

- Ubuntu 版本 22.04(arm)
- MySQL 版本 8.0.36


## 安装依赖

```bash
sudo install make
sudo install g++
```

```bash
sudo apt install mysql-server
sudo systemctl start mysql
sudo mysql -u root -p
```

> 数据库初始默认没有密码，随便输入就可以进入数据库


```MySQL
ALTER USER 'root'@'localhost' IDENTIFIED WITH mysql_native_password BY 'password';
FLUSH PRIVILEGES;
exit;
```

> 这里的 password 需要复杂度高一点，才可以配置成功，也可以去配置文件调整

```bash
sudo systemctl restart mysql
sudo apt install libmysqlclient-dev
```

## 创建数据库

```bash
sudo mysql -u root -p
```

输入密码，进入数据库

```MySQL
// 建立yourdb库
create database yourdb;

// 创建user表
USE yourdb;
CREATE TABLE user(
    username char(50) NULL,
    passwd char(50) NULL
)ENGINE=InnoDB;

// 添加数据
INSERT INTO user(username, passwd) VALUES('name', 'passwd');
```

## 修改 main.cpp 数据库初始化信息

```C++
//数据库登录名,密码,库名
string user = "root";
string passwd = "root";
string databasename = "yourdb";
```

## build
```C++
make server
./server
```
在浏览器输入

```html
ip:9006
```

## 个性化运行

```C++
./server [-p port] [-l LOGWrite] [-m TRIGMode] [-o OPT_LINGER] [-s sql_num] [-t thread_num] [-c close_log] [-a actor_model]
```

> 以上参数不是非必须，不用全部使用，根据个人情况搭配选用即可

* -p，自定义端口号
  * 默认 9006
* -l，选择日志写入方式，默认同步写入
  * 0，同步写入
  * 1，异步写入
* -m，listenfd和connfd的模式组合，默认使用LT + LT
  * 0，表示使用 LT + LT
  * 1，表示使用 LT + ET
  * 2，表示使用 ET + LT
  * 3，表示使用 ET + ET
* -o，优雅关闭连接，默认不使用
  * 0，不使用
  * 1，使用
* -s，数据库连接数量
  * 默认为 8
* -t，线程数量
  * 默认为 8
* -c，关闭日志，默认打开
  * 0，打开日志
  * 1，关闭日志
* -a，选择反应堆模型，默认 Proactor
  * 0，Proactor 模型
  * 1，Reactor 模型

测试示例命令与含义

```C++
./server -p 9007 -l 1 -m 0 -o 1 -s 10 -t 10 -c 1 -a 1
```

- [X] 端口 9007
- [X] 异步写入日志
- [X] 使用 LT + LT组合
- [X] 使用优雅关闭连接
- [X] 数据库连接池内有 10 条连接
- [X] 线程池内有 10 条线程
- [X] 关闭日志
- [X] Reactor 反应堆模型

## 压力测试

进入 `test_pressure/webbench-1.5` 目录

- 安装依赖

```bash
sudo apt install libc6-dev
sudo apt install libtirpc-dev
sudo ln -s /usr/include/tirpc/rpc/types.h /usr/include/rpc
sudo ln -s /usr/include/tirpc/netconfig.h /usr/include
```

- 生成新的可执行文件
```bash
rm webbench
make
```

- 测试
打开两个命令行窗口，一个执行 `./server`，关闭日志系统，一个执行 webbench 测试

QPS = Requests / running

  - Proactor，LT + LT
  QPS = 70363
  ./server -m 0 -c 1 -a 0
  ./webbench -c 7000 -t 5 http://192.168.64.11:9006/

  - Proactor，LT + ET
  QPS = 69258
  ./server -m 1 -c 1 -a 0
  ./webbench -c 7000 -t 5 http://192.168.64.11:9006/

  - Proactor，ET + LT
  QPS = 53796
  ./server -m 2 -c 1 -a 0
  ./webbench -c 7000 -t 5 http://192.168.64.11:9006/

  - Proactor，ET + ET
  QPS = 60640
  ./server -m 3 -c 1 -a 0
  ./webbench -c 7000 -t 5 http://192.168.64.11:9006/

