
// Linux C++ 服务器通讯架构，程序入口

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/time.h> //gettimeofday

#include "ngx_macro.h"        //各种宏定义
#include "ngx_func.h"         //各种函数声明
#include "ngx_c_conf.h"       //和配置文件处理相关的类,名字带c_表示和类有关
#include "ngx_c_socket.h"     //和socket通讯相关
#include "ngx_c_memory.h"     //和内存分配释放等相关
#include "ngx_c_threadpool.h" //和多线程有关
#include "ngx_c_crc32.h"      //和crc32校验算法有关
#include "ngx_c_slogic.h"     //和socket通讯相关

// 本文件内使用的函数声明

static void freeresource();

// 和设置标题有关的全局量
size_t g_argvneedmem = 0; // 保存下这些argv参数所需要的内存大小
size_t g_envneedmem = 0;  // 环境变量所占内存大小
int g_os_argc;            // 参数个数
char **g_os_argv;         // 原始命令行参数数组,在main中会被赋值
char *gp_envmem = NULL;   // 指向自己分配的env环境变量的内存，在ngx_init_setproctitle()函数中会被分配内存
int g_daemonized = 0;     // 守护进程标记，标记是否启用了守护进程模式，0：未启用，1：启用了

// socket/线程池相关

// CSocekt g_socket;         // socket全局对象
CLogicSocket g_socket;    // socket全局对象
CThreadPool g_threadpool; // 线程池全局对象

// 和进程本身有关的全局量

pid_t ngx_pid;    // 当前进程的pid
pid_t ngx_parent; // 父进程的pid
int ngx_process;  // 进程类型，比如master,worker进程等
int g_stopEvent;  // 标志程序退出,0不退出1，退出

sig_atomic_t ngx_reap; // 标记子进程状态变化[一般是子进程发来SIGCHLD信号表示退出],sig_atomic_t:系统定义的类型：访问或改变这些变量需要在计算机的一条指令内完成
                       // 一般等价于int【通常情况下，int类型的变量通常是原子访问的，也可以认为 sig_atomic_t就是int类型的数据】

int main(int argc, char *const *argv)
{
    // printf("%u,%u,%u",EPOLLERR ,EPOLLHUP,EPOLLRDHUP);
    // exit(0);

    int exitcode = 0; // 退出代码，先给0表示正常退出
    int i;            // 临时用

    // 初始化控制程序运行结束的变量
    g_stopEvent = 0; // 标记程序是否退出，0不退出

    // 取得进程pid
    ngx_pid = getpid();
    // 取得父进程的id
    ngx_parent = getppid();

    // 统计argv所占的内存
    g_argvneedmem = 0;
    for (i = 0; i < argc; i++)
    {
        g_argvneedmem += strlen(argv[i]) + 1;
    }

    // 统计环境变量所占的内存
    for (i = 0; environ[i]; i++)
    {
        g_envneedmem += strlen(environ[i]) + 1;
    }

    // 保存参数个数
    g_os_argc = argc;
    // 保存参数指针
    g_os_argv = (char **)argv;

    // 初始化全局变量

    //-1：表示日志文件尚未打开
    ngx_log.fd = -1;
    // 标记本进程为 master 进程
    ngx_process = NGX_PROCESS_MASTER;
    // 标记子进程无变化
    ngx_reap = 0;

    // 完成初始化

    // 读取配置文件
    CConfig *p_config = CConfig::GetInstance();
    // 加载配置文件
    if (p_config->Load("nginx.conf") == false)
    {
        ngx_log_init();
        ngx_log_stderr(0, "配置文件[%s]载入失败，退出!", "nginx.conf");
        exitcode = 2;
        goto lblexit;
    }

    // 初始化单例类
    CMemory::GetInstance();
    CCRC32::GetInstance();

    // 日志初始化
    ngx_log_init();

    // 初始化函数

    // 信号初始化
    if (ngx_init_signals() != 0)
    {
        exitcode = 1;
        goto lblexit;
    }

    // socket 初始化
    if (g_socket.Initialize() == false)
    {
        exitcode = 1;
        goto lblexit;
    }

    // 处理环境变量
    ngx_init_setproctitle();

    // 创建守护进程
    if (p_config->GetIntDefault("Daemon", 0) == 1)
    {
        // 1：按守护进程方式运行
        int cdaemonresult = ngx_daemon();
        if (cdaemonresult == -1) // fork()失败
        {
            exitcode = 1; // 标记失败
            goto lblexit;
        }
        if (cdaemonresult == 1)
        {
            // 这是原始的父进程，直接退出
            freeresource(); // 只有进程退出了才goto到 lblexit，用于提醒用户进程退出了
                            // 而我现在这个情况属于正常fork()守护进程后的正常退出，不应该跑到lblexit()去执行，因为那里有一条打印语句标记整个进程的退出，这里不该限制该条打印语句；
            exitcode = 0;
            return exitcode; // 整个进程直接在这里退出
        }
        // 走到这里，成功创建了守护进程并且这里已经是fork()出来的进程，现在这个进程做了master进程
        g_daemonized = 1; // 守护进程标记，标记是否启用了守护进程模式，0：未启用，1：启用了
    }

    // 产生的子进程作为 master 进程
    ngx_master_process_cycle();

    //--------------------------------------------------------------
    // for(;;)
    //{
    //    sleep(1); //休息1秒
    //    printf("休息1秒\n");
    //}

    //--------------------------------------
lblexit:
    //(5)该释放的资源要释放掉
    ngx_log_stderr(0, "程序退出，再见了!");
    freeresource(); // 一系列的main返回前的释放动作函数
    // printf("程序退出，再见!\n");
    return exitcode;
}

/***************************************************************
 *  @brief     释放函数资源
 **************************************************************/
void freeresource()
{
    //(1)对于因为设置可执行程序标题导致的环境变量分配的内存，我们应该释放
    if (gp_envmem)
    {
        delete[] gp_envmem;
        gp_envmem = NULL;
    }

    //(2)关闭日志文件
    if (ngx_log.fd != STDERR_FILENO && ngx_log.fd != -1)
    {
        close(ngx_log.fd); // 不用判断结果了
        ngx_log.fd = -1;   // 标记下，防止被再次close吧
    }
}
