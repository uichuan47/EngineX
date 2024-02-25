// 本文件存放日志相关的函数实现

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>   //uintptr_t
#include <stdarg.h>   //va_start....
#include <unistd.h>   //STDERR_FILENO等
#include <sys/time.h> //gettimeofday
#include <time.h>     //localtime_r
#include <fcntl.h>    //open
#include <errno.h>    //errno

#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_func.h"
#include "ngx_c_conf.h"

// 全局变量

// 错误等级数组，依次存放各项错误，与宏定义一一对应
static u_char err_levels[][20] =
    {
        {"stderr"}, // 0：控制台错误
        {"emerg"},  // 1：紧急
        {"alert"},  // 2：警戒
        {"crit"},   // 3：严重
        {"error"},  // 4：错误
        {"warn"},   // 5：警告
        {"notice"}, // 6：注意
        {"info"},   // 7：信息
        {"debug"}   // 8：调试
};

// 全局变量，保存打开的日志文件相关信息
ngx_log_t ngx_log;

/***************************************************************
 *  @brief     日志初始化，打开日志文件，涉及的释放问题将在 main 函数中解决
 **************************************************************/
void ngx_log_init()
{
    // 初始化指针
    u_char *plogname = NULL;
    // 长度
    size_t nlen;

    // 从配置文件中读取日志相关信息
    CConfig *p_config = CConfig::GetInstance();
    // 读取日志保存文件名
    plogname = (u_char *)p_config->GetString("Log");
    // 若为空则使用默认文件
    if (plogname == NULL)
    {
        // 没读到，就要给个缺省的路径文件名了
        plogname = (u_char *)NGX_ERROR_LOG_PATH; //"logs/error.log" ,logs目录需要提前建立出来
    }

    // 读取设定的日志等级，默认为 6
    ngx_log.log_level = p_config->GetIntDefault("LogLevel", NGX_LOG_NOTICE);
    // nlen = strlen((const char *)plogname);

    // 打开字符串指向的文件，权限为 只写打开|追加到末尾|文件不存在则创建，并设定文件访问权限
    ngx_log.fd = open((const char *)plogname, O_WRONLY | O_APPEND | O_CREAT, 0644);
    // ngx_log.fd = open((const char *)plogname,O_WRONLY|O_APPEND|O_CREAT|O_DIRECT,0644);   //绕过内和缓冲区，write()成功则写磁盘必然成功，但效率可能会比较低；

    // 打开文件错误
    if (ngx_log.fd == -1) // 如果有错误，则直接定位到 标准错误上去
    {
        // 输出错误信息
        ngx_log_stderr(errno, "[alert] could not open error log file: open() \"%s\" failed", plogname);
        // 直接定位到标准错误
        ngx_log.fd = STDERR_FILENO;
    }

    return;
}

/***************************************************************
 *  @brief     将字符串内容(控制台错误)显示到标准错误
 *  @param     err    错误标识，0 表示无错误，都则输出 err 编号的错误信息
 *  @param     fmt    第一个固定参数，多为字符串
 **************************************************************/
void ngx_log_stderr(int err, const char *fmt, ...)
{

    // ngx_log_stderr(0, "invalid option: \"%s\"", argv[0]);           // nginx: invalid option: "./nginx"
    // ngx_log_stderr(0, "invalid option: %10d", 21);                  // nginx: invalid option:         21  ---21前面有8个空格
    // ngx_log_stderr(0, "invalid option: %.6f", 21.378);              // nginx: invalid option: 21.378000   ---%.这种只跟f配合有效，往末尾填充0
    // ngx_log_stderr(0, "invalid option: %.6f", 12.999);              // nginx: invalid option: 12.999000
    // ngx_log_stderr(0, "invalid option: %.2f", 12.999);              // nginx: invalid option: 13.00
    // ngx_log_stderr(0, "invalid option: %xd", 1678);                 // nginx: invalid option: 68E
    // ngx_log_stderr(0, "invalid option: %Xd", 1678);                 // nginx: invalid option: 68E
    // ngx_log_stderr(15, "invalid option: %s , %d", "testInfo", 326); // nginx: invalid option: testInfo , 326
    // ngx_log_stderr(0, "invalid option: %d", 1678);

    // 可变参数集变量
    va_list args;
    // 保存错误信息字符串数组
    u_char errstr[NGX_MAX_ERROR_STR + 1];
    // 移动指针，末尾标识符
    u_char *p, *last;

    // 清空内存
    memset(errstr, 0, sizeof(errstr));

    // last 指向可写的最后一位内存，防止越界
    last = errstr + NGX_MAX_ERROR_STR;

    // 先将固定格式信息拷贝到指定内存中
    p = ngx_cpymem(errstr, "nginx: ", 7); // p指向"nginx: "之后

    // 初始化可变参数集
    va_start(args, fmt);

    // 将字符串和可变参数集组合，保存到指定内存中，p 为游标
    p = ngx_vslprintf(p, last, fmt, args);
    // 释放可变参数集
    va_end(args);

    // 存在错误
    if (err)
    {
        // 显示错误代码和错误信息
        p = ngx_log_errno(p, last, err);
    }

    // 内存不足，强行写入，即便会覆盖
    if (p >= (last - 1))
    {
        p = (last - 1) - 1;
    }

    // 换行符
    *p++ = '\n';

    // 将错误信息输出到标准错误（一般是屏幕）
    write(STDERR_FILENO, errstr, p - errstr);

    // 日志文件正确打开
    if (ngx_log.fd > STDERR_FILENO)
    {
        // 已经输出过错误信息
        err = 0;
        // 去除先前添加的换行符
        p--;
        *p = 0;

        // 将错误信息写入日志文件
        ngx_log_error_core(NGX_LOG_STDERR, err, (const char *)errstr);
    }

    return;
}

