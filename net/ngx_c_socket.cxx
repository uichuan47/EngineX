
// 本文件存放和网络相关的类的函数实现

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    //uintptr_t
#include <stdarg.h>    //va_start....
#include <unistd.h>    //STDERR_FILENO等
#include <sys/time.h>  //gettimeofday
#include <time.h>      //localtime_r
#include <fcntl.h>     //open
#include <errno.h>     //errno
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"

/***************************************************************
 *  @brief     构造函数，初始化相关变量
 **************************************************************/
CSocekt::CSocekt()
{
    // 配置相关变量初始化

    // epoll 连接最大项数
    m_worker_connections = 1;
    // 监听一个端口
    m_ListenPortCount = 1;
    // 延迟回收连接时间
    m_RecyConnectionWaitTime = 60;

    // epoll相关

    // epoll返回的句柄
    m_epollhandle = -1;
    // m_pconnections = NULL;       //连接池【连接数组】先给空
    // m_pfree_connections = NULL;  //连接池中空闲的连接链
    // m_pread_events = NULL;       //读事件数组给空
    // m_pwrite_events = NULL;      //写事件数组给空

    // 网络通讯有关的常用变量值，供后续频繁使用时提高效率

    // 包头所占空间大小
    m_iLenPkgHeader = sizeof(COMM_PKG_HEADER);
    // 消息头所占空间大小
    m_iLenMsgHeader = sizeof(STRUC_MSG_HEADER);

    // 多线程相关
    // pthread_mutex_init(&m_recvMessageQueueMutex, NULL); //互斥量初始化

    // 队列相关变量初始化

    // 发消息队列大小
    m_iSendMsgQueueCount = 0;
    // 待释放连接队列大小
    m_totol_recyconnection_n = 0;
    // 当前计时队列尺寸
    m_cur_size_ = 0;
    // 当前计时队列头部的时间值
    m_timer_value_ = 0;
    // 丢弃的发送数据包数量
    m_iDiscardSendPkgCount = 0;

    // 在线用户相关变量

    // 在线用户数量统计，先给0
    m_onlineUserCount = 0;
    // 上次打印统计信息的时间，先给 0
    m_lastprintTime = 0;

    return;
}

/***************************************************************
 *  @brief     析构函数
 *  @note      释放监听队列的内存
 **************************************************************/
CSocekt::~CSocekt()
{
    // 释放必须的内存
    // 监听端口相关内存的释放--------
    for (auto pos = m_ListenSocketList.begin(); pos != m_ListenSocketList.end(); ++pos) // vector
    {
        // 释放内存
        delete (*pos);
    } // end for

    // 清空队列
    m_ListenSocketList.clear();
    return;
}

/***************************************************************
 *  @brief     读取配置文件中的相关配置信息
 **************************************************************/
void CSocekt::ReadConf()
{

    CConfig *p_config = CConfig::GetInstance();

    // epoll连接的最大项数
    m_worker_connections = p_config->GetIntDefault("worker_connections", m_worker_connections);
    // 取得要监听的端口数量
    m_ListenPortCount = p_config->GetIntDefault("ListenPortCount", m_ListenPortCount);
    // 延迟回收连接时间
    m_RecyConnectionWaitTime = p_config->GetIntDefault("Sock_RecyConnectionWaitTime", m_RecyConnectionWaitTime);

    // 是否开启踢人时钟，1：开启   0：不开启
    m_ifkickTimeCount = p_config->GetIntDefault("Sock_WaitTimeEnable", 0);
    // 多少秒检测一次是否 心跳超时，只有当Sock_WaitTimeEnable = 1时，本项才有用
    m_iWaitTime = p_config->GetIntDefault("Sock_MaxWaitTime", m_iWaitTime);
    // 不建议低于5秒钟，因为无需太频繁
    m_iWaitTime = (m_iWaitTime > 5) ? m_iWaitTime : 5;
    // 当时间到达Sock_MaxWaitTime指定的时间时，直接把客户端踢出去，只有当Sock_WaitTimeEnable = 1时，本项才有用
    m_ifTimeOutKick = p_config->GetIntDefault("Sock_TimeOutKick", 0);

    // Flood攻击检测是否开启,1：开启   0：不开启
    m_floodAkEnable = p_config->GetIntDefault("Sock_FloodAttackKickEnable", 0);
    // 表示每次收到数据包的时间间隔是100(毫秒)
    m_floodTimeInterval = p_config->GetIntDefault("Sock_FloodTimeInterval", 100);
    // 累积多少次踢出此人
    m_floodKickCount = p_config->GetIntDefault("Sock_FloodKickCounter", 10);

    return;
}

/***************************************************************
 *  @brief     初始化函数，在 fork() 子进程之前调用
 *  @return    成功返回true，失败返回false
 **************************************************************/
bool CSocekt::Initialize()
{
    // 读配置项信息
    ReadConf();

    // 打开全部监听端口，并加入监听队列
    if (ngx_open_listening_sockets() == false)
    {
        return false;
    }

    return true;
}

/***************************************************************
 *  @brief     打开监听端口，为每一个端口创建对应的信箱和信标签，封装后加入监听队列，在创建 worker 进程之前执行
 *  @return    成功返回 true，失败返回 false
 **************************************************************/
