
// 本文件存放网络中接受客户端数据、服务器端收包以及服务器端发送数据的相关函数实现

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
#include <pthread.h>   //多线程
#include <arpa/inet.h>
// #include <sys/socket.h>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"

/***************************************************************
 *  @brief     根据指定要求接收客户端数据
 *  @param     pConn    TCP 连接，客户端数据来源
 *  @param     buff    存放接收到的数据
 *  @param     buflen    待接收数据长度
 *  @return    -1: 发生错误，已处理；>0: 收到的数据长度
 *  @note      如果遇到断线、错误等情况，直接在本函数中释放连接到连接池，关闭 socket 句柄
 **************************************************************/
ssize_t CSocekt::recvproc(lpngx_connection_t pConn, char *buff, ssize_t buflen)
{
    // 接收数据长度
    ssize_t n;

    /***************************************************************
     *  @brief     recv()系统函数，接收缓冲区数据，最后一个参数 flag 一般为0
     *  @param     fd    socket 句柄，数据来源
     *  @param     buff    存放接收到的数据
     *  @param     buflen    待接收数据长度
     *  @return    读取到的数据长度
     *  @note      调用示例
     **************************************************************/
    n = recv(pConn->fd, buff, buflen, 0);

    // 未接收到数据，客户端关闭
    if (n == 0)
    {
        // 客户端关闭【应该是正常完成了4次挥手】，我这边就直接回收连接，关闭socket即可
        // ngx_log_stderr(0,"连接被客户端正常关闭[4路挥手关闭]！");
        // ngx_close_connection(pConn);
        // inRecyConnectQueue(pConn);

        // 主动关闭 TCP 连接
        zdClosesocketProc(pConn);

        // 返回，TCP 连接关闭
        return -1;
    }

    // 客户端没关闭，但发生错误
    if (n < 0)
    {
        // EAGAIN和EWOULDBLOCK[【这个应该常用在hp上】应该是一样的值，表示没收到数据
        // 一般来讲，在ET模式下会出现这个错误，因为 ET 模式下是不停的 recv 肯定有一个时刻收到这个 errno
        // 但LT模式下一般是来事件才收，所以不该出现这个返回值
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            // 我认为LT模式不该出现这个errno，而且这个其实也不是错误，所以不当做错误处理
            // epoll为LT模式不应该出现这个返回值，所以直接打印出来瞧瞧
            ngx_log_stderr(errno, "CSocekt::recvproc()中errno == EAGAIN || errno == EWOULDBLOCK成立, 出乎我意料！");
            return -1;
        }

        // EINTR错误的产生：当阻塞于某个慢系统调用的一个进程捕获某个信号且相应信号处理函数返回时，该系统调用可能返回一个EINTR错误。
        // 例如：在socket服务器端，设置了信号捕获机制，有子进程
        // 当在父进程阻塞于慢系统调用时由父进程捕获到了一个有效信号时，内核会致使accept返回一个EINTR错误(被中断的系统调用)
        // 这个不算错误，是我参考官方nginx，官方nginx这个就不算错误；
        if (errno == EINTR)
        {
            // 我认为LT模式不该出现这个errno，而且这个其实也不是错误，所以不当做错误处理
            // epoll为LT模式不应该出现这个返回值，所以直接输出
            ngx_log_stderr(errno, "CSocekt::recvproc()中errno == EINTR成立, 出乎我意料！");
            return -1;
        }

        // 所有从这里走下来的错误，都认为异常：意味着我们要关闭客户端套接字要回收连接池中连接；

        // errno参考：http://dhfapiran1.360drm.com
        if (errno == ECONNRESET) // #define ECONNRESET 104 /* Connection reset by peer */
        {
            // 如果客户端没有正常关闭socket连接，却关闭了整个运行程序
            // 【真是够粗暴无理的，应该是直接给服务器发送rst包而不是4次挥手包完成连接断开】，那么会产生这个错误
            // 10054(WSAECONNRESET)--远程程序正在连接的时候关闭会产生这个错误--远程主机强迫关闭了一个现有的连接
            // 算常规错误吧【普通信息型】，日志都不用打印，没啥意思，太普通的错误
            // do nothing

            //....一些大家遇到的很普通的错误信息，也可以往这里增加各种，代码要慢慢完善，一步到位，不可能，很多服务器程序经过很多年的完善才比较圆满；
        }
        else
        {
            // 能走到这里的，都表示错误，我打印一下日志，希望知道一下是啥错误，我准备打印到屏幕上
            if (errno == EBADF) // #define EBADF   9 /* Bad file descriptor */
            {
                // 因为多线程，偶尔会干掉socket，所以不排除产生这个错误的可能性
            }
            else
            {
                ngx_log_stderr(errno, "CSocekt::recvproc()中发生错误，我打印出来看看是啥错误！"); // 正式运营时可以考虑这些日志打印去掉
            }
        }

        // 特殊情况的错误处理完成，剩余情况都是非正常情况，直接关闭 TCP 连接，返回错误值

        // ngx_log_stderr(0,"连接被客户端 非 正常关闭！");

        // 这种真正的错误就要，直接关闭套接字，释放连接池中连接了
        // ngx_close_connection(pConn);
        // inRecyConnectQueue(pConn);
        zdClosesocketProc(pConn);

        return -1;
    }

    // 收到有效数据，返回收到的数据长度
    return n;
}

