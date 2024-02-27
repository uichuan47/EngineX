// 本文件存放和开启子进程相关的函数实现

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

// 本文件内函数声明

static void ngx_start_worker_processes(int threadnums);
static int ngx_spawn_process(int threadnums, const char *pprocname);
static void ngx_worker_process_cycle(int inum, const char *pprocname);
static void ngx_worker_process_init(int inum);

// 变量声明

// 主进程标题
static u_char master_process[] = "master process";

/***************************************************************
 *  @brief     创建worker子进程
 **************************************************************/
void ngx_master_process_cycle()
{
    // 临时变量，信号集，用于屏蔽信号
    sigset_t set;

    // 清空信号集
    sigemptyset(&set);

    // 在执行本函数时，屏蔽下列信号，防止由信号中断，fork()子进程时采用这种方法可以有效防止信号的干扰，保护不希望由信号中断的代码临界区
    sigaddset(&set, SIGCHLD);  // 子进程状态改变
    sigaddset(&set, SIGALRM);  // 定时器超时
    sigaddset(&set, SIGIO);    // 异步I/O
    sigaddset(&set, SIGINT);   // 终端中断符
    sigaddset(&set, SIGHUP);   // 连接断开
    sigaddset(&set, SIGUSR1);  // 用户定义信号
    sigaddset(&set, SIGUSR2);  // 用户定义信号
    sigaddset(&set, SIGWINCH); // 终端窗口大小改变
    sigaddset(&set, SIGTERM);  // 终止
    sigaddset(&set, SIGQUIT);  // 终端退出符
    // 根据开发实际需要往其中添加其他要屏蔽的信号

    // 通过设置此时无法接受的信号，在阻塞期间，接收到的上述信号多个会被合并为一个暂存，解开信号屏蔽后才能收到这些信号

    // SIG_BLOCK 表明设置进程新的信号屏蔽字为 当前信号屏蔽字 和 第二个参数指向的信号集 的并集
    if (sigprocmask(SIG_BLOCK, &set, NULL) == -1)
    {
        // 设置阻塞信号失败则输出错误信息到日志
        ngx_log_error_core(NGX_LOG_ALERT, errno, "ngx_master_process_cycle()中sigprocmask()失败!");
    }

    // 即便sigprocmask失败，程序流程仍然继续，或者屏蔽信号成功

    // 设置主进程标题
    size_t size;
    int i;
    // 主进程标题长度
    size = sizeof(master_process);
    // argv 参数长度加进来
    size += g_argvneedmem;

    // 长度符合要求才设置
    if (size < 1000)
    {
        char title[1000] = {0};
        //"master process"
        strcpy(title, (const char *)master_process);
        // 加入空格，"master process "
        strcat(title, " ");
        // 加入 argv 参数，"master process ./nginx"
        for (i = 0; i < g_os_argc; i++)
        {
            strcat(title, g_os_argv[i]);
        } // end for

        // 设置标题
        ngx_setproctitle(title);
        // 设置标题后同时输出日志信息（进程id等）到文件
        ngx_log_error_core(NGX_LOG_NOTICE, 0, "%s %P  master进程 启动并开始运行......!", title, ngx_pid);
    }

    // 主进程标题设置成功

    // 开始创建 worker 子进程

    // 从配置文件中读取待创建的 worker 子进程数量
    CConfig *p_config = CConfig::GetInstance();
    int workprocess = p_config->GetIntDefault("WorkerProcesses", 1);

    // 创建指定数量的 worker 子进程
    ngx_start_worker_processes(workprocess);

    // 子进程创建成功后，主进程返回此处，取消信号屏蔽
    sigemptyset(&set);

    // 主进程进入循环，阻塞挂起，仅当信号来时才被唤醒
    for (;;)
    {

        // usleep(100000);
        // ngx_log_error_core(0, 0, "haha--这是父进程, pid为%P", ngx_pid);

        // sigsuspend(const sigset_t *mask))用于在接收到某个信号之前, 临时用mask替换进程的信号掩码, 并暂停进程执行，直到收到信号为止。
        // sigsuspend 返回后将恢复调用之前的信号掩码。信号处理函数完成后，进程将继续执行。该系统调用始终返回-1，并将errno设置为EINTR。

        // sigsuspend是一个原子操作，包含4个步骤：
        // a)根据给定的参数设置新的 mask 并阻塞当前进程【因为是个空集，所以不阻塞任何信号】
        // b)此时，一旦收到信号，便恢复原先的信号屏蔽【我们原来调用sigprocmask()的mask在上边设置的，阻塞了多达10个信号，从而保证我下边的执行流程不会再次被其他信号截断】
        // c)调用该信号对应的信号处理函数
        // d)信号处理函数返回后，sigsuspend返回，使程序流程继续往下走
        // printf("for进来了！\n"); //发现，如果print不加\n，无法及时显示到屏幕上，是行缓存问题，以往没注意；可参考https://blog.csdn.net/qq_26093511/article/details/53255970

        // 阻塞在这里，等待一个信号，此时进程是挂起的，不占用cpu时间，只有收到信号才会被唤醒（返回）；
        // 此时master进程完全靠信号驱动干活
        sigsuspend(&set);

        // printf("执行一次 sigsuspend() \n");

        // printf("master 进程休息1秒\n");
        // ngx_log_stderr(0,"haha--这是父进程，pid为%P",ngx_pid);

        sleep(1); // 休息1秒

        // 以后扩充.......

    } // end for(;;)

    return;
}