bool CSocekt::ngx_open_listening_sockets()
{
    int isock;                    // socket
    struct sockaddr_in serv_addr; // 服务器的地址结构体
    int iport;                    // 端口
    char strinfo[100];            // 临时字符串

    // 初始化相关

    // 清空地址
    memset(&serv_addr, 0, sizeof(serv_addr));
    // 选择协议族为IPV4
    serv_addr.sin_family = AF_INET;
    // 监听本地所有的IP地址；INADDR_ANY表示的是一个服务器上所有的网卡（服务器可能不止一个网卡）多个本地ip地址都进行绑定端口号，进行侦听
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // 中途使用配置信息
    CConfig *p_config = CConfig::GetInstance();

    // 循环设置每一个监听端口
    for (int i = 0; i < m_ListenPortCount; i++)
    {
        // 获取一个 socket 句柄（一个信箱）
        // 参数1：AF_INET：使用ipv4协议，一般就这么写
        // 参数2：SOCK_STREAM：使用TCP，表示可靠连接【相对还有一个UDP套接字，表示不可靠连接】
        // 参数3：给0，固定用法，就这么记
        isock = socket(AF_INET, SOCK_STREAM, 0); // 系统函数，成功返回非负描述符，出错返回-1

        if (isock == -1)
        {
            // 失败则直接退出，输出错误到标准输出
            ngx_log_stderr(errno, "CSocekt::Initialize()中socket()失败,i=%d.", i);
            // 其实这里直接退出，那如果以往有成功创建的socket呢？就没得到释放吧，当然走到这里表示程序不正常，应该整个退出，也没必要释放了
            return false;
        }

        // setsockopt() 设置套接字参数选项（信箱的属性）
        // 参数1：待设置的信箱
        // 参数2：是表示级别，和参数3配套使用，即参数3如果确定，参数2也确定了
        // 参数3：是否允许重用本地地址
        // 设置 SO_REUSEADDR，目的第五章第三节讲解的非常清楚：主要是解决TIME_WAIT这个状态导致bind()失败的问题
        int reuseaddr = 1; // 1:打开对应的设置项
        if (setsockopt(isock, SOL_SOCKET, SO_REUSEADDR, (const void *)&reuseaddr, sizeof(reuseaddr)) == -1)
        {
            // 错误直接输出
            ngx_log_stderr(errno, "CSocekt::Initialize()中setsockopt(SO_REUSEADDR)失败,i=%d.", i);
            close(isock); // 无需理会是否正常执行了
            return false;
        }

        // 为处理惊群问题使用reuseport

        int reuseport = 1;
        if (setsockopt(isock, SOL_SOCKET, SO_REUSEPORT, (const void *)&reuseport, sizeof(int)) == -1) // 端口复用需要内核支持
        {
            
            ngx_log_stderr(errno, "CSocekt::Initialize()中setsockopt(SO_REUSEPORT)失败", i);
        }

        // 设置该 socket 为非阻塞（信箱）
        if (setnonblocking(isock) == false)
        {
            ngx_log_stderr(errno, "CSocekt::Initialize()中setnonblocking()失败,i=%d.", i);
            close(isock);
            return false;
        }

        // 设置本服务器要监听的地址和端口，这样客户端才能连接到该地址和端口并发送数据（信标签）
        strinfo[0] = 0;
        // 待查找的端口序号
        sprintf(strinfo, "ListenPort%d", i);
        // 查找对应序号的端口值
        iport = p_config->GetIntDefault(strinfo, 10000);
        // 将查找到的端口写到“信标签”上
        serv_addr.sin_port = htons((in_port_t)iport); // in_port_t其实就是uint16_t

        // 绑定服务器地址结构体，绑定信箱和信标签
        if (bind(isock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        {
            ngx_log_stderr(errno, "CSocekt::Initialize()中bind()失败,i=%d.", i);
            close(isock);
            return false;
        }

        // 信箱 socket 上已经贴好了标签，可以开始准备监听了

        // 开始监听
        if (listen(isock, NGX_LISTEN_BACKLOG) == -1)
        {
            ngx_log_stderr(errno, "CSocekt::Initialize()中listen()失败,i=%d.", i);
            close(isock);

            return false;
        }

        // 完成监听，封装后加入监听队列

        // 创建一个监听的指针
        lpngx_listening_t p_listensocketitem = new ngx_listening_t;
        // 清空
        memset(p_listensocketitem, 0, sizeof(ngx_listening_t));
        // 初始化这个监听结构体指针，端口号，套接字句柄
        p_listensocketitem->port = iport;
        p_listensocketitem->fd = isock;
        // 记录成功日志
        ngx_log_error_core(NGX_LOG_INFO, 0, "监听%d端口成功!", iport);
        // 加入到监听队列
        m_ListenSocketList.push_back(p_listensocketitem);
    }

    // 检查列表是否为空
    if (m_ListenSocketList.size() <= 0)
    {
        return false;
    }

    return true;
}

/***************************************************************
 *  @brief     epoll 功能初始化，创建一个 epoll 对象，并为监听套接字依次分配一个 TCP 连接后加入到 epoll 中
 *  @return    返回值
 *  @note      在子进程中进行，被 ngx_worker_process_init() 调用
 **************************************************************/
int CSocekt::ngx_epoll_init()
{
    // 很多内核版本不处理 epoll_create 的参数，只要该参数 >0 即可
    // 创建一个epoll对象，其中包含一个红黑树和一个双向链表

    // 直接以epoll连接的最大项数为参数，肯定 > 0
    m_epollhandle = epoll_create(m_worker_connections);
    if (m_epollhandle == -1)
    {
        // 创建失败直接退出
        ngx_log_stderr(errno, "CSocekt::ngx_epoll_init()中epoll_create()失败.");
        exit(2);
    }

    // 创建连接池【数组】，后续用于处理所有客户端的连接
    initconnection();

    // m_connection_n = m_worker_connections; // 记录当前连接池中连接总数
    // // 连接池【数组，每个元素是一个对象】
    // m_pconnections = new ngx_connection_t[m_connection_n]; // new不可以失败，不用判断结果，如果失败直接报异常更好一些

    // int i = m_connection_n; // 连接池中连接数
    // lpngx_connection_t next = NULL;
    // lpngx_connection_t c = m_pconnections; // 连接池数组首地址
    // do
    // {
    //     i--; // 注意i是数字的末尾，从最后遍历，i递减至数组首个元素

    //     // 好从屁股往前来---------
    //     c[i].data = next;       // 设置连接对象的next指针，注意第一次循环时next = NULL;
    //     c[i].fd = -1;           // 初始化连接，无socket和该连接池中的连接【对象】绑定
    //     c[i].instance = 1;      // 失效标志位设置为1【失效】，此句抄自官方nginx，这句到底有啥用，后续再研究
    //     c[i].iCurrsequence = 0; // 当前序号统一从0开始
    //     //----------------------

    //     next = &c[i];                     // next指针前移
    // } while (i);                          // 循环直至i为0，即数组首地址
    // m_pfree_connections = next;           // 设置空闲连接链表头指针,因为现在next指向c[0]，注意现在整个链表都是空的
    // m_free_connection_n = m_connection_n; // 空闲连接链表长度，因为现在整个链表都是空的，这两个长度相等；

    // 遍历所有监听socket【监听端口】，我们为每个监听socket增加一个 连接池中的连接
    // 【说白了就是让一个socket和一个内存绑定，以方便记录该sokcet相关的数据、状态等等】

    // 遍历数组中每一个监听连接，为其配置一个连接池中的连接，方便管理其数据、状态等
    for (auto pos = m_ListenSocketList.begin(); pos != m_ListenSocketList.end(); ++pos)
    {
        lpngx_connection_t p_Conn = ngx_get_connection((*pos)->fd); // 从连接池中获取一个空闲连接对象
        if (p_Conn == NULL)
        {
            // 致命问题，初始状态连接池怎么可能为空呢？
            ngx_log_stderr(errno, "CSocekt::ngx_epoll_init()中ngx_get_connection()失败.");
            exit(2); // 这是致命问题了，直接退，资源由系统释放吧，这里不刻意释放了，比较麻烦
        }

        // 将创建出的连接对象和原有的监听对象相互关联

        // 连接对象 和监听对象关联，方便通过连接对象找监听对象
        p_Conn->listening = (*pos);
        // 监听对象 和连接对象关联，方便通过监听对象找连接对象
        (*pos)->connection = p_Conn;
        // 监听端口必须设置accept标志为1,这个是否有必要，再研究
        // rev->accept = 1;

        // 对监听端口的读事件设置处理方法，因为监听端口是用来等对方连接的发送三路握手的，所以监听端口关心的就是读事件
        p_Conn->rhandler = &CSocekt::ngx_event_accept;


        // 往监听socket上增加监听事件，从而开始让监听端口履行其职责【如果不加这行，虽然端口能连上，但不会触发ngx_epoll_process_events()里边的epoll_wait()往下走】
        /*if(ngx_epoll_add_event((*pos)->fd,       //socekt句柄
                                1,0,             //读，写【只关心读事件，所以参数2：readevent=1,而参数3：writeevent=0】
                                0,               //其他补充标记
                                EPOLL_CTL_ADD,   //事件类型【增加，还有删除/修改】
                                p_Conn           //连接池中的连接
                                ) == -1)
                                */

        if (ngx_epoll_oper_event(
                (*pos)->fd,           // socekt句柄
                EPOLL_CTL_ADD,        // 事件类型，这里是增加
                EPOLLIN | EPOLLRDHUP, // 标志，这里代表要增加的标志，EPOLLIN：可读，EPOLLRDHUP：TCP连接的远端关闭或者半关闭
                0,                    // 对于事件类型为增加的，不需要这个参数
                p_Conn                // 连接池中的连接
                ) == -1)
        {
            exit(2); // 有问题，直接退出，日志 已经写过了
        }

    } // end for

    return 1;
}

/***************************************************************
 *  @brief     对 epoll 对象具体操作，增加、修改或删除对象中的连接或连接的事件
 *  @param     fd           一个 socket 句柄，即 pConn 对应的 TCP 连接的句柄
 *  @param     eventtype    事件类型，一般是 EPOLL_CTL_ADD，EPOLL_CTL_MOD，EPOLL_CTL_DEL，分别对应操作 epoll 红黑树的节点（增加，修改，删除）
 *  @param     flag         标志，具体含义取决于 eventtype
 *  @param     bcaction     补充动作，补充 flag 标记的不足：0 增加，1 去掉，2 完全覆盖，eventtype 是 EPOLL_CTL_MOD 时这个参数就有用
 *  @param     pConn        一个 TCP 连接的指针，EPOLL_CTL_ADD 时增加到红黑树中去，将来 epoll_wait 时可被取出使用
 *  @return    成功返回 1，失败返回 -1
 **************************************************************/
int CSocekt::ngx_epoll_oper_event(int fd, uint32_t eventtype, uint32_t flag, int bcaction, lpngx_connection_t pConn)
{
    // 临时变量，一个 epoll_event 事件，对待编辑的 TCP 连接和对连接的编辑选项进行封装，将来用于加入到红黑树中
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));

    // 增加节点
    if (eventtype == EPOLL_CTL_ADD)
    {
        // 红黑树从无到有增加节点
        // ev.data.ptr = (void *)pConn;

        // 待增加事件记录标记
        ev.events = flag;
        // 连接本身记录这个标记
        pConn->events = flag;
    }
    // 修改节点
    else if (eventtype == EPOLL_CTL_MOD)
    {
        // 节点已经在红黑树中，修改节点的事件信息

        // 先将当前连接的标记赋给临时变量
        ev.events = pConn->events;

        // 增加标记
        if (bcaction == 0)
        {
            ev.events |= flag;
        }
        // 删除标记
        else if (bcaction == 1)
        {
            ev.events &= ~flag;
        }
        // 完全覆盖
        else
        {
            ev.events = flag;
        }

        // 将修改后的标记赋值给原链接
        pConn->events = ev.events;
    }
    // 删除节点
    else
    {
        // 删除红黑树中节点，目前没这个需求
        // socket关闭这项会自动从红黑树移除
        return 1; // 先直接返回1表示成功
    }

    // 原来的理解中，绑定ptr这个事，只在EPOLL_CTL_ADD的时候做一次即可，但是发现EPOLL_CTL_MOD似乎会破坏掉.data.ptr，因此不管是EPOLL_CTL_ADD，还是EPOLL_CTL_MOD，都给进去
    // 找了下内核源码SYSCALL_DEFINE4(epoll_ctl, int, epfd, int, op, int, fd,		struct epoll_event __user *, event)，感觉真的会覆盖掉：
    // copy_from_user(&epds, event, sizeof(struct epoll_event)))，感觉这个内核处理这个事情太粗暴了

    // epoll_event 事件中的数据指针赋值为当前连接，无论什么操作，都需要重新赋值
    ev.data.ptr = (void *)pConn;

    // 将 ev 中保存的信息，按照 eventtype 的方式，加入到 m_epollhandle 指向的 epoll 中，连接的句柄为 fd
    if (epoll_ctl(m_epollhandle, eventtype, fd, &ev) == -1)
    {
        ngx_log_stderr(errno, "CSocekt::ngx_epoll_oper_event()中epoll_ctl(%d,%ud,%ud,%d)失败.", fd, eventtype, flag, bcaction);
        return -1;
    }

    return 1;
}