6 /***************************************************************
   *  @brief     当收到数据时，调用本函数进行处理
   *  @param     pConn    数据来源的 TCP 连接，类内成员保存收到的数据
   *  @return    返回值
   *  @note      本函数会被 ngx_epoll_process_events() 所调用，仅读取一次数据，若未读完，则交由上层循环调用本函数读取完整数据
   **************************************************************/
    void
    CSocekt::ngx_read_request_handler(lpngx_connection_t pConn)
{
    // 是否flood攻击
    bool isflood = false;

    // 收包，注意我们用的第二个和第三个参数，我们用的始终是这两个参数，
    // 因此必须保证 c->precvbuf 指向正确的收包位置，保证c->irecvlen指向正确的收包宽度

    // 从 pConn 连接中读取 irecvlen 长度的数据到 precvbuf 中
    ssize_t reco = recvproc(pConn, pConn->precvbuf, pConn->irecvlen);

    // 错误已经处理过，此处直接返回
    if (reco <= 0)
    {
        return;
    }

    // 成功受到一些数据，开始处理

    // 初始状态下，一定是准备接受包头状态
    if (pConn->curStat == _PKG_HD_INIT)
    {
        // 收到数据长度恰好等于包头长度
        if (reco == m_iLenPkgHeader)
        {
            // 处理完整包头数据
            ngx_wait_request_handler_proc_p1(pConn, isflood);
        }
        else
        {
            // 收到的包头不完整--我们不能预料每个包的长度，也不能预料各种拆包/粘包情况，所以收到不完整包头【也算是缺包】是很可能的；

            // 收到的包头不完整，则先保存已收到的数据

            // 调整状态为正在接受包头
            pConn->curStat = _PKG_HD_RECVING;
            // 待接收内存向后移动
            pConn->precvbuf = pConn->precvbuf + reco;
            // 需要接受的内存长度减少，保证先收到完整包头
            pConn->irecvlen = pConn->irecvlen - reco;
        } // end  if(reco == m_iLenPkgHeader)
    }
    // 收到一部分包头，继续接受包头
    else if (pConn->curStat == _PKG_HD_RECVING)
    {
        // 剩余待接收包头长度与收到的数据长度相等
        if (pConn->irecvlen == reco)
        {
            // 包头接受完整，处理包头数据
            ngx_wait_request_handler_proc_p1(pConn, isflood);
        }
        else
        {
            // 包头依然不完整，继续接受
            // pConn->curStat = _PKG_HD_RECVING;         // 实际不需要
            // 待接收内存向后移动
            pConn->precvbuf = pConn->precvbuf + reco;
            // 需要接受的内存长度减少，保证先收到完整包头
            pConn->irecvlen = pConn->irecvlen - reco;
        }
    }
    // 包头刚好收完，准备接收包体
    else if (pConn->curStat == _PKG_BD_INIT)
    {
        // 恰好收到整个包体
        if (reco == pConn->irecvlen)
        {
            // 收到的宽度等于要收的宽度，包体也收完整了
            if (m_floodAkEnable == 1)
            {
                // Flood攻击检测是否开启
                isflood = TestFlood(pConn);
            }

            // 直接准备处理
            ngx_wait_request_handler_proc_plast(pConn, isflood);
        }
        else
        {
            // 收到的宽度小于要收的宽度
            pConn->curStat = _PKG_BD_RECVING;
            pConn->precvbuf = pConn->precvbuf + reco;
            pConn->irecvlen = pConn->irecvlen - reco;
        }
    }
    else if (pConn->curStat == _PKG_BD_RECVING)
    {
        // 接收包体中，包体不完整，继续接收中
        if (pConn->irecvlen == reco)
        {
            // 包体收完整了
            if (m_floodAkEnable == 1)
            {
                // Flood攻击检测是否开启
                isflood = TestFlood(pConn);
            }

            ngx_wait_request_handler_proc_plast(pConn, isflood);
        }
        else
        {
            // 包体没收完整，继续收
            pConn->precvbuf = pConn->precvbuf + reco;
            pConn->irecvlen = pConn->irecvlen - reco;
        }
    } // end if(c->curStat == _PKG_HD_INIT)

    // flood 攻击
    if (isflood == true)
    {
        // 客户端flood服务器，则直接把客户端踢掉
        // ngx_log_stderr(errno,"发现客户端flood，干掉该客户端!");
        zdClosesocketProc(pConn);
    }

    return;
}