/***************************************************************
 *  @brief     将错误编号代表的错误以指定格式写入指定内存中
 *  @param     buf    待写入内存空间足够
 *  @param     last    内存结尾
 *  @param     err    错误编号
 *  @return    写后的内存末尾，下一位可写位置
 **************************************************************/
u_char *ngx_log_errno(u_char *buf, u_char *last, int err)
{
    // 获取错误编号代表的错误
    char *perrorinfo = strerror(err);
    // 错误字符串长度
    size_t len = strlen(perrorinfo);

    // 写入错误编号信息，左侧
    char leftstr[10] = {0};
    sprintf(leftstr, " (%d: ", err);
    // 错误长度
    size_t leftlen = strlen(leftstr);

    // 错误编号消息，右侧
    char rightstr[] = ") ";
    size_t rightlen = strlen(rightstr);

    // 左右额外宽度
    size_t extralen = leftlen + rightlen;
    // 长度允许
    if ((buf + len + extralen) < last)
    {
        buf = ngx_cpymem(buf, leftstr, leftlen);
        buf = ngx_cpymem(buf, perrorinfo, len);
        buf = ngx_cpymem(buf, rightstr, rightlen);
    }

    return buf;
}

/***************************************************************
 *  @brief     将错误信息写入日志文件中
 *  @param     level    日志等级
 *  @param     err    错误编号
 *  @param     fmt    第一个固定参数
 **************************************************************/
void ngx_log_error_core(int level, int err, const char *fmt, ...)
{
    // 内存末尾指针
    u_char *last;
    // 存放待写入的字符串
    u_char errstr[NGX_MAX_ERROR_STR + 1];
    // 清空内存
    memset(errstr, 0, sizeof(errstr));
    // 末尾指针
    last = errstr + NGX_MAX_ERROR_STR;

    //  时间相关变量
    struct timeval tv;
    struct tm tm;
    time_t sec;

    // 指向当前要拷贝数据到其中的内存位置
    u_char *p;
    va_list args;
    // 清空内存
    memset(&tv, 0, sizeof(struct timeval));
    memset(&tm, 0, sizeof(struct tm));
    // 获取当前时间，返回自1970-01-01 00:00:00到现在经历的秒数【第二个参数是时区，一般不关心】
    gettimeofday(&tv, NULL);

    // 调整时间变量
    sec = tv.tv_sec;        // 秒
    localtime_r(&sec, &tm); // 把参数1的time_t转换为本地时间，保存到参数2中去，带_r的是线程安全的版本，尽量使用
    tm.tm_mon++;            // 月份要调整下正常
    tm.tm_year += 1900;     // 年份要调整下才正常

    // 存放当前时间字符串，格式形如：2019/01/08 19:57:11
    u_char strcurrtime[40] = {0};
    // 将日期写入指定内存
    ngx_slprintf(strcurrtime, (u_char *)-1, "%4d/%02d/%02d %02d:%02d:%02d", tm.tm_year, tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

    // 依次写入日志信息
    p = ngx_cpymem(errstr, strcurrtime, strlen((const char *)strcurrtime)); // 日期增加进来，得到形如：     2019/01/08 20:26:07
    p = ngx_slprintf(p, last, " [%s] ", err_levels[level]);                 // 日志级别增加进来，得到形如：  2019/01/08 20:26:07 [crit]
    p = ngx_slprintf(p, last, "%P: ", ngx_pid);                             // 支持%P格式，进程id增加进来，得到形如：   2019/01/08 20:50:15 [crit] 2037:

    // 初始化可变参数集
    va_start(args, fmt);
    // 组合得到可变参数集字符串
    p = ngx_vslprintf(p, last, fmt, args);
    // 释放可变参数集
    va_end(args);

    if (err) // 如果错误代码不是0，表示有错误发生
    {
        // 错误代码和错误信息也要显示出来
        p = ngx_log_errno(p, last, err);
    }

    // 若位置不够，强行插入到末尾
    if (p >= (last - 1))
    {
        p = (last - 1) - 1;
    }
    *p++ = '\n'; // 增加个换行符

    // 这么写代码是图方便：随时可以把流程弄到while后边去；大家可以借鉴一下这种写法
    ssize_t n;
    while (1)
    {
        // 判断日志等级
        if (level > ngx_log.log_level)
        {
            // 日志等级不足，不打印
            break;
        }

        // 将日志写入文件
        n = write(ngx_log.fd, errstr, p - errstr);

        // 写入失败
        if (n == -1)
        {
            // 写失败有问题
            if (errno == ENOSPC) // 写失败，且原因是磁盘没空间了
            {
                // 磁盘空间不足
            }
            else
            {
                // 这是有其他错误，那么我考虑把这个错误显示到标准错误设备吧；
                if (ngx_log.fd != STDERR_FILENO) // 当前是定位到文件的，则条件成立
                {
                    n = write(STDERR_FILENO, errstr, p - errstr);
                }
            }
        }
        break;
    } // end while
    return;
}
