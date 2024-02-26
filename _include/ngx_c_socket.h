
#ifndef __NGX_SOCKET_H__
#define __NGX_SOCKET_H__

#include <vector>	   //vector
#include <list>		   //list
#include <sys/epoll.h> //epoll
#include <sys/socket.h>
#include <pthread.h>   //多线程
#include <semaphore.h> //信号量
#include <atomic>	   //c++11里的原子操作
#include <map>		   //multimap

#include "ngx_comm.h"

// 本文件使用的一些宏定义

// 已完成连接队列的最大已完成连接数
#define NGX_LISTEN_BACKLOG 511
// epoll_wait 单次接收的最大事件个数
#define NGX_MAX_EVENTS 512

// 结构体声明

// 监听端口相关结构体
typedef struct ngx_listening_s ngx_listening_t, *lpngx_listening_t;
// TCP 连接结构体
typedef struct ngx_connection_s ngx_connection_t, *lpngx_connection_t;
// socket 相关类
typedef class CSocekt CSocekt;

// 成员函数指针
typedef void (CSocekt::*ngx_event_handler_pt)(lpngx_connection_t c);

// 本文件专用的结构体定义

// 监听端口相关的结构体
struct ngx_listening_s
{
	// 监听的端口号
	int port;
	// 套接字句柄 socket
	int fd;
	// 连接池中的一个连接的指针
	lpngx_connection_t connection;
};

// 表示一个 TCP 连接的结构体（客户端主动发起的、服务器被动接受的TCP连接）
struct ngx_connection_s
{
	// 构造函数
	ngx_connection_s();
	// 析构函数
	virtual ~ngx_connection_s(); // 析构函数
	// 分配一个连接时进行初始化
	void GetOneToUse();
	// 回收一个连接时处理
	void PutOneToFree();

	// 套接字句柄
	int fd;
	// 当连接被分配给一个监听套接字时，指向对应的监听套接字的内存
	lpngx_listening_t listening;

	// 【位域】失效标志位：0：有效，1：失效 【这个是官方nginx提供，到底有什么用，ngx_epoll_process_events()中详解】
	// unsigned instance : 1;

	// 引入的一个序号，每次分配出去时 +1，可在一定程度上检测错包废包，具体使用方式后续展开
	uint64_t iCurrsequence;
	// 保存 TCP 连接的对方的套接字信息
	struct sockaddr s_sockaddr;
	// 保存 ip 地址的文本信息，F
	// char addr_text[100];

	// 和读有关的标志-----------------------
	// 保存读相关的标志
	// uint8_t                   r_ready;        //读准备好标记
	// uint8_t                   w_ready;        //写准备好标记

	// 读事件的处理方法
	ngx_event_handler_pt rhandler;
	// 写事件的处理方法
	ngx_event_handler_pt whandler;

	// epoll 事件相关
	uint32_t events;

	// 收包相关变量

	// 当前收包状态
	unsigned char curStat;
	// 保存数据包头信息
	char dataHeadInfo[_DATA_BUFSIZE_];
	// 接收数据的缓冲区的指针，用于暂存未收全的包
	char *precvbuf;
	// 收到数据的大小，和 precvbuf 配合使用
	unsigned int irecvlen;
	// 用于收包的内存首地址
	char *precvMemPointer;

	// 逻辑处理相关的互斥量，处理本链接发送的信息时需要互斥
	pthread_mutex_t logicPorcMutex;

	// 发包相关变量

	// 标记缓冲区满的变量
	std::atomic<int> iThrowsendCount;
	// 整个数据的头指针，指向 消息头 + 包头 + 包体，用于发送完成后释放内存
	char *psendMemPointer;
	// 发送数据的缓冲区的头指针，开始指向 包头+包体
	char *psendbuf;
	// 发送数据的大小
	unsigned int isendlen;

	// 回收相关变量

	// 入到资源回收站中的时间
	time_t inRecyTime;

	// 心跳包相关变量

	// 上次 ping 的时间（上次发送心跳包的事件）
	time_t lastPingTime;

	// 网络安全相关变量

	// Flood 攻击上次收到包的时间
	uint64_t FloodkickLastTime;
	// Flood 攻击在该时间内收到包的次数统计
	int FloodAttackCount;
	// 发送队列中有的数据条目数，若 client 只发不收，则可能造成此数过大，依据此数做出踢出处理
	std::atomic<int> iSendCount;

	// 指向下一个本类型对象的指针，可将空闲的连接池中的对象相连，构成一个单向链表，方便取用
	lpngx_connection_t next;
};

// 消息头结构体，额外记录收到数据包时的一些信息以备将来使用
typedef struct _STRUC_MSG_HEADER
{
	// 指向对应的 TCP 连接
	lpngx_connection_t pConn;

	// 收到数据包时，记录对应连接的序号，可用于将来比较连接是否废用
	uint64_t iCurrsequence;

} STRUC_MSG_HEADER, *LPSTRUC_MSG_HEADER;