/***************************************************************
 *  @brief     开始获取发生的时间消息
 *  @param     timer    epoll_wait() 阻塞时长，单位毫秒
 *  @return    1 正常返回 ，0 有问题返回
 *  @note      本函数是子进程处理事件的核心函数，会不断被调用
 **************************************************************/
int CSocekt::ngx_epoll_process_events(int timer)
{
    /***************************************************************
     *  @brief     等待事件，将事件返回，可能会返回多个时间，也不返回任何事件，取决于是否有事件发生
     *  @param     m_epollhandle    epoll 对象句柄，事件来源
     *  @param     m_events    存储返回事件的内存
     *  @param     NGX_MAX_EVENTS    最大返回事件数
     *  @param     timer    -1: 保持阻塞，0: 立即返回
     *  @return    -1: 错误，0: 等待超时，>0: 成功获得事件的个数
     *  @note      调用示例
     **************************************************************/
    int events = epoll_wait(m_epollhandle, m_events, NGX_MAX_EVENTS, timer);

    // 发生错误，如收到信号
    if (events == -1)
    {
        // 有错误发生，发送某个信号给本进程就可以导致这个条件成立，而且错误码根据观察是4；
        // #define EINTR  4，
        // EINTR错误的产生：当阻塞于某个慢系统调用的一个进程捕获某个信号且相应信号处理函数返回时，该系统调用可能返回一个EINTR错误。
        // 例如：在socket服务器端，设置了信号捕获机制，有子进程，
        // 当在父进程阻塞于慢系统调用时由父进程捕获到了一个有效信号时，内核会致使accept返回一个EINTR错误(被中断的系统调用)。

        // 错误来源是收到信号
        if (errno == EINTR)
        {
            // 一般情况下不应当收到信号，因此输出错误信息到日志
            ngx_log_error_core(NGX_LOG_INFO, errno, "CSocekt::ngx_epoll_process_events()中epoll_wait()失败!");
            // 正常返回
            return 1;
        }
        // 其余错误来源，表名确实存在错误
        else
        {
            // 这被认为应该是有问题，记录日志
            ngx_log_error_core(NGX_LOG_ALERT, errno, "CSocekt::ngx_epoll_process_events()中epoll_wait()失败!");
            // 错误返回
            return 0;
        }
    }

    // 等待超时，无事件到来
    if (events == 0)
    {
        // 指定了等待时长
        if (timer != -1)
        {
            // 等待时间到，无事件，正常返回
            return 1;
        }

        // timer == -1 表示无限等待，阻塞，不应当返回 0 ，存在问题
        ngx_log_error_core(NGX_LOG_ALERT, 0, "CSocekt::ngx_epoll_process_events()中epoll_wait()没超时却没返回任何事件!");
        // 非正常返回
        return 0;
    }

    // 会惊群，一个telnet上来，4个worker进程都会被惊动，都执行下边这个
    // ngx_log_stderr(0,"惊群测试:events=%d,进程id=%d",events,ngx_pid);
    // ngx_log_stderr(0,"----------------------------------------");

    // 处理完成特殊情况，开始处理事件，

    // 走到这里，就是属于有事件收到了

    // 临时变量，一个 TCP 连接
    lpngx_connection_t p_Conn;
    // 临时变量
    uint32_t revents;

    // uintptr_t          instance;

    // 根据返回的事件数量，遍历本次 epoll_wait 返回的所有事件，依次处理
    for (int i = 0; i < events; ++i)
    {
        // 将事件内的 TCP 连接取出
        p_Conn = (lpngx_connection_t)(m_events[i].data.ptr);

        // instance = (uintptr_t)c & 1;
        // // 将地址的最后一位取出来，用instance变量标识, 见ngx_epoll_add_event，该值是当时随着连接池中的连接一起给进来的
        // // 取得的是你当时调用ngx_epoll_add_event()的时候，这个连接里边的instance变量的值；
        // p_Conn = (lpngx_connection_t)((uintptr_t)p_Conn & (uintptr_t)~1); // 最后1位干掉，得到真正的c地址

        // // 仔细分析一下官方nginx的这个判断
        // // 过滤过期事件的；
        // if (c->fd == -1) // 一个套接字，当关联一个 连接池中的连接【对象】时，这个套接字值是要给到c->fd的，
        //                  // 那什么时候这个c->fd会变成-1呢？关闭连接时这个fd会被设置为-1，哪行代码设置的-1再研究，但应该不是ngx_free_connection()函数设置的-1
        // {
        //     // 比如我们用epoll_wait取得三个事件，处理第一个事件时，因为业务需要，我们把这个连接关闭，那我们应该会把c->fd设置为-1；
        //     // 第二个事件照常处理
        //     // 第三个事件，假如这第三个事件，也跟第一个事件对应的是同一个连接，那这个条件就会成立；那么这种事件，属于过期事件，不该处理

        //     // 这里可以增加个日志，也可以不增加日志
        //     ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_process_events()中遇到了fd=-1的过期事件:%p.", c);
        //     continue; // 这种事件就不处理即可
        // }

        // // 过滤过期事件的；
        // if (c->instance != instance)
        // {
        //     //--------------------以下这些说法来自于资料--------------------------------------
        //     // 什么时候这个条件成立呢？【换种问法：instance标志为什么可以判断事件是否过期呢？】
        //     // 比如我们用epoll_wait取得三个事件，处理第一个事件时，因为业务需要，我们把这个连接关闭【麻烦就麻烦在这个连接被服务器关闭上了】，但是恰好第三个事件也跟这个连接有关；
        //     // 因为第一个事件就把socket连接关闭了，显然第三个事件我们是不应该处理的【因为这是个过期事件】，若处理肯定会导致错误；
        //     // 那我们上述把c->fd设置为-1，可以解决这个问题吗？ 能解决一部分问题，但另外一部分不能解决，不能解决的问题描述如下【这么离奇的情况应该极少遇到】：

        //     // a)处理第一个事件时，因为业务需要，我们把这个连接【假设套接字为50】关闭，同时设置c->fd = -1;并且调用ngx_free_connection将该连接归还给连接池；
        //     // b)处理第二个事件，恰好第二个事件是建立新连接事件，调用ngx_get_connection从连接池中取出的连接非常可能就是刚刚释放的第一个事件对应的连接池中的连接；
        //     // c)又因为a中套接字50被释放了，所以会被操作系统拿来复用，复用给了b)【一般这么快就被复用也是醉了】；
        //     // d)当处理第三个事件时，第三个事件其实是已经过期的，应该不处理，那怎么判断这第三个事件是过期的呢？ 【假设现在处理的是第三个事件，此时这个 连接池中的该连接 实际上已经被用作第二个事件对应的socket上了】；
        //     // 依靠instance标志位能够解决这个问题，当调用ngx_get_connection从连接池中获取一个新连接时，我们把instance标志位置反，所以这个条件如果不成立，说明这个连接已经被挪作他用了；

        //     //--------------------我的个人思考--------------------------------------
        //     // 如果收到了若干个事件，其中连接关闭也搞了多次，导致这个instance标志位被取反2次，那么，造成的结果就是：还是有可能遇到某些过期事件没有被发现【这里也就没有被continue】，照旧被当做没过期事件处理了；
        //     // 如果是这样，那就只能被照旧处理了。可能会造成偶尔某个连接被误关闭？但是整体服务器程序运行应该是平稳，问题不大的，这种漏网而被当成没过期来处理的的过期事件应该是极少发生的

        //     ngx_log_error_core(NGX_LOG_DEBUG, 0, "CSocekt::ngx_epoll_process_events()中遇到了instance值改变的过期事件:%p.", c);
        //     continue; // 这种事件就不处理即可
        // }
        // // 存在一种可能性，过期事件没被过滤完整【非常极端】，走下来的；

        // 能走到这里，我们认为这些事件都没过期，就正常开始处理

        // 将事件取出准备处理
        revents = m_events[i].events;

        /*
        if(revents & (EPOLLERR|EPOLLHUP)) //例如对方close掉套接字，这里会感应到【换句话说：如果发生了错误或者客户端断连】
        {
            //这加上读写标记，方便后续代码处理，至于怎么处理，后续再说，这里也是参照nginx官方代码引入的这段代码；
            //官方说法：if the error events were returned, add EPOLLIN and EPOLLOUT，to handle the events at least in one active handler
            //我认为官方也是经过反复思考才加上着东西的，先放这里放着吧；
            revents |= EPOLLIN|EPOLLOUT;   //EPOLLIN：表示对应的链接上有数据可以读出（TCP链接的远端主动关闭连接，也相当于可读事件，因为本服务器小处理发送来的FIN包）
                                           //EPOLLOUT：表示对应的连接上可以写入数据发送【写准备好】
        } */

        // 读事件
        if (revents & EPOLLIN)
        {
            // ngx_log_stderr(errno,"数据来了来了来了 ~~~~~~~~~~~~~.");
            // 一个客户端新连入，这个会成立，
            // 已连接发送数据来，这个也成立；
            // c->r_ready = 1;      // 标记可以读；【从连接池拿出一个连接时这个连接的所有成员都是0】

            // 通过 this 指针调用 TCP 连接设置的读事件处理函数，将本 TCP 连接作为参数传入
            (this->*(p_Conn->rhandler))(p_Conn);
            // 注意括号的运用来正确设置优先级，防止编译出错
            // 如果新连接进入，这里执行的应该是 CSocekt::ngx_event_accept(c)
            // 如果是已经连入，发送数据到这里，则这里执行的应该是 CSocekt::ngx_read_request_handler()
        }

        // 写事件
        if (revents & EPOLLOUT)
        // 如果是写事件【对方关闭连接也触发这个，再研究。。。。。。】，
        // 注意上边的 if(revents & (EPOLLERR|EPOLLHUP))  revents |= EPOLLIN|EPOLLOUT; 读写标记都给加上了
        {
            // ngx_log_stderr(errno,"22222222222222222222.");

            if (revents & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) // 客户端关闭，如果服务器端挂着一个写通知事件，则这里个条件是可能成立的
            {
                // EPOLLERR：对应的连接发生错误                     8     = 1000
                // EPOLLHUP：对应的连接被挂起                       16    = 0001 0000
                // EPOLLRDHUP：表示TCP连接的远端关闭或者半关闭连接   8192   = 0010  0000   0000   0000
                // 我想打印一下日志看一下是否会出现这种情况
                // 8221 = ‭0010 0000 0001 1101‬  ：包括 EPOLLRDHUP ，EPOLLHUP， EPOLLERR
                // ngx_log_stderr(errno,"CSocekt::ngx_epoll_process_events()中revents&EPOLLOUT成立并且revents & (EPOLLERR|EPOLLHUP|EPOLLRDHUP)成立,event=%ud。",revents);

                // 我们只有投递了 写事件，但对端断开时，程序流程才走到这里，投递了写事件意味着 iThrowsendCount标记肯定被+1了，这里我们减回
                --p_Conn->iThrowsendCount;
            }
            else
            {
                (this->*(p_Conn->whandler))(p_Conn);
                // 如果有数据没有发送完毕，由系统驱动来发送，则这里执行的应该是 CSocekt::ngx_write_request_handler()
            }
        }

    } // end for(int i = 0; i < events; ++i)

    return 1;
}

