// 本文件存放子进程核心工作函数的函数实现

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h> //信号相关头文件
#include <errno.h>  //errno
#include <unistd.h>

#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_c_conf.h"

/***************************************************************
 *  @brief     处理网络事件和定时器事件，在子进程循环中不断调用此函数
 **************************************************************/
void ngx_process_events_and_timers()
{
    // 调用 socket 相关函数，-1 表示阻塞在此处
    g_socket.ngx_epoll_process_events(-1);

    // 统计信息打印
    g_socket.printTDInfo();

    // ...再完善
}
