// 本文件存放 网络和逻辑处理 类的函数实现

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
#include "ngx_c_memory.h"
#include "ngx_c_crc32.h"
#include "ngx_c_slogic.h"
#include "ngx_logiccomm.h"
#include "ngx_c_lockmutex.h"
// #include "ngx_c_socket.h"

/***************************************************************
 *  @brief     处理业务函数指针
 *  @param     pConn    连接池中连接的指针
 *  @param     pMsgHeader    消息头指针
 *  @param     pPkgBody    包体指针
 *  @param     iBodyLength    包体长度
 *  @return    返回值
 *  @note      调用示例
 **************************************************************/
typedef bool (CLogicSocket::*handler)(lpngx_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char *pPkgBody, unsigned short iBodyLength);

// 保存不同序号的 成员函数指针 ，分别执行不同的函数，完成不同功能
static const handler statusHandler[] =
    {
        // 数组前5个元素，保留，以备将来增加一些基本服务器功能
        &CLogicSocket::_HandlePing, // 【0】：心跳包的实现
        NULL,                       // 【1】：下标从0开始
        NULL,                       // 【2】：下标从0开始
        NULL,                       // 【3】：下标从0开始
        NULL,                       // 【4】：下标从0开始

        // 开始处理具体的业务逻辑
        &CLogicSocket::_HandleRegister, // 【5】：实现具体的注册功能
        &CLogicSocket::_HandleLogIn,    // 【6】：实现具体的登录功能

};

// 总函数个数
#define AUTH_TOTAL_COMMANDS sizeof(statusHandler) / sizeof(handler)

/***************************************************************
 *  @brief     构造函数，默认使用父类构造函数
 **************************************************************/
CLogicSocket::CLogicSocket()
{
}

/***************************************************************
 *  @brief     析构函数，默认使用父类析构函数
 **************************************************************/
CLogicSocket::~CLogicSocket()
{
}

/***************************************************************
 *  @brief     初始化函数，初始化相关功能，监听端口等
 *  @return    true: 成功，false: 失败
 *  @note      在 fork 子进程之前完成
 **************************************************************/
bool CLogicSocket::Initialize()
{
    // 做一些和本类相关的初始化工作
    //....日后根据需要扩展
    bool bParentInit = CSocekt::Initialize(); // 调用父类的同名函数
    return bParentInit;
}

/***************************************************************
 *  @brief     处理收到的完整消息
 *  @param     pMsgBuf 收到的完整消息，消息头 + 包头 + 包体
 *  @return    返回值
 *  @note      由线程池中的一个线程调用本函数，本函数在一个单独线程内执行
 **************************************************************/