/***************************************************************
 *  @brief     主动关闭一个 TCP 连接
 *  @param     p_Conn    待关闭连接
 *  @note      即便被多线程调用，也不影响本服务器程序的稳定性和正确运行性
 **************************************************************/
void CSocekt::zdClosesocketProc(lpngx_connection_t p_Conn)
{
    // 是否开启踢人时钟
    if (m_ifkickTimeCount == 1)
    {
        // 从时间队列中删除连接
        DeleteFromTimerQueue(p_Conn);
    }

    // 句柄未关闭
    if (p_Conn->fd != -1)
    {
        // 这个socket关闭，关闭后epoll就会被从红黑树中删除，所以这之后无法收到任何epoll事件
        close(p_Conn->fd);
        p_Conn->fd = -1;
    }

    // 如果缓冲区仍存在数据
    if (p_Conn->iThrowsendCount > 0)
    {
        // 直接清空
        --p_Conn->iThrowsendCount;
    }

    // 将待回收连接放入待回收队列中
    inRecyConnectQueue(p_Conn);

    return;
}

/***************************************************************
 *  @brief     关闭监听端口绑定的 socket 套接字
 **************************************************************/
void CSocekt::ngx_close_listening_sockets()
{
    // 遍历监听队列中全部监听链接
    for (int i = 0; i < m_ListenPortCount; i++)
    {
        // ngx_log_stderr(0,"端口是%d,socketid是%d.",m_ListenSocketList[i]->port,m_ListenSocketList[i]->fd);

        // 关闭连接的套接字
        close(m_ListenSocketList[i]->fd);
        // 输出日志
        ngx_log_error_core(NGX_LOG_INFO, 0, "关闭监听端口%d!", m_ListenSocketList[i]->port);
    } // end for(int i = 0; i < m_ListenPortCount; i++)
    return;
}

