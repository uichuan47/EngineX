// 本文件存放网络中信息（ip地址等信息）转换相关的函数实现

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
 *  @brief     将 socket 绑定的地址信息（信标签）转换为文本格式，即根据参数1给定的信息，获取地址端口字符串，返回这个字符串的长度
 *  @param     sa    客户端的 ip 等信息（信标签）
 *  @param     port    是否包含端口信息，1：包含端口信息到字符串，0：不包含端口信息
 *  @param     text    存放书写后的文本信息
 *  @param     len    最大文本信息宽度
 *  @return    字符串长度
 *  @note      调用示例
 **************************************************************/
size_t CSocekt::ngx_sock_ntop(struct sockaddr *sa, int port, u_char *text, size_t len)
{
    // 临时变量
    struct sockaddr_in *sin;
    u_char *p;

    // 根据不同协议族进行不同处理
    switch (sa->sa_family)
    {
    case AF_INET: // IPV4
        sin = (struct sockaddr_in *)sa;
        p = (u_char *)&sin->sin_addr;

        // 包含端口信息
        if (port)
        {
            p = ngx_snprintf(text, len, "%ud.%ud.%ud.%ud:%d", p[0], p[1], p[2], p[3], ntohs(sin->sin_port));
        }
        else
        {
            p = ngx_snprintf(text, len, "%ud.%ud.%ud.%ud", p[0], p[1], p[2], p[3]);
        }

        return (p - text);
        break;

    default:

        return 0;
        break;
    }

    return 0;
}