void CLogicSocket::threadRecvProcFunc(char *pMsgBuf)
{
    // 取出消息中的消息头
    LPSTRUC_MSG_HEADER pMsgHeader = (LPSTRUC_MSG_HEADER)pMsgBuf;
    // 取出消息中的包头
    LPCOMM_PKG_HEADER pPkgHeader = (LPCOMM_PKG_HEADER)(pMsgBuf + m_iLenMsgHeader);
    // 指向包体的指针
    void *pPkgBody;
    // 取出包头中的包长数据
    unsigned short pkglen = ntohs(pPkgHeader->pkgLen);

    // 包头长度等于包长，无包体
    if (m_iLenPkgHeader == pkglen)
    {
        // 没有包体，只有包头，判断 CRC 值，规定只有包头的消息 CRC 给 0
        if (pPkgHeader->crc32 != 0)
        {
            // crc错，直接丢弃
            return;
        }

        // 包体指针置空
        pPkgBody = NULL;
    }
    // 有包体
    else
    {
        // 将 CRC 码转为主机序
        pPkgHeader->crc32 = ntohl(pPkgHeader->crc32);
        // 包体指针跳过消息头和包头大小，指向包体内存
        pPkgBody = (void *)(pMsgBuf + m_iLenMsgHeader + m_iLenPkgHeader);

        // ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中收到包的crc值为%d!",pPkgHeader->crc32);

        // 计算crc值判断包的完整性
        int calccrc = CCRC32::GetInstance()->Get_CRC((unsigned char *)pPkgBody, pkglen - m_iLenPkgHeader);

        // 服务器端根据包体计算crc值，和客户端传递过来的包头中的crc32信息比较
        if (calccrc != pPkgHeader->crc32)
        {
            // crc错，直接丢弃
            // ngx_log_stderr(0, "CLogicSocket::threadRecvProcFunc()中CRC错误[服务器:%d/客户端:%d]，丢弃数据!", calccrc, pPkgHeader->crc32);
            return;
        }
        // 验证无误，可以开始处理
        else
        {
            // ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中CRC正确[服务器:%d/客户端:%d]，不错!",calccrc,pPkgHeader->crc32);
        }
    }

    // 确认包合法性

    // 取出消息代码
    unsigned short imsgCode = ntohs(pPkgHeader->msgCode);
    // 取出消息头中保存的 TCP 连接对象指针
    lpngx_connection_t p_Conn = pMsgHeader->pConn;

    // 进一步判断是否必须处理，如连接是否过期等

    // 连接序号改变，说明连接已经被回收甚至被重新分配出去
    // 即从在收到客户端数据包到服务器指派线程去处理的过程中，客户端已断开，则不必处理
    if (p_Conn->iCurrsequence != pMsgHeader->iCurrsequence)
    {
        return; // 丢弃不理这种包了【客户端断开了】
    }

    // 判断消息码合法性，避免恶意连接破坏
    if (imsgCode >= AUTH_TOTAL_COMMANDS)
    {
        // 消息码不在规定范围内，输出恶意信息来源
        ngx_log_stderr(0, "CLogicSocket::threadRecvProcFunc()中imsgCode=%d消息码不对!", imsgCode);
        // 不处理，直接返回
        return;
    }

    // 判断是否存在对应的处理函数
    if (statusHandler[imsgCode] == NULL)
    {
        // 消息码没有对应的处理函数，输出恶意信息来源
        ngx_log_stderr(0, "CLogicSocket::threadRecvProcFunc()中imsgCode=%d消息码找不到对应的处理函数!", imsgCode);
        // 无处理函数，直接返回
        return;
    }

    // 保证消息正确性，消息码正确性，可以调用相关函数处理
    (this->*statusHandler[imsgCode])(p_Conn, pMsgHeader, (char *)pPkgBody, pkglen - m_iLenPkgHeader);

    return;
}

/***************************************************************
 *  @brief     注册，业务逻辑处理函数
 *  @param     pConn    连接池中连接的指针
 *  @param     pMsgHeader    消息头指针
 *  @param     pPkgBody    包体指针
 *  @param     iBodyLength    包体长度
 *  @return    true: 正确处理返回 false: 无效包信息，不处理
 **************************************************************/