/***************************************************************
 *  @brief     设置 socket 连接为非阻塞，调用系统函数
 *  @param     sockfd    待设置 socket 句柄
 *  @return    true: 成功，false: 失败
 *  @note      调用示例
 **************************************************************/
bool CSocekt::setnonblocking(int sockfd)
{
    // 0：清除，1：设置
    int nb = 1;
    // FIONBIO：设置/清除非阻塞I/O标记：0：清除，1：设置
    if (ioctl(sockfd, FIONBIO, &nb) == -1)
    {
        return false;
    }

    return true;

    // 如下也是一种写法，跟上边这种写法其实是一样的，但上边的写法更简单
    /*
    //fcntl:file control【文件控制】相关函数，执行各种描述符控制操作
    //参数1：所要设置的描述符，这里是套接字【也是描述符的一种】
    int opts = fcntl(sockfd, F_GETFL);  //用F_GETFL先获取描述符的一些标志信息
    if(opts < 0)
    {
        ngx_log_stderr(errno,"CSocekt::setnonblocking()中fcntl(F_GETFL)失败.");
        return false;
    }
    opts |= O_NONBLOCK; //把非阻塞标记加到原来的标记上，标记这是个非阻塞套接字【如何关闭非阻塞呢？opts &= ~O_NONBLOCK,然后再F_SETFL一下即可】
    if(fcntl(sockfd, F_SETFL, opts) < 0)
    {
        ngx_log_stderr(errno,"CSocekt::setnonblocking()中fcntl(F_SETFL)失败.");
        return false;
    }
    return true;
    */
}

/***************************************************************
 *  @brief     打印服务器工作信息，输出到标准输出
 *  @note      调用示例
 **************************************************************/
void CSocekt::printTDInfo()
{
    // return;
    // 获取当前时间
    time_t currtime = time(NULL);
    // 控制打印频率
    if ((currtime - m_lastprintTime) > 10)
    {
        // 超过10秒我们打印一次

        // 收消息队列大小
        int tmprmqc = g_threadpool.getRecvMsgQueueCount();

        // 更新上次打印时间
        m_lastprintTime = currtime;
        // 当前在线人数
        int tmpoLUC = m_onlineUserCount;
        // 发消息队列大小
        int tmpsmqc = m_iSendMsgQueueCount;

        // 输出
        ngx_log_stderr(0, "------------------------------------begin--------------------------------------");
        ngx_log_stderr(0, "当前在线人数/总人数(%d/%d)。", tmpoLUC, m_worker_connections);
        ngx_log_stderr(0, "连接池中空闲连接/总连接/要释放的连接(%d/%d/%d)。", m_freeconnectionList.size(), m_connectionList.size(), m_recyconnectionList.size());
        ngx_log_stderr(0, "当前时间队列大小(%d)。", m_timerQueuemap.size());
        ngx_log_stderr(0, "当前收消息队列/发消息队列大小分别为(%d/%d)，丢弃的待发送数据包数量为%d。", tmprmqc, tmpsmqc, m_iDiscardSendPkgCount);

        // 收到消息过多
        if (tmprmqc > 100000)
        {
            // 接收队列过大，报一下，这个属于应该 引起警觉的，考虑限速等等手段
            ngx_log_stderr(0, "接收队列条目数量过大(%d)，要考虑限速或者增加处理线程数量了！！！！！！", tmprmqc);
        }

        ngx_log_stderr(0, "-------------------------------------end---------------------------------------");
    }

    return;
}

/***************************************************************
 *  @brief     清空 TCP 发消息队列
 *  @note      调用示例
 **************************************************************/
void CSocekt::clearMsgSendQueue()
{
    // 临时变量
    char *sTmpMempoint;
    // 内存对象
    CMemory *p_memory = CMemory::GetInstance();

    // 队列非空
    while (!m_MsgSendQueue.empty())
    {
        // 获取队首元素
        sTmpMempoint = m_MsgSendQueue.front();
        // 弹出队首元素
        m_MsgSendQueue.pop_front();
        // 释放内存
        p_memory->FreeMemory(sTmpMempoint);
    }
}

/***************************************************************
 *  @brief     初始化函数
 *  @return    true: 成功，false: 失败
 *  @note      在子进程中执行，对互斥量初始化，创建专门的工作线程用于发送数据、回收连接、踢人等，与线程池对象中的线程容器不同
 **************************************************************/
bool CSocekt::Initialize_subproc()
{
    // 进入子进程，在开始工作前，需要初始化全部互斥量

    // 发消息互斥量初始化
    if (pthread_mutex_init(&m_sendMessageQueueMutex, NULL) != 0)
    {
        ngx_log_stderr(0, "CSocekt::Initialize_subproc()中pthread_mutex_init(&m_sendMessageQueueMutex)失败.");
        return false;
    }

    // 连接相关互斥量初始化
    if (pthread_mutex_init(&m_connectionMutex, NULL) != 0)
    {
        ngx_log_stderr(0, "CSocekt::Initialize_subproc()中pthread_mutex_init(&m_connectionMutex)失败.");
        return false;
    }

    // 连接回收队列相关互斥量初始化
    if (pthread_mutex_init(&m_recyconnqueueMutex, NULL) != 0)
    {
        ngx_log_stderr(0, "CSocekt::Initialize_subproc()中pthread_mutex_init(&m_recyconnqueueMutex)失败.");
        return false;
    }

    // 和时间处理队列有关的互斥量初始化
    if (pthread_mutex_init(&m_timequeueMutex, NULL) != 0)
    {
        ngx_log_stderr(0, "CSocekt::Initialize_subproc()中pthread_mutex_init(&m_timequeueMutex)失败.");
        return false;
    }

    // 初始化发消息相关信号量，信号量用于进程/线程 之间的同步，虽然 互斥量[pthread_mutex_lock]和 条件变量[pthread_cond_wait]都是线程之间的同步手段，但
    // 这里用信号量实现 则 更容易理解，更容易简化问题，使用书写的代码短小且清晰；
    // 第二个参数=0，表示信号量在线程之间共享，确实如此 ，如果非0，表示在进程之间共享
    // 第三个参数=0，表示信号量的初始值，为0时，调用sem_wait()就会卡在那里卡着
    if (sem_init(&m_semEventSendQueue, 0, 0) == -1)
    {
        ngx_log_stderr(0, "CSocekt::Initialize_subproc()中sem_init(&m_semEventSendQueue,0,0)失败.");
        return false;
    }

    // 创建线程

    int err;

    // 临时变量，用于发送数据
    ThreadItem *pSendQueue;
    // 创建 一个新线程对象 并入到容器中
    m_threadVector.push_back(pSendQueue = new ThreadItem(this));
    // 创建一个线程到线程对象中
    err = pthread_create(&pSendQueue->_Handle, NULL, ServerSendQueueThread, pSendQueue);
    if (err != 0)
    {
        ngx_log_stderr(0, "CSocekt::Initialize_subproc()中pthread_create(ServerSendQueueThread)失败.");
        return false;
    }

    // 回收连接的线程
    ThreadItem *pRecyconn;
    // 放入线程容器
    m_threadVector.push_back(pRecyconn = new ThreadItem(this));
    err = pthread_create(&pRecyconn->_Handle, NULL, ServerRecyConnectionThread, pRecyconn);
    if (err != 0)
    {
        ngx_log_stderr(0, "CSocekt::Initialize_subproc()中pthread_create(ServerRecyConnectionThread)失败.");
        return false;
    }

    // 如果开启踢人时钟，则创建用于处理非法用户的线程
    // 是否开启踢人时钟，1：开启   0：不开启
    if (m_ifkickTimeCount == 1)
    {
        // 专门用来处理到期不发心跳包的用户踢出的线程
        ThreadItem *pTimemonitor;
        m_threadVector.push_back(pTimemonitor = new ThreadItem(this));
        err = pthread_create(&pTimemonitor->_Handle, NULL, ServerTimerQueueMonitorThread, pTimemonitor);
        if (err != 0)
        {
            ngx_log_stderr(0, "CSocekt::Initialize_subproc()中pthread_create(ServerTimerQueueMonitorThread)失败.");
            return false;
        }
    }

    return true;
}

