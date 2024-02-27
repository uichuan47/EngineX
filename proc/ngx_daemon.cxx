// 本文件存放用于将进程设置为守护进程的函数实现

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <unistd.h>
#include <errno.h> //errno
#include <sys/stat.h>
#include <fcntl.h>

#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_c_conf.h"

/***************************************************************
 *  @brief     守护进程初始化
 *  @return    失败返回 -1，子进程返回 0，父进程返回 1
 **************************************************************/
int ngx_daemon()
{

    // 首先 fork() 一个子进程，作为真正的主进程 master
    switch (fork())
    {
    case -1:
        // 创建子进程失败，输出错误信息到日志文件
        ngx_log_error_core(NGX_LOG_EMERG, errno, "ngx_daemon()中fork()失败!");
        // 直接返回错误代码
        return -1;
    case 0:
        // 子进程，直接跳出，执行后续逻辑
        break;
    default:
        // 原先的父进程，直接返回父进程代码 1 并退出，回到主函数中释放资源
        return 1;
    } // end switch

    // fork() 出的子进程继续执行

    // 更改进程 id
    // ngx_pid是原来父进程的id，因为这里是子进程，所以子进程的ngx_parent设置为原来父进程的pid
    ngx_parent = ngx_pid;
    // 重新取得当前子进程的 id
    ngx_pid = getpid();

    // 将当前进程脱离终端，此后终端关闭将于本进程无关
    if (setsid() == -1)
    {
        // 设置失败的输出错误信息到日志文件
        ngx_log_error_core(NGX_LOG_EMERG, errno, "ngx_daemon()中setsid()失败!");
        return -1;
    }

    // 取消文件权限限制，避免混乱
    umask(0);

    // 读写方式打开黑洞设备
    int fd = open("/dev/null", O_RDWR);

    // 打开失败则输出错误信息到日志并返回错误代码
    if (fd == -1)
    {
        ngx_log_error_core(NGX_LOG_EMERG, errno, "ngx_daemon()中open(\"/dev/null\")失败!");
        return -1;
    }
    // 先关闭STDIN_FILENO（这是规矩，已经打开的描述符，动他之前，先close），类似于指针指向null，让/dev/null成为标准输入
    if (dup2(fd, STDIN_FILENO) == -1)
    {
        ngx_log_error_core(NGX_LOG_EMERG, errno, "ngx_daemon()中dup2(STDIN)失败!");
        return -1;
    }
    // 再关闭STDIN_FILENO，类似于指针指向null，让/dev/null成为标准输出
    if (dup2(fd, STDOUT_FILENO) == -1)
    {
        ngx_log_error_core(NGX_LOG_EMERG, errno, "ngx_daemon()中dup2(STDOUT)失败!");
        return -1;
    }

    // 判断是否成功关闭
    if (fd > STDERR_FILENO) // fd应该是3，这个应该成立
    {
        // 释放资源，文件描述符可以被复用
        if (close(fd) == -1)
        {
            // 输出错误信息到日志文件
            ngx_log_error_core(NGX_LOG_EMERG, errno, "ngx_daemon()中close(fd)失败!");
            return -1;
        }
    }

    // 子进程返回0
    return 0;
}