/***************************************************************
 *  @brief     处理收到的完整的数据包包头，将消息头和包头信息写入 TCP 连接中的成员变量
 *  @param     pConn    包头来源的 TCP 连接
 *  @param     isflood    是否 flood 攻击
 *  @note      专门处理收到完整包头，是数据处理第一步，后续有专门处理完整包的函数
 **************************************************************/
void CSocekt::ngx_wait_request_handler_proc_p1(lpngx_connection_t pConn, bool &isflood)
{
    CMemory *p_memory = CMemory::GetInstance();

    // 初始化一个包头变量
    LPCOMM_PKG_HEADER pPkgHeader;
    // 将已收到的包头信息拷贝出来
    pPkgHeader = (LPCOMM_PKG_HEADER)pConn->dataHeadInfo; // 正好收到包头时，包头信息肯定是在dataHeadInfo里；

    // 临时变量，保存数据包总长度
    unsigned short e_pkgLen;

    // 注意这里网络序转本机序，所有传输到网络上的2字节数据，都要用htons()转成网络序，所有从网络上收到的2字节数据，都要用ntohs()转成本机序
    // ntohs/htons的目的就是保证不同操作系统数据之间收发的正确性，【不管客户端/服务器是什么操作系统，发送的数字是多少，收到的就是多少】
    // 不明白的同学，直接百度搜索"网络字节序" "主机字节序" "c++ 大端" "c++ 小端"

    // 网络序转换为本机字节序
    e_pkgLen = ntohs(pPkgHeader->pkgLen);

    // 判断是否存在恶意包或者错误包

    // 包总长小于包头长度
    if (e_pkgLen < m_iLenPkgHeader)
    {
        // 伪造包/或者包错误，否则整个包长怎么可能比包头还小
        // 报文总长度 < 包头长度，认定非法用户，废包
        // 状态和接收位置都复原，这些值都有必要，因为有可能在其他状态比如_PKG_HD_RECVING状态调用这个函数

        // 非法数据包，恢复原状态，进行下一次读取

        // 重置接收数据状态
        pConn->curStat = _PKG_HD_INIT;
        // 保存读取数据内存从包头开始
        pConn->precvbuf = pConn->dataHeadInfo;
        // 期待读取长度为包头长度
        pConn->irecvlen = m_iLenPkgHeader;
    }
    // 包长超过最大规定长度，判断为恶意包
    else if (e_pkgLen > (_PKG_MAX_LENGTH - 1000))
    {
        // 恶意包，太大，认定非法用户，废包【包头中说这个包总长度这么大，这不行】
        // 状态和接收位置都复原，这些值都有必要，因为有可能在其他状态比如_PKG_HD_RECVING状态调用这个函数

        // 恢复状态
        pConn->curStat = _PKG_HD_INIT;
        pConn->precvbuf = pConn->dataHeadInfo;
        pConn->irecvlen = m_iLenPkgHeader;
    }
    // 包长符合要求，判断为正常包，开始处理包头
    else
    {
        // 我现在要分配内存开始收包体，因为包体长度并不是固定的，所以内存肯定要new出来；

        // 动态分配指定长度的内存，用来接收包体，分配内存长度为 消息头长度  + 包头长度 + 包体长度
        char *pTmpBuffer = (char *)p_memory->AllocMemory(m_iLenMsgHeader + e_pkgLen, false);
        // 将得到的内存空间指向 TCP 连接中待接收首地址
        pConn->precvMemPointer = pTmpBuffer;

        // 写入消息头内容

        // 初始化一个消息头指针
        LPSTRUC_MSG_HEADER ptmpMsgHeader = (LPSTRUC_MSG_HEADER)pTmpBuffer;
        ptmpMsgHeader->pConn = pConn;
        // 保存收到包时的连接序号
        ptmpMsgHeader->iCurrsequence = pConn->iCurrsequence;

        // 写入包头内容

        // 指针移动到写完消息头后的下一个可写位置
        pTmpBuffer += m_iLenMsgHeader;
        // 将包头信息拷贝到此处
        memcpy(pTmpBuffer, pPkgHeader, m_iLenPkgHeader);

        // 包总长度等于包头长度
        if (e_pkgLen == m_iLenPkgHeader)
        {
            // 该报文只有包头无包体【我们允许一个包只有包头，没有包体】
            // 收到完整包，直接进行后续处理

            // 开启 flood 检测
            if (m_floodAkEnable == 1)
            {
                isflood = TestFlood(pConn);
            }

            ngx_wait_request_handler_proc_plast(pConn, isflood);
        }
        else
        {
            // 仅收到完整包头，还未接收包体

            // 改变收包状态，开始接受包体
            pConn->curStat = _PKG_BD_INIT;
            // 待接收位置移动到包头末尾，开始接受包体
            pConn->precvbuf = pTmpBuffer + m_iLenPkgHeader;
            // 期待接收数据长度修改为包体长度
            pConn->irecvlen = e_pkgLen - m_iLenPkgHeader;
        }

    } // end if(e_pkgLen < m_iLenPkgHeader)

    return;
}

