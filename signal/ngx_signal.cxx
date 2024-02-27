// 本文件存放信号相关的函数实现

#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>   //信号相关头文件
#include <errno.h>    //errno
#include <sys/wait.h> //waitpid

#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_func.h"

// 信号相关结构体
typedef struct
{
    // 信号对应的宏定义编号
    int signo;
    // 信号名称
    const char *signame;

    // 信号处理函数的函数指针，参数和返回值由操作系统规定，siginfo_t 是系统定义的结构
    void (*handler)(int signo, siginfo_t *siginfo, void *ucontext);
} ngx_signal_t;

// 信号处理函数，仅本文件内使用

/***************************************************************
 *  @brief     信号处理函数，参数和返回值由系统规定
 *  @param     signo    信号编号
 *  @param     siginfo    系统定义的结构,包含信号产生原因的有关信息
 *  @param     ucontext
 *  @return    返回值
 *  @note      调用示例
 **************************************************************/
static void ngx_signal_handler(int signo, siginfo_t *siginfo, void *ucontext);
// 获取子进程的结束状态，防止单独kill子进程时子进程变成僵尸进程
static void ngx_process_get_status(void);

// 将需要处理的全部信号以数组形式存储，方便后续绑定
ngx_signal_t signals[] = {
    // signo     signame    handler
    {SIGHUP, "SIGHUP", ngx_signal_handler},   // 终端断开信号，对于守护进程常用于reload重载配置文件通知--标识1
    {SIGINT, "SIGINT", ngx_signal_handler},   // 标识2
    {SIGTERM, "SIGTERM", ngx_signal_handler}, // 标识15
    {SIGCHLD, "SIGCHLD", ngx_signal_handler}, // 子进程退出时，父进程会收到这个信号--标识17
    {SIGQUIT, "SIGQUIT", ngx_signal_handler}, // 标识3
    {SIGIO, "SIGIO", ngx_signal_handler},     // 指示一个异步I/O事件【通用异步I/O信号】
    {SIGSYS, "SIGSYS,SIG_IGN", NULL},         // 无效系统调用，忽略，否则进程会被操作系统杀死，--标识31

    //...日后根据需要再继续增加

    {0, NULL, NULL} // 信号内容截止
};

/***************************************************************
 *  @brief     初始化信号函数，注册各个信号处理函数
 *  @return    0 成功，-1 失败
 **************************************************************/
int ngx_init_signals()
{
    // 系统定义的临时变量，用于保存信号相关信息，进行注册
    struct sigaction sa;

    // 遍历数组中各个型号，将信息逐个保存在 sa 中，进行注册
    for (ngx_signal_t *sig = signals; sig->signo != 0; sig++)
    {
        // 清空内存
        memset(&sa, 0, sizeof(struct sigaction));

        // 信号处理函数不为空
        if (sig->handler)
        {
            // 成员变量设置为指定的函数，sa_sigaction: 指定信号处理程序(函数)
            sa.sa_sigaction = sig->handler;

            // 设定标记，表示选择使用 sa_sigaction 的函数，否则默认使用另一个成员变量的函数指针
            // sa_flags：int型，指定信号处理函数的选项，设置了该标记(SA_SIGINFO)，就表示信号附带的参数可以被传递到信号处理函数中
            sa.sa_flags = SA_SIGINFO;

            // 简而言之，若使得 sa.sa_sigaction 指定的信号处理程序(函数)生效，就需要sa_flags设定为SA_SIGINFO
        }
        // 未指定信号处理函数
        else
        {
            // sa_handler:这个标记SIG_IGN给到sa_handler成员，表示忽略信号的处理程序，否则操作系统的缺省信号处理程序很可能把这个进程杀掉；
            sa.sa_handler = SIG_IGN;

            // 实际上，sa_handler 和 sa_sigaction 都是指向信号处理函数的函数指针，但参数不同
            // sa_sigaction 携带参数较多，sa_handler 携带参数少
            // 若需要使用 sa_sigaction，就需要将 sa_flags 设置为 SA_SIGINFO

        } // end if

        // 设置处理当前保存信号时需要阻塞的信号，不设置即为清空，表示不阻塞任何信号
        sigemptyset(&sa.sa_mask);

        // sa_mask是个信号集（描述信号的集合），用于表示要阻塞的信号
        // 把信号集中的所有信号清0，本意就是不准备阻塞任何信号；

        // 注册信号及其对应的信号处理函数
        // signo: 待注册信号，sa: 信号处理函数及处理方式，NULL: 是否返回以往的对信号的处理方式
        if (sigaction(sig->signo, &sa, NULL) == -1)
        {
            // 注册失败直接写入日志
            ngx_log_error_core(NGX_LOG_EMERG, errno, "sigaction(%s) failed", sig->signame);
            // 退出
            return -1;
        }
        else
        {
            // ngx_log_error_core(NGX_LOG_EMERG,errno,"sigaction(%s) succed!",sig->signame);     // 注册成功无需记录日志
            // ngx_log_stderr(0,"sigaction(%s) succed!",sig->signame);                           // 将成功注册信息输出到屏幕
        }
    } // end for

    return 0; // 成功
}