bool CLogicSocket::_HandleRegister(lpngx_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char *pPkgBody, unsigned short iBodyLength)
{
    // ngx_log_stderr(0,"执行了CLogicSocket::_HandleRegister()!");

    // 首先判断包体合法性
    // 根据服务器端规定，如果本命令必须配合包体使用，则不带包体的直接丢弃

    // 在本命令处理函数中，处理注册信息，规定包体信息按照注册结构体形式存储并发送

    // 包体为空，直接返回
    if (pPkgBody == NULL)
    {
        return false;
    }

    // 获取注册结构体长度
    int iRecvLen = sizeof(STRUCT_REGISTER);

    // 长度不一致，直接丢弃
    if (iRecvLen != iBodyLength)
    {
        return false;
    }

    // 判断包体安全性结束，可以开始处理

    //(2)对于同一个用户，可能同时发送来多个请求过来，造成多个线程同时为该 用户服务，
    // 比如以网游为例，用户要在商店中买A物品，又买B物品，而用户的钱 只够买A或者B，不够同时买A和B呢？
    // 那如果用户发送购买命令过来买了一次A，又买了一次B，如果是两个线程来执行同一个用户的这两次不同的购买命令，
    // 很可能造成这个用户购买成功了 A，又购买成功了 B
    // 所以，为了稳妥起见，针对某个用户的命令，我们一般都要互斥,我们需要增加临界的变量于ngx_connection_s结构中

    // 凡是和本用户有关的访问都互斥
    CLock lock(&pConn->logicPorcMutex);

    // 获取包体信息
    LPSTRUCT_REGISTER p_RecvInfo = (LPSTRUCT_REGISTER)pPkgBody;
    // 转换为主机序
    p_RecvInfo->iType = ntohl(p_RecvInfo->iType);

    // 这非常关键，防止客户端发送过来畸形包，导致服务器直接使用这个数据出现错误
    p_RecvInfo->username[sizeof(p_RecvInfo->username) - 1] = 0;
    p_RecvInfo->password[sizeof(p_RecvInfo->password) - 1] = 0;

    // 根据具体的业务逻辑，进行进一步处理，属于具体的服务器功能，不在本架构开发考虑范围内

    // 返回给客户端数据，同样需要规定固定的格式进行传输，此处不妨仍然使用注册结构体 STRUCT_REGISTER 返回为例

    // 指向收到的包的包头，其中数据后续可能要用到
    // LPSTRUCT_REGISTER pFromPkgHeader =  (LPSTRUCT_REGISTER)(((char *)pMsgHeader)+m_iLenMsgHeader);

    // 先声明一些发送数据时的临时变量

    // 临时变量，包头
    LPCOMM_PKG_HEADER pPkgHeader;
    // 内存对象
    CMemory *p_memory = CMemory::GetInstance();
    // CRC 验证对象
    CCRC32 *p_crc32 = CCRC32::GetInstance();
    // 发送数据长度，此处以注册结构体 STRUCT_REGISTER 为例
    int iSendLen = sizeof(STRUCT_REGISTER);

    // 首先分配需要的内存

    // iSendLen = 65000; //unsigned最大也就是这个值
    // 准备发送的格式，这里是 消息头+包头+包体
    char *p_sendbuf = (char *)p_memory->AllocMemory(m_iLenMsgHeader + m_iLenPkgHeader + iSendLen, false);

    // 填充消息头内容，将原消息头拷贝到此处
    memcpy(p_sendbuf, pMsgHeader, m_iLenMsgHeader);

    // 填充包头信息
    // 指向包头内存
    pPkgHeader = (LPCOMM_PKG_HEADER)(p_sendbuf + m_iLenMsgHeader);
    // 消息代码，可以统一在 ngx_logiccomm.h 中定义
    pPkgHeader->msgCode = _CMD_REGISTER;
    // htons主机序转网络序
    // 消息代码
    pPkgHeader->msgCode = htons(pPkgHeader->msgCode);
    // 包长
    pPkgHeader->pkgLen = htons(m_iLenPkgHeader + iSendLen);

    // 填充包体信息
    // 包体，也即注册结构体，内存指向包体首端
    LPSTRUCT_REGISTER p_sendInfo = (LPSTRUCT_REGISTER)(p_sendbuf + m_iLenMsgHeader + m_iLenPkgHeader);

    // 。。。。。这里根据需要，填充要发回给客户端的内容,int类型要使用htonl()转，short类型要使用htons()转；

    // 计算 CRC 验证码值
    pPkgHeader->crc32 = p_crc32->Get_CRC((unsigned char *)p_sendInfo, iSendLen);
    pPkgHeader->crc32 = htonl(pPkgHeader->crc32);

    // 发送数据包到客户端
    msgSend(p_sendbuf);

    /*if(ngx_epoll_oper_event(
                                pConn->fd,          //socekt句柄
                                EPOLL_CTL_MOD,      //事件类型，这里是增加
                                EPOLLOUT,           //标志，这里代表要增加的标志,EPOLLOUT：可写
                                0,                  //对于事件类型为增加的，EPOLL_CTL_MOD需要这个参数, 0：增加   1：去掉 2：完全覆盖
                                pConn               //连接池中的连接
                                ) == -1)
    {
        ngx_log_stderr(0,"1111111111111111111111111111111111111111111111111111111111111!");
    } */

    /*
    sleep(100);  //休息这么长时间
    //如果连接回收了，则肯定是iCurrsequence不等了
    if(pMsgHeader->iCurrsequence != pConn->iCurrsequence)
    {
        //应该是不等，因为这个插座已经被回收了
        ngx_log_stderr(0,"插座不等,%L--%L",pMsgHeader->iCurrsequence,pConn->iCurrsequence);
    }
    else
    {
        ngx_log_stderr(0,"插座相等哦,%L--%L",pMsgHeader->iCurrsequence,pConn->iCurrsequence);
    }

    */
    // ngx_log_stderr(0,"执行了CLogicSocket::_HandleRegister()并返回结果!");

    // 正确返回
    return true;
}