/***************************************************************
 *  @brief     处理收到的完整的数据包，将数据包放入指定队列，恢复收包状态
 *  @param     pConn    数据包来源的 TCP 连接
 *  @param     isflood    是否 flood 攻击
 *  @note      调用示例
 **************************************************************/
void CSocekt::ngx_wait_request_handler_proc_plast(lpngx_connection_t pConn, bool &isflood)
{
    // 把这段内存放到消息队列中来；
    // int irmqc = 0;  //消息队列当前信息数量
    // inMsgRecvQueue(c->precvMemPointer,irmqc); //返回消息队列当前信息数量irmqc，是调用本函数后的消息队列中消息数量
    // 激发线程池中的某个线程来处理业务逻辑
    // g_threadpool.Call(irmqc);

    // 是否 flood 攻击
    if (isflood == false)
    {
        // 放入消息队列等候下一步处理，专门有线程处理收到的数据包
        g_threadpool.inMsgRecvQueueAndSignal(pConn->precvMemPointer);
    }
    else
    {
        // 对于有攻击倾向的恶人，先把他的包丢掉
        CMemory *p_memory = CMemory::GetInstance();
        p_memory->FreeMemory(pConn->precvMemPointer); // 直接释放掉内存，根本不往消息队列入
    }

    // 恢复接受初始状态，准备接受下一个数据包

    // 地址置空，恢复原态
    pConn->precvMemPointer = NULL;
    // 收包状态机的状态恢复为原始态，为收下一个包做准备
    pConn->curStat = _PKG_HD_INIT;
    // 设置好收包的位置
    pConn->precvbuf = pConn->dataHeadInfo;
    // 设置好要接收数据的大小
    pConn->irecvlen = m_iLenPkgHeader;

    return;
}