// socket 类
class CSocekt
{
public:
	// 构造函数
	CSocekt();
	// 析构函数
	virtual ~CSocekt();
	// 父进程中的初始化
	virtual bool Initialize();
	// 子进程中的初始化
	virtual bool Initialize_subproc();
	// 关闭、退出函数，在子进程中执行
	virtual void Shutdown_subproc();

	// 输出统计信息
	void printTDInfo();

public:
	// 处理客户端请求函数
	virtual void threadRecvProcFunc(char *pMsgBuf);
	// 心跳包检测时间到，检测心跳包是否超时等事宜，本函数仅释放内存，子类应实现该函数的具体判断操作
	virtual void procPingTimeOutChecking(LPSTRUC_MSG_HEADER tmpmsg, time_t cur_time);

public:
	// epoll功能初始化
	int ngx_epoll_init();

	// epoll增加事件
	// int  ngx_epoll_add_event(int fd,int readevent,int writeevent,uint32_t otherflag,uint32_t eventtype,lpngx_connection_t pConn);

	// epoll等待接收和处理事件
	int ngx_epoll_process_events(int timer);

	// epoll操作事件
	int ngx_epoll_oper_event(int fd, uint32_t eventtype, uint32_t flag, int bcaction, lpngx_connection_t pConn);

protected:
	// 数据发送相关
	void msgSend(char *psendbuf);					   // 把数据扔到待发送对列中
	void zdClosesocketProc(lpngx_connection_t p_Conn); // 主动关闭一个连接时的要做些善后的处理函数

private:
	// 专门用于读各种配置项
	void ReadConf();
	// 监听必须的端口【支持多个端口】
	bool ngx_open_listening_sockets();
	// 关闭监听套接字
	void ngx_close_listening_sockets();
	// 设置非阻塞套接字
	bool setnonblocking(int sockfd);

	// 一些业务处理函数handler

	// 建立新连接
	void ngx_event_accept(lpngx_connection_t oldc);
	// 设置数据来时的读处理函数
	void ngx_read_request_handler(lpngx_connection_t pConn);
	// 设置数据发送时的写处理函数
	void ngx_write_request_handler(lpngx_connection_t pConn);
	// 通用连接关闭函数，资源用这个函数释放【因为这里涉及到好几个要释放的资源，所以写成函数】
	void ngx_close_connection(lpngx_connection_t pConn);

	// 接收从客户端来的数据专用函数
	ssize_t recvproc(lpngx_connection_t pConn, char *buff, ssize_t buflen);
	// 包头收完整后的处理，我们称为包处理阶段1：写成函数，方便复用
	void ngx_wait_request_handler_proc_p1(lpngx_connection_t pConn, bool &isflood);
	// 收到一个完整包后的处理，放到一个函数中，方便调用
	void ngx_wait_request_handler_proc_plast(lpngx_connection_t pConn, bool &isflood);

	// 处理发送消息队列
	void clearMsgSendQueue();

	ssize_t sendproc(lpngx_connection_t c, char *buff, ssize_t size); // 将数据发送到客户端

	// 获取对端信息相关
	size_t ngx_sock_ntop(struct sockaddr *sa, int port, u_char *text, size_t len); // 根据参数1给定的信息，获取地址端口字符串，返回这个字符串的长度

	// 连接池 或 连接 相关
	void initconnection();								// 初始化连接池
	void clearconnection();								// 回收连接池
	lpngx_connection_t ngx_get_connection(int isock);	// 从连接池中获取一个空闲连接
	void ngx_free_connection(lpngx_connection_t pConn); // 归还参数pConn所代表的连接到到连接池中
	void inRecyConnectQueue(lpngx_connection_t pConn);	// 将要回收的连接放到一个队列中来

	// 和时间相关的函数
	void AddToTimerQueue(lpngx_connection_t pConn);		  // 设置踢出时钟(向map表中增加内容)
	time_t GetEarliestTime();							  // 从multimap中取得最早的时间返回去
	LPSTRUC_MSG_HEADER RemoveFirstTimer();				  // 从m_timeQueuemap移除最早的时间，并把最早这个时间所在的项的值所对应的指针 返回，调用者负责互斥，所以本函数不用互斥，
	LPSTRUC_MSG_HEADER GetOverTimeTimer(time_t cur_time); // 根据给的当前时间，从m_timeQueuemap找到比这个时间更老（更早）的节点【1个】返回去，这些节点都是时间超过了，要处理的节点
	void DeleteFromTimerQueue(lpngx_connection_t pConn);  // 把指定用户tcp连接从timer表中抠出去
	void clearAllFromTimerQueue();						  // 清理时间队列中所有内容