/***************************************************************
 *  @brief     创建指定数量的子进程
 *  @param     threadnums    待创建的子进程数量
 **************************************************************/
static void ngx_start_worker_processes(int threadnums)
{
    // 循环创建，每次创建一个子进程
    for (int i = 0; i < threadnums; i++)
    {
        // 创建一个子进程并指定子进程标题
        ngx_spawn_process(i, "worker process");
    } // end for

    return;
}

/***************************************************************
 *  @brief     创建一个子进程
 *  @param     inum         子进程序号
 *  @param     pprocname    子进程标题
 *  @return    -1 表示出错，> 0 表示子进程 pid
 **************************************************************/
static int ngx_spawn_process(int inum, const char *pprocname)
{
    // 临时变量
    pid_t pid;

    // fork() 系统调用产生一个子进程
    pid = fork();

    // 根据不同进程中返回的 pid 不同，使得父子进程执行不同逻辑
    switch (pid)
    {
    case -1: // 产生子进程失败

        // 输出错误信息到日志，返回错误值
        ngx_log_error_core(NGX_LOG_ALERT, errno, "ngx_spawn_process()fork()产生子进程num=%d,procname=\"%s\"失败!", inum, pprocname);
        return -1;

    case 0: // 子进程分支
        // 子进程中，修改对应的 pid 值
        ngx_parent = ngx_pid;
        // 重新获取当前 pid，即本子进程的 pid
        ngx_pid = getpid();
        // 产生出的子进程立刻进入工作函数中（循环）
        ngx_worker_process_cycle(inum, pprocname);
        // 子进程在上述函数中循环工作，不会继续向下执行
        break;

    default: // 这个应该是父进程分支，直接break;，流程往switch之后走
        break;
    } // end switch

    // 父进程分支会走到这里，子进程流程不往下边走-------------------------
    // 若有需要，以后再扩展增加其他代码......

    return pid;
}

/***************************************************************
 *  @brief     对创建出的子进程进行初始化，在子进程工作前被调用
 *  @param     inum    子进程序号
 **************************************************************/