/***************************************************************
 *  @brief     判断是否为 flood 攻击，成立则可以交由上层函数踢出
 *  @param     pConn    待判断 TCP 连接
 *  @return    true: 成立，false: 不成立
 **************************************************************/
bool CSocekt::TestFlood(lpngx_connection_t pConn)
{
    // 时间结构体，保存当前时间
    struct timeval sCurrTime;
    // 当前时间，（单位：毫秒）
    uint64_t iCurrTime;
    // 判断结果
    bool reco = false;

    // 获取当前时间
    gettimeofday(&sCurrTime, NULL);
    // 取得当前时间
    iCurrTime = (sCurrTime.tv_sec * 1000 + sCurrTime.tv_usec / 1000);

    // 收到包的时间差 < 100毫秒
    if ((iCurrTime - pConn->FloodkickLastTime) < m_floodTimeInterval)
    {
        // 发包太频繁，记录下来
        // 攻击次数增加
        pConn->FloodAttackCount++;
        // 发包时间更新
        pConn->FloodkickLastTime = iCurrTime;
    }
    else
    {
        // 恢复默认值
        pConn->FloodAttackCount = 0;
        pConn->FloodkickLastTime = iCurrTime;
    }

    // ngx_log_stderr(0,"pConn->FloodAttackCount=%d,m_floodKickCount=%d.",pConn->FloodAttackCount,m_floodKickCount);

    // 攻击次数超出指定值
    if (pConn->FloodAttackCount >= m_floodKickCount)
    {
        // 判断结果为成立
        reco = true;
    }

    return reco;
}

/***************************************************************
 *  @brief     退出函数，在子线程中进行
 *  @note      关闭全部线程（回收连接、发送数据等）
 **************************************************************/
