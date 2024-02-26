
// 本文件存放网络中接受连接 accept 三次握手接入函数的函数实现

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
// #include <sys/socket.h>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"

/***************************************************************
 *  @brief     建立新连接专用函数，当新连接进入时，本函数会被 ngx_epoll_process_events() 调用，用于处理监听套接字的读事件（三次握手）
 *  @param     oldc    原有的 TCP 连接
 *  @note      因为 listen 套接字上用的不是ET【边缘触发】，而是LT【水平触发】，
 *             意味着客户端连入如果我要不处理，这个函数会被多次调用，
 *             所以，我这里可以不必多次accept()，可以只执行一次accept()，这也可以避免本函数被卡太久，
 *             注意，本函数应该尽快返回，以免阻塞程序运行；
 **************************************************************/
void CSocekt::ngx_event_accept(lpngx_connection_t oldc)
{
    // 远端服务器的socket地址
    struct sockaddr mysockaddr;
    // socket 地址长度
    socklen_t socklen;

    // 临时变量
    int err; // 保存错误信息
    int level;
    int s;

    // 优先使用 accept4() 函数
    static int use_accept4 = 1;
    // 连接池中的一个新连接
    lpngx_connection_t newc;
    // 取地址长度
    socklen = sizeof(mysockaddr);

    // ngx_log_stderr(0,"这是几个\n"); 这里会惊群，也就是说，epoll技术本身有惊群的问题

    do
    {
        // 接收客户端连接请求
        if (use_accept4)
        {
            // 因为listen套接字是非阻塞的，所以即便已完成连接队列为空，accept4()也不会卡在这里；

            /***************************************************************
             *  @brief     接收客户端连接，
             *  @param     fd    服务端监听套接字，即收到客户端连接请求的套接字
             *  @param     mysockaddr    存放客户端信标的内存
             *  @param     socklen    内存大小
             *  @return    返回新的套接字用于和接入的客户端连接发送数据
             *  @note      调用示例
             **************************************************************/
            s = accept4(oldc->fd, &mysockaddr, &socklen, SOCK_NONBLOCK);

            // 从内核获取一个用户端连接，SOCK_NONBLOCK 表示返回一个非阻塞的 socket，节省一次 ioctl【设置为非阻塞】调用
        }
        else
        {
            s = accept(oldc->fd, &mysockaddr, &socklen);
        }

        // 惊群，有时候不一定完全惊动所有4个worker进程，可能只惊动其中2个等等，其中一个成功其余的accept4()都会返回-1；
        // 错误 (11: Resource temporarily unavailable【资源暂时不可用】)
        // 所以参考资料：https://blog.csdn.net/russell_tao/article/details/7204260
        // 其实，在linux2.6内核上，accept系统调用已经不存在惊群了（至少我在2.6.18内核版本上已经不存在）。
        // 大家可以写个简单的程序试下，在父进程中 bind，listen，然后fork出子进程，
        // 所有的子进程都 accept 这个监听句柄。
        // 这样，当新连接过来时，大家会发现，仅有一个子进程返回新建的连接，其他子进程继续休眠在accept调用上，没有被唤醒。
        // ngx_log_stderr(0,"测试惊群问题，看惊动几个worker进程%d\n",s);
        // 【我的结论是：accept4可以认为基本解决惊群问题，但似乎并没有完全解决，有时候还会惊动其他的worker进程】

        // 惊群测试
        // if(s == -1)
        // {
        //     ngx_log_stderr(0,"惊群测试:ngx_event_accept()中accept失败,进程id=%d",ngx_pid);
        // }
        // else
        // {
        //     ngx_log_stderr(0,"惊群测试:ngx_event_accept()中accept成功,进程id=%d",ngx_pid);
        // }

        // 接受客户端连接失败
        if (s == -1)
        {
            // 保存错误信息
            err = errno;

            // 对 accept、send 和 recv 而言，事件未发生时 errno 通常被设置成EAGAIN（意为“再来一次”）或者EWOULDBLOCK（意为“期待阻塞”）

            // accept()没准备好，这个EAGAIN错误EWOULDBLOCK是一样的
            if (err == EAGAIN)
            {
                // 除非你用一个循环不断的accept()取走所有的连接，不然一般不会有这个错误【我们这里只取一个连接，也就是accept()一次】
                return;
            }

            // 设置日志错误等级
            level = NGX_LOG_ALERT;

            // ECONNRESET 错误发生在对方意外关闭套接字后
            // 【您的主机中的软件放弃了一个已建立的连接--由于超时或者其它失败而中止接连(用户插拔网线就可能有这个错误出现)】
            if (err == ECONNABORTED)
            {
                // 该错误被描述为“software caused connection abort”，即“软件引起的连接中止”。原因在于当服务和客户进程在完成用于 TCP 连接的“三次握手”后，
                // 客户 TCP 却发送了一个 RST （复位）分节，在服务进程看来，就在该连接已由 TCP 排队，等着服务进程调用 accept 的时候 RST 却到达了。
                // POSIX 规定此时的 errno 值必须 ECONNABORTED。源自 Berkeley 的实现完全在内核中处理中止的连接，服务进程将永远不知道该中止的发生。
                // 服务器进程一般可以忽略该错误，直接再次调用accept。

                level = NGX_LOG_ERR;
            }
            else if (err == EMFILE || err == ENFILE)
            // EMFILE:进程的fd已用尽【已达到系统所允许单一进程所能打开的文件/套接字总数】。
            // 可参考：https://blog.csdn.net/sdn_prc/article/details/28661661   以及 https://bbs.csdn.net/topics/390592927
            // ulimit -n ,看看文件描述符限制,如果是1024的话，需要改大;  打开的文件句柄数过多 ,把系统的fd软限制和硬限制都抬高.
            // ENFILE这个errno的存在，表明一定存在system-wide的resource limits，而不仅仅有 process-specific 的 resource limits。
            // 按照常识，process-specific 的 resource limits，一定受限于 system-wide 的 resource limits。
            {
                level = NGX_LOG_CRIT;
            }

            // ngx_log_error_core(level,errno,"CSocekt::ngx_event_accept()中accept4()失败!");

            // accept4()函数没实现
            if (use_accept4 && err == ENOSYS)
            {
                // 标记不使用 accept4() 函数，改用accept()函数
                use_accept4 = 0;
                // 回去重新使用 accept() 函数
                continue;
            }

            // 对方关闭了套接字
            if (err == ECONNABORTED)
            {
                // 这个错误因为可以忽略，所以不用干啥
                // do nothing
            }

            if (err == EMFILE || err == ENFILE)
            {
                // do nothing，这个官方做法是先把读事件从listen socket上移除，然后再弄个定时器，定时器到了则继续执行该函数，但是定时器到了有个标记，会把读事件增加到listen socket上去；
                // 我这里目前先不处理吧【因为上边已经写这个日志了】；
            }

            return;

        } // end if(s == -1)

        // 返回套接字 accept4()/accept() 成功，可以开始处理

        // 用户连接数过多，关闭该用户socket
        if (m_onlineUserCount >= m_worker_connections)
        {
            // ngx_log_stderr(0,"超出系统允许的最大连入用户数(最大允许连入数%d)，关闭连入请求(%d)。",m_worker_connections,s);

            // 关闭套接字句柄，返回
            close(s);
            return;
        }

        // 如果某些恶意用户连上来发了1条数据就断，不断连接，会导致频繁调用 ngx_get_connection() 使用我们短时间内产生大量连接，危及本服务器安全

        // 判断连接池大小是否已经远超规定的连接上限
        if (m_connectionList.size() > (m_worker_connections * 5))
        {
            // 比如你允许同时最大2048个连接，但连接池却有了 2048*5这么大的容量，
            // 这肯定是表示短时间内 产生大量连接/断开，因为我们的延迟回收机制，这里连接还在垃圾池里没有被回收

            // 空闲连接却少于规定连接数，证明存在恶意连接
            if (m_freeconnectionList.size() < m_worker_connections)
            {
                // 整个连接池这么大了，而空闲连接却这么少了，所以我认为是  短时间内 产生大量连接，发一个包后就断开，我们不可能让这种情况持续发生，所以必须断开新入用户的连接
                // 一直到m_freeconnectionList变得足够大【连接池中连接被回收的足够多】

                // 关闭，返回
                close(s);
                return;
            }
        }

        // ngx_log_stderr(errno,"accept4成功s=%d",s); //s这里就是 一个句柄了

        // 安全状态下，为 socket 句柄获取一个连接池连接
        newc = ngx_get_connection(s);
        // 连接池连接数量不足
        if (newc == NULL)
        {
            // 连接池中连接不够用，那么就得把这个socekt直接关闭并返回了，因为在ngx_get_connection()中已经写日志了，所以这里不需要写日志了
            if (close(s) == -1)
            {
                ngx_log_error_core(NGX_LOG_ALERT, errno, "CSocekt::ngx_event_accept()中close(%d)失败!", s);
            }

            return;
        }

        //...........将来这里会判断是否连接超过最大允许连接数，现在，这里可以不处理

        // 成功的拿到了连接池中的一个连接
        // 拷贝客户端地址到连接对象【要转成字符串ip地址参考函数ngx_sock_ntop()】
        memcpy(&newc->s_sockaddr, &mysockaddr, socklen);

        //{
        //    //测试将收到的地址弄成字符串，格式形如"192.168.1.126:40904"或者"192.168.1.126"
        //    u_char ipaddr[100]; memset(ipaddr,0,sizeof(ipaddr));
        //    ngx_sock_ntop(&newc->s_sockaddr,1,ipaddr,sizeof(ipaddr)-10); //宽度给小点
        //    ngx_log_stderr(0,"ip信息为%s\n",ipaddr);
        //}

        // 如果未使用 accept4() 函数，则手动设置非阻塞
        if (!use_accept4)
        {
            // 如果不是用accept4()取得的socket，那么就要设置为非阻塞【因为用accept4()的已经被accept4()设置为非阻塞了】
            if (setnonblocking(s) == false)
            {
                // 设置非阻塞居然失败
                ngx_close_connection(newc); // 关闭socket,这种可以立即回收这个连接，无需延迟，因为其上还没有数据收发，谈不到业务逻辑因此无需延迟；
                return;                     // 直接返回
            }
        }

        // 将原连接的监听对象赋值给新的连接
        newc->listening = oldc->listening; // 连接对象 和监听对象关联，方便通过连接对象找监听对象【关联到监听端口】

        // 标记可以写，新连接写事件肯定是ready的；【从连接池拿出一个连接时这个连接的所有成员都是0】
        //  newc->w_ready = 1;

        // 设置数据来时的读处理函数，其实官方nginx中是ngx_http_wait_request_handler()
        newc->rhandler = &CSocekt::ngx_read_request_handler;
        // 设置数据发送时的写处理函数
        newc->whandler = &CSocekt::ngx_write_request_handler;

        // 客户端应该主动发送第一次的数据，这里将读事件加入epoll监控，这样当客户端发送数据来时，会触发ngx_wait_request_handler()被ngx_epoll_process_events()调用
        if (ngx_epoll_oper_event(
                s,                    // socekt句柄
                EPOLL_CTL_ADD,        // 事件类型，这里是增加
                EPOLLIN | EPOLLRDHUP, // 标志，这里代表要增加的标志,EPOLLIN：可读，EPOLLRDHUP：TCP连接的远端关闭或者半关闭 ，如果边缘触发模式可以增加 EPOLLET
                0,                    // 对于事件类型为增加的，不需要这个参数
                newc                  // 连接池中的连接
                ) == -1)
        {
            // 增加事件失败，失败日志在ngx_epoll_add_event中写过了，因此这里不多写啥；
            ngx_close_connection(newc); // 关闭socket,这种可以立即回收这个连接，无需延迟，因为其上还没有数据收发，谈不到业务逻辑因此无需延迟；
            return;                     // 直接返回
        }
        /*
        else
        {
            //打印下发送缓冲区大小
            int           n;
            socklen_t     len;
            len = sizeof(int);
            getsockopt(s,SOL_SOCKET,SO_SNDBUF, &n, &len);
            ngx_log_stderr(0,"发送缓冲区的大小为%d!",n); //87040

            n = 0;
            getsockopt(s,SOL_SOCKET,SO_RCVBUF, &n, &len);
            ngx_log_stderr(0,"接收缓冲区的大小为%d!",n); //374400

            int sendbuf = 2048;
            if (setsockopt(s, SOL_SOCKET, SO_SNDBUF,(const void *) &sendbuf,n) == 0)
            {
                ngx_log_stderr(0,"发送缓冲区大小成功设置为%d!",sendbuf);
            }

             getsockopt(s,SOL_SOCKET,SO_SNDBUF, &n, &len);
            ngx_log_stderr(0,"发送缓冲区的大小为%d!",n); //87040
        }
        */

        // 是否开启踢人时钟
        if (m_ifkickTimeCount == 1)
        {
            AddToTimerQueue(newc);
        }

        // 连入用户数量增加
        ++m_onlineUserCount;

        // 成功则直接跳出
        break;

    } while (1);

    return;
}
