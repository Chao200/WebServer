
同步/异步日志系统
===============
同步/异步日志系统主要涉及了两个模块，一个是日志模块，一个是阻塞队列模块,其中加入阻塞队列模块主要是解决异步写入日志做准备.
> * 自定义阻塞队列
> * 单例模式创建日志
> * 同步日志
> * 异步日志
> * 实现按天、超行分类


---

日志就是存储在建立连接过程中的一些信息，比如异常、关闭连接、创建连接等

这些信息就是 string，

同步日志，按序处理 string，写入日志文件

异步日志，把 string 存入循环队列，让工作线程竞争处理，写入日志文件