/***************************************************************
 *  @brief     信号处理函数，参数和返回值由系统规定
 *  @param     signo    信号编号
 *  @param     siginfo    系统定义的结构,包含信号产生原因的有关信息
 *  @param     ucontext
 **************************************************************/
static void ngx_signal_handler(int signo, siginfo_t *siginfo, void *ucontext)
{
    // 收到信号时会自动调用本函数进行处理
    // 根据设定，本函数根据传入信号不同，分别作出不同处理

    // 临时变量
    ngx_signal_t *sig;

    // 一个字符串，用于记录一个动作字符串以往日志文件中写
    char *action;

    // 遍历信号数组，
    for (sig = signals; sig->signo != 0; sig++)
    {
        // 找到对应信号，即可处理
        if (sig->signo == signo)
        {
            break;
        }
    } // end for

    action = (char *)""; // 目前还没有什么动作；

    // master进程，管理进程，处理信号较多
    if (ngx_process == NGX_PROCESS_MASTER) //
    {
        // master进程在此处理
        switch (signo)
        {
        case SIGCHLD:     // 一般子进程退出会收到该信号
            // 全局变量
            ngx_reap = 1; // 标记子进程状态变化，日后master主进程的for(;;)循环中可能会用到这个变量【比如重新产生一个子进程】
            break;

            //.....其他信号处理以后待增加

        default:
            break;
        } // end switch
    }
    else if (ngx_process == NGX_PROCESS_WORKER) // worker进程，具体干活的进程，处理的信号相对比较少
    {
        // worker进程的往这里走
        //......以后再增加
        //....
    }
    else
    {
        // 非master非worker进程，先啥也不干
        // do nothing
    } // end if(ngx_process == NGX_PROCESS_MASTER)

    // 记录一些日志信息
    // siginfo这个
    if (siginfo && siginfo->si_pid) // si_pid = sending process ID【发送该信号的进程id】
    {
        ngx_log_error_core(NGX_LOG_NOTICE, 0, "signal %d (%s) received from %P%s", signo, sig->signame, siginfo->si_pid, action);
    }
    else
    {
        ngx_log_error_core(NGX_LOG_NOTICE, 0, "signal %d (%s) received %s", signo, sig->signame, action); // 没有发送该信号的进程id，所以不显示发送该信号的进程id
    }

    //.......其他需要扩展的将来再处理；

    // 子进程状态有变化，通常是意外退出【既然官方是在这里处理，我们也学习官方在这里处理】
    if (signo == SIGCHLD)
    {
        ngx_process_get_status(); // 获取子进程的结束状态
    }                             // end if

    return;
}

/***************************************************************
 *  @brief     获取子进程的结束状态，防止单独kill子进程时子进程变成僵尸进程
 *  @return    返回值
 *  @note      调用示例
 **************************************************************/
static void ngx_process_get_status(void)
{
    // 临时变量
    pid_t pid;
    int status;
    int err;

    // 参考官方nginx，应该是标记信号正常处理过一次
    int one = 0;

    // 杀死一个子进程时，父进程会收到 SIGCHLD 信号
    for (;;)
    {
        // waitpid，有人也用wait,但老师要求大家掌握和使用waitpid即可；这个waitpid说白了获取子进程的终止状态，这样，子进程就不会成为僵尸进程了；
        // 第一次waitpid返回一个> 0值，表示成功，后边显示 2019/01/14 21:43:38 [alert] 3375: pid = 3377 exited on signal 9【SIGKILL】
        // 第二次再循环回来，再次调用waitpid会返回一个0，表示子进程还没结束，然后这里有return来退出；

        // 获取子进程状态
        // 第一个参数为-1，表示等待任何子进程，
        // 第二个参数：保存子进程的状态信息
        // 第三个参数：提供额外选项，WNOHANG 表示非阻塞，waitpid() 立即返回
        pid = waitpid(-1, &status, WNOHANG);

        // 子进程未结束，立刻返回
        if (pid == 0)
        {
            return;
        } // end if(pid == 0)

        // waitpid 调用发生错误
        if (pid == -1)
        {
            // 输出日志
            err = errno;

            if (err == EINTR) // 调用被某个信号中断
            {
                continue;
            }

            if (err == ECHILD && one) // 没有子进程
            {
                return;
            }

            if (err == ECHILD) // 没有子进程
            {
                ngx_log_error_core(NGX_LOG_INFO, err, "waitpid() failed!");
                return;
            }

            // 输出错误信息到日志
            ngx_log_error_core(NGX_LOG_ALERT, err, "waitpid() failed!");

            return;
        } // end if(pid == -1)

        // 成功返回子进程 id ，打印日志记录子进程的退出

        // 标记waitpid()返回了正常的返回值
        one = 1;

        // 获取使子进程终止的信号编号
        if (WTERMSIG(status))
        {
            // 获取使子进程终止的信号编号
            ngx_log_error_core(NGX_LOG_ALERT, 0, "pid = %P exited on signal %d!", pid, WTERMSIG(status));
        }
        else
        {
            // WEXITSTATUS()获取子进程传递给exit或者_exit参数的低八位
            ngx_log_error_core(NGX_LOG_NOTICE, 0, "pid = %P exited with code %d!", pid, WEXITSTATUS(status));
        }

    } // end for

    return;
}