void CSocekt::Shutdown_subproc()
{
    //(1)把干活的线程停止掉，注意 系统应该尝试通过设置 g_stopEvent = 1来 开始让整个项目停止
    //(2)用到信号量的，可能还需要调用一下sem_post
    if (sem_post(&m_semEventSendQueue) == -1) // 让ServerSendQueueThread()流程走下来干活
    {
        ngx_log_stderr(0, "CSocekt::Shutdown_subproc()中sem_post(&m_semEventSendQueue)失败.");
    }

    // 等待 socket 线程容器中全部线程运行结束
    for (auto iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
    {
        pthread_join((*iter)->_Handle, NULL);
    }

    // 释放 socket
    for (auto iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
    {
        if (*iter)
            delete *iter;
    }

    m_threadVector.clear();

    // 清空成员队列容器的元素
    clearMsgSendQueue();
    clearconnection();
    clearAllFromTimerQueue();

    // 释放全部互斥量
    pthread_mutex_destroy(&m_connectionMutex);       // 连接相关互斥量释放
    pthread_mutex_destroy(&m_sendMessageQueueMutex); // 发消息互斥量释放
    pthread_mutex_destroy(&m_recyconnqueueMutex);    // 连接回收队列相关的互斥量释放
    pthread_mutex_destroy(&m_timequeueMutex);        // 时间处理队列相关的互斥量释放
    sem_destroy(&m_semEventSendQueue);               // 发消息相关线程信号量释放
}

/***************************************************************
 *  @brief     将待发送的消息放入发消息队列中
 *  @param     psendbuf    待发送消息
 **************************************************************/
void CSocekt::msgSend(char *psendbuf)
{
    // 内存对象
    CMemory *p_memory = CMemory::GetInstance();

    // 访问发送消息队列，上锁
    CLock lock(&m_sendMessageQueueMutex); // 互斥量

    // 发送消息队列过大也可能给服务器带来风险，判断发消息队列大小
    if (m_iSendMsgQueueCount > 50000)
    {
        // 发送队列过大，比如客户端恶意不接受数据，就会导致这个队列越来越大
        // 那么可以考虑为了服务器安全，干掉一些数据的发送，虽然有可能导致客户端出现问题，但总比服务器不稳定要好很多

        // 发送队列超出限定

        // 丢弃包数量增加
        m_iDiscardSendPkgCount++;
        // 释放消息内存
        p_memory->FreeMemory(psendbuf);
        // 直接返回
        return;
    }

    // 总体数据并无风险，不会导致服务器崩溃，要看看个体数据，找一下恶意者了

    // 消息头，取出消息中的消息头
    LPSTRUC_MSG_HEADER pMsgHeader = (LPSTRUC_MSG_HEADER)psendbuf;
    // 取出消息头中的 TCP 连接
    lpngx_connection_t p_Conn = pMsgHeader->pConn;

    // 发送消息数过大，但却不接收消息，超出指定值则踢出
    if (p_Conn->iSendCount > 400)
    {
        // 该用户收消息太慢【或者干脆不收消息】，累积的该用户的发送队列中有的数据条目数过大，认为是恶意用户，直接切断
        ngx_log_stderr(0, "CSocekt::msgSend()中发现某用户%d积压了大量待发送数据包, 切断与他的连接！", p_Conn->fd);
        m_iDiscardSendPkgCount++;
        p_memory->FreeMemory(psendbuf);
        zdClosesocketProc(p_Conn); // 直接关闭
        return;
    }

    // TCP 连接发送消息数增加
    ++p_Conn->iSendCount;
    // 放入发送队列
    m_MsgSendQueue.push_back(psendbuf);
    // 发送队列大小增加
    ++m_iSendMsgQueueCount;

    // 将信号量的值+1，这样其他卡在sem_wait的就可以走下去

    // 激活 ServerSendQueueThread() 流程，处理发送队列中的信息
    if (sem_post(&m_semEventSendQueue) == -1)
    {
        ngx_log_stderr(0, "CSocekt::msgSend()中sem_post(&m_semEventSendQueue)失败.");
    }

    return;
}

/*
//epoll增加事件，可能被ngx_epoll_init()等函数调用
//fd:句柄，一个socket
//readevent：表示是否是个读事件，0是，1不是
//writeevent：表示是否是个写事件，0是，1不是
//otherflag：其他需要额外补充的标记，弄到这里
//eventtype：事件类型  ，一般就是用系统的枚举值，增加，删除，修改等;
//c：对应的连接池中的连接的指针
//返回值：成功返回1，失败返回-1；
int CSocekt::ngx_epoll_add_event(int fd,
                                int readevent,int writeevent,
                                uint32_t otherflag,
                                uint32_t eventtype,
                                lpngx_connection_t pConn
                                )
{
    struct epoll_event ev;
    //int op;
    memset(&ev, 0, sizeof(ev));

    if(readevent==1)
    {
        //读事件，这里发现官方nginx没有使用EPOLLERR，因此我们也不用【有些范例中是使用EPOLLERR的】
        ev.events = EPOLLIN|EPOLLRDHUP; //EPOLLIN读事件，也就是read ready【客户端三次握手连接进来，也属于一种可读事件】   EPOLLRDHUP 客户端关闭连接，断连
                                          //似乎不用加EPOLLERR，只用EPOLLRDHUP即可，EPOLLERR/EPOLLRDHUP 实际上是通过触发读写事件进行读写操作recv write来检测连接异常

        //ev.events |= (ev.events | EPOLLET);  //只支持非阻塞socket的高速模式【ET：边缘触发】，就拿accetp来说，如果加这个EPOLLET，则客户端连入时，epoll_wait()只会返回一次该事件，
                    //如果用的是EPOLLLT【水平触发：低速模式】，则客户端连入时，epoll_wait()会被触发多次，一直到用accept()来处理；



        //https://blog.csdn.net/q576709166/article/details/8649911
        //找下EPOLLERR的一些说法：
        //a)对端正常关闭（程序里close()，shell下kill或ctr+c），触发EPOLLIN和EPOLLRDHUP，但是不触发EPOLLERR 和EPOLLHUP。
        //b)EPOLLRDHUP    这个好像有些系统检测不到，可以使用EPOLLIN，read返回0，删除掉事件，关闭close(fd);如果有EPOLLRDHUP，检测它就可以直到是对方关闭；否则就用上面方法。
        //c)client 端close()联接,server 会报某个sockfd可读，即epollin来临,然后recv一下 ， 如果返回0再掉用epoll_ctl 中的EPOLL_CTL_DEL , 同时close(sockfd)。
                //有些系统会收到一个EPOLLRDHUP，当然检测这个是最好不过了。只可惜是有些系统，上面的方法最保险；如果能加上对EPOLLRDHUP的处理那就是万能的了。
        //d)EPOLLERR      只有采取动作时，才能知道是否对方异常。即对方突然断掉，是不可能有此事件发生的。只有自己采取动作（当然自己此刻也不知道），read，write时，出EPOLLERR错，说明对方已经异常断开。
        //e)EPOLLERR 是服务器这边出错（自己出错当然能检测到，对方出错你咋能知道啊）
        //f)给已经关闭的socket写时，会发生EPOLLERR，也就是说，只有在采取行动（比如读一个已经关闭的socket，或者写一个已经关闭的socket）时候，才知道对方是否关闭了。
                //这个时候，如果对方异常关闭了，则会出现EPOLLERR，出现Error把对方DEL掉，close就可以了。
    }
    if(writeevent==1)
    {
        //写事件
        ev.events = EPOLLOUT;
        //保留以往的读事件，读事件是肯定要在【一直有的】，你不能说后续增加了一个写事件，就把原来的读事件覆盖掉了
        ev.events |= (EPOLLIN|EPOLLRDHUP); //把以往的读事件弄进来
    }

    if(otherflag != 0)
    {
        ev.events |= otherflag;
    }

    //以下这段代码抄自nginx官方,因为指针的最后一位【二进制位】肯定不是1，所以 和 c->instance做 |运算；到时候通过一些编码，既可以取得c的真实地址，又可以把此时此刻的c->instance值取到
    //比如c是个地址，可能的值是 0x00af0578，对应的二进制是‭101011110000010101111000‬，而 | 1后是0x00af0579
    //ev.data.ptr = (void *)( (uintptr_t)c | c->instance);   //把对象弄进去，后续来事件时，用epoll_wait()后，这个对象能取出来用
                                                             //但同时把一个 标志位【不是0就是1】弄进去

    if(eventtype == EPOLL_CTL_ADD)
    {
        //如果事件类型是
        ev.data.ptr = (void *)pConn;
    }

    if(epoll_ctl(m_epollhandle,eventtype,fd,&ev) == -1)
    {
        ngx_log_stderr(errno,"CSocekt::ngx_epoll_add_event()中epoll_ctl(%d,%d,%d,%ud,%ud)失败.",fd,readevent,writeevent,otherflag,eventtype);
        //exit(2); //这是致命问题了，直接退，资源由系统释放吧，这里不刻意释放了，比较麻烦，后来发现不能直接退；
        return -1;
    }
    return 1;
}
*/

/***************************************************************
 *  @brief     专门处理发送消息队列的线程入口函数
 *  @param     threadData    线程对象
 *  @note      在子线程初始化时被创建出的线程直接调用
 **************************************************************/
void *CSocekt::ServerSendQueueThread(void *threadData)
{
    // 线程对象
    ThreadItem *pThread = static_cast<ThreadItem *>(threadData);
    // 记录所属线程池
    CSocekt *pSocketObj = pThread->_pThis;
    // 错误代码
    int err;

    std::list<char *>::iterator pos, pos2, posend;

    // 保存待发送数据
    char *pMsgBuf;
    // 消息头
    LPSTRUC_MSG_HEADER pMsgHeader;
    // 包头
    LPCOMM_PKG_HEADER pPkgHeader;
    // TCP 连接
    lpngx_connection_t p_Conn;

    unsigned short itmp;
    // 待发送数据宽度
    ssize_t sendsize;

    // 内存对象
    CMemory *p_memory = CMemory::GetInstance();

    // 不退出程序
    while (g_stopEvent == 0)
    {
        // 如果信号量值>0，则 -1(减1) 并走下去，否则卡这里卡着【为了让信号量值+1，可以在其他线程调用sem_post达到，
        // 实际上在CSocekt::msgSend()调用sem_post就达到了让这里sem_wait走下去的目的】
        // 如果被某个信号中断，sem_wait也可能过早的返回，错误为EINTR；
        // 整个程序退出之前，也要sem_post()一下，确保如果本线程卡在sem_wait()，也能走下去从而让本线程成功返回

        if (sem_wait(&pSocketObj->m_semEventSendQueue) == -1)
        {
            // 失败？及时报告，其他的也不好干啥
            if (errno != EINTR) // 这个我就不算个错误了【当阻塞于某个慢系统调用的一个进程捕获某个信号且相应信号处理函数返回时，该系统调用可能返回一个EINTR错误。】
                ngx_log_stderr(errno, "CSocekt::ServerSendQueueThread()中sem_wait(&pSocketObj->m_semEventSendQueue)失败.");
        }

        // 需要处理发送数据

        // 退出标志，则退出
        if (g_stopEvent != 0)
            break;

        // 发送队列大小大于 0，即有消息待发送
        if (pSocketObj->m_iSendMsgQueueCount > 0)
        {
            // 上锁
            err = pthread_mutex_lock(&pSocketObj->m_sendMessageQueueMutex);
            if (err != 0)
                ngx_log_stderr(err, "CSocekt::ServerSendQueueThread()中pthread_mutex_lock()失败，返回的错误码为%d!", err);

            // 遍历待发送消息队列
            pos = pSocketObj->m_MsgSendQueue.begin();
            posend = pSocketObj->m_MsgSendQueue.end();

            while (pos != posend)
            {
                pMsgBuf = (*pos);                                                        // 拿到的每个消息都是 消息头+包头+包体【但要注意，我们是不发送消息头给客户端的】
                pMsgHeader = (LPSTRUC_MSG_HEADER)pMsgBuf;                                // 指向消息头
                pPkgHeader = (LPCOMM_PKG_HEADER)(pMsgBuf + pSocketObj->m_iLenMsgHeader); // 指向包头
                p_Conn = pMsgHeader->pConn;

                // 包过期，因为如果 这个连接被回收，比如在ngx_close_connection(),inRecyConnectQueue()中都会自增iCurrsequence
                // 而且这里有没必要针对 本连接 来用m_connectionMutex临界 ,只要下面条件成立，肯定是客户端连接已断，要发送的数据肯定不需要发送了
                if (p_Conn->iCurrsequence != pMsgHeader->iCurrsequence)
                {
                    // 本包中保存的序列号与p_Conn【连接池中连接】中实际的序列号已经不同，丢弃此消息，小心处理该消息的删除
                    pos2 = pos;
                    pos++;
                    pSocketObj->m_MsgSendQueue.erase(pos2);
                    --pSocketObj->m_iSendMsgQueueCount; // 发送消息队列容量少1
                    p_memory->FreeMemory(pMsgBuf);
                    continue;
                } // end if

                if (p_Conn->iThrowsendCount > 0)
                {
                    // 靠系统驱动来发送消息，所以这里不能再发送
                    pos++;
                    continue;
                }

                --p_Conn->iSendCount; // 发送队列中有的数据条目数-1；

                // 走到这里，可以发送消息，一些必须的信息记录，要发送的东西也要从发送队列里干掉
                p_Conn->psendMemPointer = pMsgBuf; // 发送后释放用的，因为这段内存是new出来的
                pos2 = pos;
                pos++;
                pSocketObj->m_MsgSendQueue.erase(pos2);
                --pSocketObj->m_iSendMsgQueueCount;    // 发送消息队列容量少1
                p_Conn->psendbuf = (char *)pPkgHeader; // 要发送的数据的缓冲区指针，因为发送数据不一定全部都能发送出去，我们要记录数据发送到了哪里，需要知道下次数据从哪里开始发送
                itmp = ntohs(pPkgHeader->pkgLen);      // 包头+包体 长度 ，打包时用了htons【本机序转网络序】，所以这里为了得到该数值，用了个ntohs【网络序转本机序】；
                p_Conn->isendlen = itmp;               // 要发送多少数据，因为发送数据不一定全部都能发送出去，我们需要知道剩余有多少数据还没发送

                // 这里是重点，我们采用 epoll水平触发的策略，能走到这里的，都应该是还没有投递 写事件 到epoll中
                // epoll水平触发发送数据的改进方案：
                // 开始不把socket写事件通知加入到epoll,当我需要写数据的时候，直接调用write/send发送数据；
                // 如果返回了EAGIN【发送缓冲区满了，需要等待可写事件才能继续往缓冲区里写数据】，此时，我再把写事件通知加入到epoll，
                // 此时，就变成了在epoll驱动下写数据，全部数据发送完毕后，再把写事件通知从epoll中干掉；
                // 优点：数据不多的时候，可以避免epoll的写事件的增加/删除，提高了程序的执行效率；
                //(1)直接调用write或者send发送数据
                // ngx_log_stderr(errno,"即将发送数据%ud。",p_Conn->isendlen);

                sendsize = pSocketObj->sendproc(p_Conn, p_Conn->psendbuf, p_Conn->isendlen); // 注意参数
                if (sendsize > 0)
                {
                    if (sendsize == p_Conn->isendlen) // 成功发送出去了数据，一下就发送出去这很顺利
                    {
                        // 成功发送的和要求发送的数据相等，说明全部发送成功了 发送缓冲区去了【数据全部发完】
                        p_memory->FreeMemory(p_Conn->psendMemPointer); // 释放内存
                        p_Conn->psendMemPointer = NULL;
                        p_Conn->iThrowsendCount = 0; // 这行其实可以没有，因此此时此刻这东西就是=0的
                        // ngx_log_stderr(0,"CSocekt::ServerSendQueueThread()中数据发送完毕，很好。"); //做个提示吧，商用时可以干掉
                    }
                    else // 没有全部发送完毕(EAGAIN)，数据只发出去了一部分，但肯定是因为 发送缓冲区满了,那么
                    {
                        // 发送到了哪里，剩余多少，记录下来，方便下次sendproc()时使用
                        p_Conn->psendbuf = p_Conn->psendbuf + sendsize;
                        p_Conn->isendlen = p_Conn->isendlen - sendsize;
                        // 因为发送缓冲区慢了，所以 现在我要依赖系统通知来发送数据了
                        ++p_Conn->iThrowsendCount; // 标记发送缓冲区满了，需要通过epoll事件来驱动消息的继续发送【原子+1，且不可写成p_Conn->iThrowsendCount = p_Conn->iThrowsendCount +1 ，这种写法不是原子+1】
                        // 投递此事件后，我们将依靠epoll驱动调用ngx_write_request_handler()函数发送数据
                        if (pSocketObj->ngx_epoll_oper_event(
                                p_Conn->fd,    // socket句柄
                                EPOLL_CTL_MOD, // 事件类型，这里是增加【因为我们准备增加个写通知】
                                EPOLLOUT,      // 标志，这里代表要增加的标志,EPOLLOUT：可写【可写的时候通知我】
                                0,             // 对于事件类型为增加的，EPOLL_CTL_MOD需要这个参数, 0：增加   1：去掉 2：完全覆盖
                                p_Conn         // 连接池中的连接
                                ) == -1)
                        {
                            // 有这情况发生？这可比较麻烦，不过先do nothing
                            ngx_log_stderr(errno, "CSocekt::ServerSendQueueThread()ngx_epoll_oper_event()失败.");
                        }

                        // ngx_log_stderr(errno,"CSocekt::ServerSendQueueThread()中数据没发送完毕【发送缓冲区满】，整个要发送%d，实际发送了%d。",p_Conn->isendlen,sendsize);

                    }         // end if(sendsize > 0)
                    continue; // 继续处理其他消息
                }             // end if(sendsize > 0)

                // 能走到这里，应该是有点问题的
                else if (sendsize == 0)
                {
                    // 发送0个字节，首先因为我发送的内容不是0个字节的；
                    // 然后如果发送 缓冲区满则返回的应该是-1，而错误码应该是EAGAIN，所以我综合认为，这种情况我就把这个发送的包丢弃了【按对端关闭了socket处理】
                    // 这个打印下日志，我还真想观察观察是否真有这种现象发生
                    // ngx_log_stderr(errno,"CSocekt::ServerSendQueueThread()中sendproc()居然返回0？"); //如果对方关闭连接出现send=0，那么这个日志可能会常出现，商用时就 应该干掉
                    // 然后这个包干掉，不发送了
                    p_memory->FreeMemory(p_Conn->psendMemPointer); // 释放内存
                    p_Conn->psendMemPointer = NULL;
                    p_Conn->iThrowsendCount = 0; // 这行其实可以没有，因此此时此刻这东西就是=0的
                    continue;
                }

                // 能走到这里，继续处理问题
                else if (sendsize == -1)
                {
                    // 发送缓冲区已经满了【一个字节都没发出去，说明发送 缓冲区当前正好是满的】
                    ++p_Conn->iThrowsendCount; // 标记发送缓冲区满了，需要通过epoll事件来驱动消息的继续发送
                    // 投递此事件后，我们将依靠epoll驱动调用ngx_write_request_handler()函数发送数据
                    if (pSocketObj->ngx_epoll_oper_event(
                            p_Conn->fd,    // socket句柄
                            EPOLL_CTL_MOD, // 事件类型，这里是增加【因为我们准备增加个写通知】
                            EPOLLOUT,      // 标志，这里代表要增加的标志,EPOLLOUT：可写【可写的时候通知我】
                            0,             // 对于事件类型为增加的，EPOLL_CTL_MOD需要这个参数, 0：增加   1：去掉 2：完全覆盖
                            p_Conn         // 连接池中的连接
                            ) == -1)
                    {
                        // 有这情况发生？这可比较麻烦，不过先do nothing
                        ngx_log_stderr(errno, "CSocekt::ServerSendQueueThread()中ngx_epoll_add_event()_2失败.");
                    }
                    continue;
                }

                else
                {
                    // 能走到这里的，应该就是返回值-2了，一般就认为对端断开了，等待recv()来做断开socket以及回收资源
                    p_memory->FreeMemory(p_Conn->psendMemPointer); // 释放内存
                    p_Conn->psendMemPointer = NULL;
                    p_Conn->iThrowsendCount = 0; // 这行其实可以没有，因此此时此刻这东西就是=0的
                    continue;
                }

            } // end while(pos != posend)

// 解锁
            err = pthread_mutex_unlock(&pSocketObj->m_sendMessageQueueMutex);
            if (err != 0)
                ngx_log_stderr(err, "CSocekt::ServerSendQueueThread()pthread_mutex_unlock()失败，返回的错误码为%d!", err);

        } // if(pSocketObj->m_iSendMsgQueueCount > 0)
    }     // end while

    return (void *)0;
}