static void ngx_worker_process_init(int inum)
{
    // 临时变量，信号集
    sigset_t set;

    // 清空信号集
    sigemptyset(&set);
    // 在主进程中屏蔽了很多信号，在此取消屏蔽，保证正常工作
    if (sigprocmask(SIG_SETMASK, &set, NULL) == -1)
    {
        // 取消失败则输出信息到日志文件
        ngx_log_error_core(NGX_LOG_ALERT, errno, "ngx_worker_process_init()中sigprocmask()失败!");
    }

    // 信号取消屏蔽设置成功
    // 开始对工作环境初始化

    // 最先创建线程池代码
    // 读取配置文件中线程池的创建参数
    CConfig *p_config = CConfig::GetInstance();
    // 处理接收到的消息的线程池中线程数量
    int tmpthreadnums = p_config->GetIntDefault("ProcMsgRecvWorkThreadCount", 5);
    // 创建指定数量线程的线程池
    if (g_threadpool.Create(tmpthreadnums) == false)
    {
        // 创建失败，可能是内存原因，强制退出
        exit(-2);
    }
    // 休息 1s
    sleep(1);

    // 线程池初始化完成

    // 多线程能力相关信息初始化
    if (g_socket.Initialize_subproc() == false) // 初始化子进程需要具备的一些多线程能力相关的信息
    {
        // 内存没释放，但是简单粗暴退出；
        exit(-2);
    }

    // 初始化epoll相关内容，同时向监听socket上增加监听事件，从而开始让监听端口履行其职责
    g_socket.ngx_epoll_init();
    // g_socket.ngx_epoll_listenportstart();
    // 往监听socket上增加监听事件，从而开始让监听端口履行其职责
    // 如果不加这行，虽然端口能连上，但不会触发ngx_epoll_process_events()里边的epoll_wait()往下走

    //....将来再扩充代码
    //....
    return;
}

/***************************************************************
 *  @brief     worker 进程的工作函数，子进程被创建后在此函数中不断循环
 *  @param     inum         子进程序号
 *  @param     pprocname    子进程标题
 **************************************************************/
static void ngx_worker_process_cycle(int inum, const char *pprocname)
{
    // 设置进程类型为 worker 进程
    ngx_process = NGX_PROCESS_WORKER;

    // 在开始工作前完成初始化设置

    // 初始化子进程工作环境（需要用到的变量）
    ngx_worker_process_init(inum);
    // 设置子进程标题
    ngx_setproctitle(pprocname);
    // 输出子进程创建信息到日志文件
    // 设置标题时顺便记录下来进程名，进程id等信息到日志
    ngx_log_error_core(NGX_LOG_NOTICE, 0, "%s %P 【worker进程】启动并开始运行......!", pprocname, ngx_pid);

    // 测试代码，测试线程池的关闭
    // sleep(5); //休息5秒
    // g_threadpool.StopAll(); //测试Create()后立即释放的效果

    // 暂时先放个死循环，我们在这个循环里一直不出来
    // setvbuf(stdout,NULL,_IONBF,0); //这个函数. 直接将printf缓冲区禁止， printf就直接输出了

    // 初始化完毕，创建完毕，进入工作循环
    for (;;)
    {

        // 先sleep一下 以后扩充.......printf("worker进程休息1秒");
        // fflush(stdout); // 刷新标准输出缓冲区，把输出缓冲区里的东西打印到标准输出设备上，则printf里的东西会立即输出；
        // sleep(1);       // 休息1秒

        // usleep(100000);
        // ngx_log_error_core(0, 0, "good--这是子进程，编号为%d,pid为%P！", inum, ngx_pid);
        // printf("1212");

        // if (inum == 1)
        // {
        //     ngx_log_stderr(0, "good--这是子进程，编号为%d,pid为%P", inum, ngx_pid);
        //     printf("good--这是子进程，编号为%d,pid为%d\r\n", inum, ngx_pid);
        //     ngx_log_error_core(0, 0, "good--这是子进程，编号为%d", inum, ngx_pid);
        //     printf("我的测试哈inum=%d", inum++);
        //     fflush(stdout);
        // }

        // ngx_log_stderr(0, "good--这是子进程，编号为%d,pid为%P", inum, ngx_pid);
        // ngx_log_error_core(0, 0, "good--这是子进程，编号为%d,pid为%P", inum, ngx_pid);

        // 处理网络事件和定时器事件
        ngx_process_events_and_timers();

        // 发生意外时，优雅退出
        // if (false)
        // {
        //     g_stopEvent = 1;
        //     break;
        // }

    } // end for(;;)

    // 跳出工作循环，表名进程结束

    // 终止线程池
    g_threadpool.StopAll();

    // 释放 socket 相关内容
    g_socket.Shutdown_subproc();

    return;
}
