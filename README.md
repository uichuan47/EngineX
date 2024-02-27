# EngineX项目概述

本项目实现了一个完整的网络通信服务器程序项目，包括通信框架以及业务逻辑框架。

- 项目本身是一个完整的多线程高并发服务器程序。

- 按照包头+包体格式正确接收客户端发送的数据包，解决了数据粘包问题。
- 根据收到的包的不同来执行不同的业务逻辑处理，并且把处理后的结果按照规定结构返回给客户端

------

本项目在一定程度上参考了Nginx的架构，参考了网络资料与相关数据，很多细节参考了Nginx的实现方式，

对本人来讲，很具有挑战性。

------

### 开发技术

- epoll高并发通信技术，水平触发模式（LT）
- 使用线程池技术处理业务逻辑
- 线程之间的同步技术包括了互斥量与信号量
- 其他技术
  - 信号处理
  - 配置文件组件
  - 日志输出

### 借鉴Nginx

- 借鉴了一个master进程，搭配多个worker进程的进程框架

  master进程管理进程，以及信号处理，worker进程处理具体的业务逻辑与功能

- 借鉴了epoll相关代码，Nginx使用了边缘触发（ET），本框架使用水平触发（LT）
- 接收与发送数据包相关

------

### 项目结构

```cpp
.
├── README.md
├── _include //本项目全部的头文件
│   ├── Dota_Pool.h
│   ├── MemoryPool.h
│   ├── StackAlloc.h
│   ├── ngx_c_conf.h
│   ├── ngx_c_crc32.h
│   ├── ngx_c_lockmutex.h
│   ├── ngx_c_memory.h
│   ├── ngx_c_slogic.h
│   ├── ngx_c_socket.h
│   ├── ngx_c_threadpool.h
│   ├── ngx_comm.h
│   ├── ngx_func.h
│   ├── ngx_global.h
│   ├── ngx_logiccomm.h
│   └── ngx_macro.h
├── app //核心的文件,包括程序入口函数、设置进程标题和配置文件读取等
│   ├── makefile
│   ├── nginx.cxx
│   ├── ngx_c_conf.cxx
│   ├── ngx_log.cxx
│   ├── ngx_printf.cxx
│   ├── ngx_setproctitle.cxx
│   └── ngx_string.cxx
├── common.mk
├── config.mk
├── logic // 通信逻辑类的函数实现
│   ├── makefile
│   └── ngx_c_slogic.cxx
├── makefile
├── misc // 存放不便于归类的一些文件，如线程池函数实现、内存分配和校验码等
│   ├── Dota_Pool.cpp
│   ├── MemoryPool.cpp
│   ├── StackAlloc.cpp
│   ├── makefile
│   ├── ngx_c_crc32.cxx
│   ├── ngx_c_memory.cxx
│   └── ngx_c_threadpool.cxx
├── net //核心文件 基础通信类、建立连接、连接请求、连接超时等核心文件
│   ├── makefile
│   ├── ngx_c_socket.cxx
│   ├── ngx_c_socket_accept.cxx
│   ├── ngx_c_socket_conn.cxx
│   ├── ngx_c_socket_inet.cxx
│   ├── ngx_c_socket_request.cxx
│   └── ngx_c_socket_time.cxx
├── nginx.conf
├── proc // 存放进程相关的函数实现
│   ├── makefile
│   ├── ngx_daemon.cxx
│   ├── ngx_event.cxx
│   └── ngx_process_cycle.cxx
└── signal //存放信号处理相关的函数实现
    └── ngx_signal.cxx
```

### 流程