/***************************************************************
 *  @brief     登录，业务逻辑处理函数
 *  @param     pConn    连接池中连接的指针
 *  @param     pMsgHeader    消息头指针
 *  @param     pPkgBody    包体指针
 *  @param     iBodyLength    包体长度
 *  @return    true: 正确处理返回 false: 无效包信息，不处理
 *  @note      与 _HandleRegister 十分相似，不再注释
 **************************************************************/
bool CLogicSocket::_HandleLogIn(lpngx_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char *pPkgBody, unsigned short iBodyLength)
{
    if (pPkgBody == NULL)
    {
        return false;
    }
    int iRecvLen = sizeof(STRUCT_LOGIN);
    if (iRecvLen != iBodyLength)
    {
        return false;
    }
    CLock lock(&pConn->logicPorcMutex);

    LPSTRUCT_LOGIN p_RecvInfo = (LPSTRUCT_LOGIN)pPkgBody;
    p_RecvInfo->username[sizeof(p_RecvInfo->username) - 1] = 0;
    p_RecvInfo->password[sizeof(p_RecvInfo->password) - 1] = 0;

    LPCOMM_PKG_HEADER pPkgHeader;
    CMemory *p_memory = CMemory::GetInstance();
    CCRC32 *p_crc32 = CCRC32::GetInstance();

    int iSendLen = sizeof(STRUCT_LOGIN);
    char *p_sendbuf = (char *)p_memory->AllocMemory(m_iLenMsgHeader + m_iLenPkgHeader + iSendLen, false);
    memcpy(p_sendbuf, pMsgHeader, m_iLenMsgHeader);
    pPkgHeader = (LPCOMM_PKG_HEADER)(p_sendbuf + m_iLenMsgHeader);
    pPkgHeader->msgCode = _CMD_LOGIN;
    pPkgHeader->msgCode = htons(pPkgHeader->msgCode);
    pPkgHeader->pkgLen = htons(m_iLenPkgHeader + iSendLen);
    LPSTRUCT_LOGIN p_sendInfo = (LPSTRUCT_LOGIN)(p_sendbuf + m_iLenMsgHeader + m_iLenPkgHeader);
    pPkgHeader->crc32 = p_crc32->Get_CRC((unsigned char *)p_sendInfo, iSendLen);
    pPkgHeader->crc32 = htonl(pPkgHeader->crc32);
    // ngx_log_stderr(0,"成功收到了登录并返回结果！");
    msgSend(p_sendbuf);
    return true;
}