	// 和网络安全有关
	bool TestFlood(lpngx_connection_t pConn); // 测试是否flood攻击成立，成立则返回true，否则返回false

	// 线程相关函数
	static void *ServerSendQueueThread(void *threadData);		  // 专门用来发送数据的线程
	static void *ServerRecyConnectionThread(void *threadData);	  // 专门用来回收连接的线程
	static void *ServerTimerQueueMonitorThread(void *threadData); // 时间队列监视线程，处理到期不发心跳包的用户踢出的线程

protected:
	// 一些和网络通讯有关的成员变量

	// 包头大小 sizeof(COMM_PKG_HEADER);
	size_t m_iLenPkgHeader;
	// 消息头大小 sizeof(STRUC_MSG_HEADER);
	size_t m_iLenMsgHeader;

	// 时间相关
	int m_ifTimeOutKick; // 当时间到达Sock_MaxWaitTime指定的时间时，直接把客户端踢出去，只有当Sock_WaitTimeEnable = 1时，本项才有用
	int m_iWaitTime;	 // 多少秒检测一次是否 心跳超时，只有当Sock_WaitTimeEnable = 1时，本项才有用

private:
	struct ThreadItem
	{
		pthread_t _Handle; // 线程句柄
		CSocekt *_pThis;   // 记录线程池的指针
		bool ifrunning;	   // 标记是否正式启动起来，启动起来后，才允许调用StopAll()来释放

		// 构造函数
		ThreadItem(CSocekt *pthis) : _pThis(pthis), ifrunning(false) {}
		// 析构函数
		~ThreadItem() {}
	};

	// epoll连接的最大项数
	int m_worker_connections;
	// 所监听的端口数量
	int m_ListenPortCount;
	// epoll_create返回的句柄，每个进程仅有一个
	int m_epollhandle;

	// 和连接池有关的

	// 连接池，存放全部连接，每个连接代表一个封装后的 TCP 连接
	std::list<lpngx_connection_t> m_connectionList;
	// 空闲连接列表，存放全部空闲的连接
	std::list<lpngx_connection_t> m_freeconnectionList;
	// 连接池总连接数
	std::atomic<int> m_total_connection_n;
	// 空闲连接数
	std::atomic<int> m_free_connection_n;
	// 连接相关互斥量，用于互斥 m_freeconnectionList，m_connectionList
	pthread_mutex_t m_connectionMutex;
	// 连接回收队列相关的互斥量
	pthread_mutex_t m_recyconnqueueMutex;
	// 将要释放的连接放这里
	std::list<lpngx_connection_t> m_recyconnectionList;
	// 待释放连接队列大小
	std::atomic<int> m_totol_recyconnection_n;
	// 回收连接延迟时间
	int m_RecyConnectionWaitTime;

	// lpngx_connection_t             m_pfree_connections;                //空闲连接链表头，连接池中总是有某些连接被占用，为了快速在池中找到一个空闲的连接，我把空闲的连接专门用该成员记录;
	// 【串成一串，其实这里指向的都是m_pconnections连接池里的没有被使用的成员】

	// 监听套接字队列，存放全部用于监听各个端口的封装后的套接字
	std::vector<lpngx_listening_t> m_ListenSocketList;
	// 存储 epoll_wait() 返回的事件
	struct epoll_event m_events[NGX_MAX_EVENTS];

	// 消息队列

	std::list<char *> m_MsgSendQueue;	   // 发送数据消息队列
	std::atomic<int> m_iSendMsgQueueCount; // 发消息队列大小

	// 多线程相关

	// 存储封装后的线程对象
	std::vector<ThreadItem *> m_threadVector;
	// 发消息队列互斥量
	pthread_mutex_t m_sendMessageQueueMutex;
	// 处理发消息线程相关的信号量
	sem_t m_semEventSendQueue;

	// 时间相关
	int m_ifkickTimeCount;									   // 是否开启踢人时钟，1：开启   0：不开启
	pthread_mutex_t m_timequeueMutex;						   // 和时间队列有关的互斥量
	std::multimap<time_t, LPSTRUC_MSG_HEADER> m_timerQueuemap; // 时间队列
	size_t m_cur_size_;										   // 时间队列的尺寸
	time_t m_timer_value_;									   // 当前计时队列头部时间值

	// 在线用户相关

	// 当前在线用户数统计
	std::atomic<int> m_onlineUserCount;

	// 网络安全相关
	int m_floodAkEnable;			  // Flood攻击检测是否开启,1：开启   0：不开启
	unsigned int m_floodTimeInterval; // 表示每次收到数据包的时间间隔是100(毫秒)
	int m_floodKickCount;			  // 累积多少次踢出此人

	// 统计用途
	time_t m_lastprintTime;		// 上次打印统计信息的时间(10秒钟打印一次)
	int m_iDiscardSendPkgCount; // 丢弃的发送数据包数量
};

#endif