/***************************************************************
 *  @brief     处理消息函数，专门处理各种接收到的TCP消息
 *  @param     pMsgBuf    消息存放地址，包含完整的消息头、包头、包体
 *  @note      线程池中的线程被唤醒且正确取得消息队列中的消息时，会调用本函数处理消息
 **************************************************************/
void CSocekt::threadRecvProcFunc(char *pMsgBuf)
{
    return;
}

/***************************************************************
 *  @brief     发送数据专用函数
 *  @param     c    TCP 连接
 *  @param     buff    待发送信息
 *  @param     size    发送信息长度
 *  @return    成功发送的字节数
 **************************************************************/
ssize_t CSocekt::sendproc(lpngx_connection_t c, char *buff, ssize_t size)
{
    // 这里参考借鉴了官方nginx函数ngx_unix_send()的写法
    ssize_t n;

    for (;;)
    {
        // 调用系统函数发送数据
        n = send(c->fd, buff, size, 0);

        // 成功发送部分数据
        if (n > 0)
        {
            // 发送成功一些数据，但发送了多少，我们这里不关心，也不需要再次send
            // 这里有两种情况
            //(1) n == size也就是想发送多少都发送成功了，这表示完全发完毕了
            //(2) n < size 没发送完毕，那肯定是发送缓冲区满了，所以也不必要重试发送，直接返回吧

            // 直接返回本次发送的字节数
            return n;
        }

        if (n == 0)
        {
            // send()返回0？ 一般recv()返回0表示断开,send()返回0，我这里就直接返回0吧【让调用者处理】；我个人认为send()返回0，要么你发送的字节是0，要么对端可能断开。
            // 网上找资料：send=0表示超时，对方主动关闭了连接过程
            // 我们写代码要遵循一个原则，连接断开，我们并不在send动作里处理诸如关闭socket这种动作，集中到recv那里处理，否则send,recv都处理都处理连接断开关闭socket则会乱套
            // 连接断开epoll会通知并且 recvproc()里会处理，不在这里处理
            return 0;
        }

        if (errno == EAGAIN) // 这东西应该等于EWOULDBLOCK
        {
            // 内核缓冲区满，这个不算错误
            return -1; // 表示发送缓冲区满了
        }

        if (errno == EINTR)
        {
            // 这个应该也不算错误 ，收到某个信号导致send产生这个错误？
            // 参考官方的写法，打印个日志，其他啥也没干，那就是等下次for循环重新send试一次了
            ngx_log_stderr(errno, "CSocekt::sendproc()中send()失败."); // 打印个日志看看啥时候出这个错误
            // 其他不需要做什么，等下次for循环吧
        }
        else
        {
            // 走到这里表示是其他错误码，都表示错误，错误我也不断开socket，我也依然等待recv()来统一处理断开，因为我是多线程，send()也处理断开，recv()也处理断开，很难处理好
            return -2;
        }
    } // end for
}