/***************************************************************
 *  @brief     接收并处理客户端发送过来的ping包
 *  @param     pConn    连接池中连接的指针
 *  @param     pMsgHeader    消息头指针
 *  @param     pPkgBody    包体指针
 *  @param     iBodyLength    包体长度
 *  @return    true: 正确处理返回 false: 无效包信息，不处理
 *  @note      调用示例
 **************************************************************/
bool CLogicSocket::_HandlePing(lpngx_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char *pPkgBody, unsigned short iBodyLength)
{
    // 规定心跳包不需要包体，则有包体的认为是非法包
    if (iBodyLength != 0)
        return false;

    // 访问本用户连接，需要互斥
    CLock lock(&pConn->logicPorcMutex);
    // 更新最新的心跳包发送时间
    pConn->lastPingTime = time(NULL);

    // 服务器发送一个仅有包头的数据报给客户端
    SendNoBodyPkgToClient(pMsgHeader, _CMD_PING);

    // ngx_log_stderr(0,"成功收到了心跳包并返回结果！");

    return true;
}

/***************************************************************
 *  @brief     发送没有包体的数据包，即心跳包，给客户端
 *  @param     pMsgHeader    消息头
 *  @param     iMsgCode    消息代码
 **************************************************************/
void CLogicSocket::SendNoBodyPkgToClient(LPSTRUC_MSG_HEADER pMsgHeader, unsigned short iMsgCode)
{
    // 内存对象
    CMemory *p_memory = CMemory::GetInstance();

    // 分配内存
    char *p_sendbuf = (char *)p_memory->AllocMemory(m_iLenMsgHeader + m_iLenPkgHeader, false);
    char *p_tmpbuf = p_sendbuf;

    // 拷贝消息头和包头
    memcpy(p_tmpbuf, pMsgHeader, m_iLenMsgHeader);
    // 指针移动到包头位置
    p_tmpbuf += m_iLenMsgHeader;

    // 指向待发送的包头
    LPCOMM_PKG_HEADER pPkgHeader = (LPCOMM_PKG_HEADER)p_tmpbuf;

    // 转换为网络序
    pPkgHeader->msgCode = htons(iMsgCode);
    pPkgHeader->pkgLen = htons(m_iLenPkgHeader);
    pPkgHeader->crc32 = 0;

    // 放入发送队列
    msgSend(p_sendbuf);

    return;
}

/***************************************************************
 *  @brief     检测心跳包是否超时
 *  @param     tmpmsg    消息头
 *  @param     cur_time    当前时间
 **************************************************************/
void CLogicSocket::procPingTimeOutChecking(LPSTRUC_MSG_HEADER tmpmsg, time_t cur_time)
{
    // 内存对象
    CMemory *p_memory = CMemory::GetInstance();

    // 连接未断开
    if (tmpmsg->iCurrsequence == tmpmsg->pConn->iCurrsequence)
    {
        // 取出消息头中指向的指针
        lpngx_connection_t p_Conn = tmpmsg->pConn;

        // 是否踢出
        if (/*m_ifkickTimeCount == 1 && */ m_ifTimeOutKick == 1)
        {
            // 到时间直接踢出去的需求
            zdClosesocketProc(p_Conn);
        }
        // 超出规定时间仍未发送心跳包
        else if ((cur_time - p_Conn->lastPingTime) > (m_iWaitTime * 3 + 10))
        {
            // 踢出去【如果此时此刻该用户正好断线，则这个socket可能立即被后续上来的连接复用  如果真有人这么倒霉，赶上这个点了，那么可能错踢，错踢就错踢】
            // ngx_log_stderr(0,"时间到不发心跳包，踢出去!");   //感觉OK
            zdClosesocketProc(p_Conn);
        }

        // 释放内存
        p_memory->FreeMemory(tmpmsg);
    }
    // 连接已断开
    else
    {
        // 释放内存
        p_memory->FreeMemory(tmpmsg);
    }

    return;
}