/***************************************************************
 *  @brief     连接的写处理函数
 *  @param     pConn    待处理连接
 *  @note      设置数据发送时的写处理函数,当数据可写时epoll通知我们，我们在 int CSocekt::ngx_epoll_process_events(int timer)  中调用此函数
 **************************************************************/
void CSocekt::ngx_write_request_handler(lpngx_connection_t pConn)
{
    // 内存对象
    CMemory *p_memory = CMemory::GetInstance();

    // 这些代码的书写可以参照 void* CSocekt::ServerSendQueueThread(void* threadData)

    // 调用函数通过系统函数直接发送
    ssize_t sendsize = sendproc(pConn, pConn->psendbuf, pConn->isendlen);

    // 成功发送一部分但不完全发送
    if (sendsize > 0 && sendsize != pConn->isendlen)
    {
        // 记录当前发送位置
        pConn->psendbuf = pConn->psendbuf + sendsize;
        // 待发送长度减少
        pConn->isendlen = pConn->isendlen - sendsize;
        // 返回
        return;
    }
    // 缓冲区满，一般不会出现
    else if (sendsize == -1)
    {
        // 这不太可能，可以发送数据时通知我发送数据，我发送时你却通知我发送缓冲区满？
        ngx_log_stderr(errno, "CSocekt::ngx_write_request_handler()时if(sendsize == -1)成立，这很怪异。");

        return;
    }

    // 完成发送
    if (sendsize > 0 && sendsize == pConn->isendlen)
    {
        // 如果是成功的发送完毕数据，则把写事件通知从epoll中干掉吧；其他情况，那就是断线了，等着系统内核把连接从红黑树中干掉即可；

        // 将写事件从树中移除
        if (ngx_epoll_oper_event(
                pConn->fd,     // socket句柄
                EPOLL_CTL_MOD, // 事件类型，这里是修改【因为我们准备减去写通知】
                EPOLLOUT,      // 标志，这里代表要减去的标志,EPOLLOUT：可写【可写的时候通知我】
                1,             // 对于事件类型为增加的，EPOLL_CTL_MOD需要这个参数, 0：增加   1：去掉 2：完全覆盖
                pConn          // 连接池中的连接
                ) == -1)
        {
            // 有这情况发生？这可比较麻烦，不过先do nothing
            ngx_log_stderr(errno, "CSocekt::ngx_write_request_handler()中ngx_epoll_oper_event()失败。");
        }

        // ngx_log_stderr(0,"CSocekt::ngx_write_request_handler()中数据发送完毕，很好。"); //做个提示吧，商用时可以干掉
    }

    // 数据发送完毕或者断开连接

    /* 2019.4.2注释掉，调整下顺序，感觉这个顺序不太好
    //数据发送完毕，或者把需要发送的数据干掉，都说明发送缓冲区可能有地方了，让发送线程往下走判断能否发送新数据
    if(sem_post(&m_semEventSendQueue)==-1)
        ngx_log_stderr(0,"CSocekt::ngx_write_request_handler()中sem_post(&m_semEventSendQueue)失败.");


    p_memory->FreeMemory(pConn->psendMemPointer);  //释放内存
    pConn->psendMemPointer = NULL;
    --pConn->iThrowsendCount;  //建议放在最后执行
    */

    // 2019.4.2调整成新顺序

    // 释放内存
    p_memory->FreeMemory(pConn->psendMemPointer);
    // 发送数据指针置空
    pConn->psendMemPointer = NULL;
    // 缓冲区满变量减少
    --pConn->iThrowsendCount;

    if (sem_post(&m_semEventSendQueue) == -1)
        ngx_log_stderr(0, "CSocekt::ngx_write_request_handler()中sem_post(&m_semEventSendQueue)失败.");

    return;
}
