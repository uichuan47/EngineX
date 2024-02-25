// 本文件存放打印格式相关的函数实现

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h> //类型相关头文件

#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_func.h"

// 仅在本文件中使用的函数的函数声明

/***************************************************************
 *  @brief     以指定宽度将数字写入指定内存，宽度取数字宽度与指定宽度的较大值
 *  @param     buf              待写入内存
 *  @param     last             内存末尾标识
 *  @param     ui64             待写入数字
 *  @param     zero             补齐长度字符，默认为' '，也可指定为'0'
 *  @param     hexadecimal      是否以十六进制形式写入，0 为否，1 为十六进制小写，2 为十六进制大写
 *  @param     width            指定宽度
 *  @return    写入字符后的结尾位置
 **************************************************************/
static u_char *ngx_sprintf_num(u_char *buf, u_char *last, uint64_t ui64, u_char zero, uintptr_t hexadecimal, uintptr_t width);

/***************************************************************
 *  @brief     根据自定义数据格式进行标准化格式输出，对 ngx_vslprintf() 函数进行了包装
 *  @param     buf      待写入的内存空间
 *  @param     last     内存末尾标识，防止越界
 *  @param     fmt      可变参数集的第一个固定参数
 *  @return    完成书写的内存空间
 **************************************************************/
u_char *ngx_slprintf(u_char *buf, u_char *last, const char *fmt, ...)
{
    va_list args;
    u_char *p;

    va_start(args, fmt); // 使args指向起始的参数
    p = ngx_vslprintf(buf, last, fmt, args);
    va_end(args); // 释放args
    return p;
}

/***************************************************************
 *  @brief     根据自定义数据格式进行标准化格式输出，对 ngx_vslprintf() 函数进行了包装，与 ngx_snprintf() 十分类似，但更安全
 *  @param     buf    待写入的内存空间
 *  @param     max    可书写的最大长度
 *  @param     fmt    可变参数集的第一个固定参数
 *  @return    完成书写的内存空间
 **************************************************************/
u_char *ngx_snprintf(u_char *buf, size_t max, const char *fmt, ...)
{
    // 临时变量
    u_char *p;
    // 可变参数集
    va_list args;

    // 通过第一个固定参数对可变参数集进行初始化
    va_start(args, fmt);
    // 将可变参数集中的数据写到指定内存空间当中
    p = ngx_vslprintf(buf, buf + max, fmt, args);
    // 释放可变参数集
    va_end(args);

    return p;
}

/***************************************************************
 *  @brief     以指定宽度将数字写入指定内存，宽度取数字宽度与指定宽度的较大值
 *  @param     buf              待写入内存
 *  @param     last             内存末尾标识
 *  @param     ui64             待写入数字
 *  @param     zero             补齐长度字符，默认为' '，也可指定为'0'
 *  @param     hexadecimal      是否以十六进制形式写入，0 为否，1 为十六进制小写，2 为十六进制大写
 *  @param     width            指定宽度
 *  @return    写入字符后的结尾位置
 **************************************************************/
static u_char *ngx_sprintf_num(u_char *buf, u_char *last, uint64_t ui64, u_char zero, uintptr_t hexadecimal, uintptr_t width)
{
    // temp[21]
    // 临时变量，移动指针和临时写入的内存，需要注意尾零占一位长度
    u_char *p, temp[NGX_INT64_LEN + 1];

    // 临时变量
    size_t len;
    // 临时变量
    uint32_t ui32;

    // 字符串数组，十六进制小写
    const static u_char hex[] = "0123456789abcdef";
    // 字符串数组，十六进制大写
    const static u_char HEX[] = "0123456789ABCDEF";

    // p 指向 temp 数组最后一个合法置
    p = temp + NGX_INT64_LEN;

    // 十进制输出
    if (hexadecimal == 0)
    {
        // 小于最大值
        if (ui64 <= (uint64_t)NGX_MAX_UINT32_VALUE)
        {
            // 可以强制类型转换
            ui32 = (uint32_t)ui64;
            do
            {
                // 从内存末尾向前依次写入
                *--p = (u_char)(ui32 % 10 + '0');
            } while (ui32 /= 10); // 每次缩小10倍，去掉个位数字
        }
        else
        {
            do
            {
                *--p = (u_char)(ui64 % 10 + '0');
            } while (ui64 /= 10);
        }
    }
    // 十六进制小写，%xd
    else if (hexadecimal == 1)
    {
        // 比如我显示一个1,234,567【十进制数】，他对应的二进制数实际是 12 D687 ，那怎么显示出这个12D687来呢？
        do
        {
            // 取出最后四位转为十六进制后写入
            *--p = hex[(uint32_t)(ui64 & 0xf)];
        } while (ui64 >>= 4); //  右移四位，即去除最后四位
    }
    // 十六进制大写，%Xd
    else
    {
        do
        {
            *--p = HEX[(uint32_t)(ui64 & 0xf)];
        } while (ui64 >>= 4);
    }

    // 已经写入的长度，即数字的实际长度
    len = (temp + NGX_INT64_LEN) - p;

    // 数字长度不足指定值，且内存未溢出
    for (int i = len; i < width && buf < last; i++)
    {
        // 写入指定补齐字符，占位
        *buf++ = zero;
    }

    // 长度不足
    if ((buf + len) >= last)
    {
        // 长度减少至允许的最大部分
        len = last - buf; // 剩余的buf有多少我就拷贝多少
    }

    // 将 p 中长度为 len 的数据拷贝到 buf 中并返回最后的书写位置
    return ngx_cpymem(buf, p, len);
}

/***************************************************************
 *  @brief     根据输入字符串，对自定义数据结构进行格式化输出
 *  @param     buf      待写入内存
 *  @param     last     内存末尾标识，防止越界
 *  @param     fmt      第一个固定参数，多为字符串
 *  @param     args     可变参数集
 *  @return    写入字符后的结尾位置
 *  @note      %d【%Xd/%xd】:数字,    %s:字符串      %f：浮点,  %P：pid_t
 **************************************************************/
u_char *ngx_vslprintf(u_char *buf, u_char *last, const char *fmt, va_list args)
{
    // 比如说你要调用ngx_log_stderr(0, "invalid option: \"%s\"", argv[i]);，那么这里的fmt就应该是:   invalid option: "%s"
    // printf("fmt = %s\n",fmt);

    // 临时变量
    u_char zero;

    // 根据系统不同进行条件编译

    // #ifdef _WIN64
    //     typedef unsigned __int64 uintptr_t;
    // #else
    //     typedef unsigned int uintptr_t;
    // #endif

    // 临时变量
    uintptr_t width, sign, hex, frac_width, scale, n;

    int64_t i64;   // 保存%d对应的可变参
    uint64_t ui64; // 保存%ud对应的可变参，临时作为%f可变参的整数部分也是可以的
    u_char *p;     // 保存%s对应的可变参
    double f;      // 保存%f对应的可变参
    uint64_t frac; // %f可变参数,保存小数点数值

    // 依次处理字符串中的每个字符
    while (*fmt && buf < last)
    {
        if (*fmt == '%') //%开头的一般都是需要被可变参数 取代的
        {
            // 对各个变量进行初始化

            // 读取默认填充字符，若为'0'，则用'0'填充补齐，否则空格补齐
            zero = (u_char)((*++fmt == '0') ? '0' : ' ');

            width = 0;      // 指定宽度，仅对数字类型有效
            sign = 1;       // 有无符号数，默认有符号，'%u'表示无符号
            hex = 0;        // 是否为十六进制，0：不是，1：是、小写，2：是、大写，输出地址时可能会使用
            frac_width = 0; // 小数点后保留位数，%.10f
            i64 = 0;        // 可变参数中的实际数字，%d
            ui64 = 0;       // 可变参数中的实际数字，%ud

            // 变量初始化完成

            // 判断是否指定了输出宽度
            while (*fmt >= '0' && *fmt <= '9')
            {
                // 从大到小将字符串转为数字
                width = width * 10 + (*fmt++ - '0');
            }

            // 判断是否指定了特殊格式
            for (;;)
            {
                // 对字符进行分类讨论
                switch (*fmt)
                {
                case 'u':     // %u，u 表示无符号
                    sign = 0; // 标记为无符号数
                    fmt++;    // 处理下一个字符
                    continue; // 继续判断有无特殊格式字符

                case 'X':     // %X，X 表示大写十六进制
                    hex = 2;  // 标记以大写字母显示十六进制中的A-F
                    sign = 0; // 不区分有无符号，直接将符号位输出
                    fmt++;    // 处理下一个字符
                    continue; // 继续判断有无特殊格式字符

                case 'x':     // %x，x表示小写十六进制
                    hex = 1;  // 标记以小写字母显示十六进制中的a-f
                    sign = 0; // 不区分有无符号，直接将符号位输出
                    fmt++;    // 处理下一个字符
                    continue; // 继续判断有无特殊格式字符

                case '.':  // %.10f，表示指定小数点后保留位数
                    fmt++; // 处理下一个字符

                    // 取出指定的保留位数
                    while (*fmt >= '0' && *fmt <= '9')
                    {
                        frac_width = frac_width * 10 + (*fmt++ - '0');
                    }      // end while(*fmt >= '0' && *fmt <= '9')
                    break; // 不会再有其他特殊字符，直接退出

                default:
                    break;
                } // end switch (*fmt)
                break;
            } // end for ( ;; )

            // 判断标准输出格式选项
            switch (*fmt)
            {
            case '%':         // %%，表示输出一个%
                *buf++ = '%'; // 直接写入
                fmt++;        // 移动指针
                continue;     // 直接处理下一个字符

            case 'f':                     // 浮点型数据
                f = va_arg(args, double); // 获取可变参数集中的浮点型数值

                // 负数处理
                if (f < 0)
                {
                    *buf++ = '-'; // 单独写入负号
                    f = -f;       // 转为正数
                }

                // 保存整数部分
                ui64 = (int64_t)f; // 正整数部分给到ui64里
                frac = 0;

                // 如果有指定小数点后显示位数
                if (frac_width)
                {
                    // 计算放大规模
                    scale = 1;
                    for (n = frac_width; n; n--)
                    {
                        scale *= 10;
                    }

                    // 作差求得小数部分规模放大后四舍五入，转为整型
                    frac = (uint64_t)((f - (double)ui64) * scale + 0.5);

                    // 存在进位现象
                    if (frac == scale)
                    {
                        ui64++;   // 正整数部分进位
                        frac = 0; // 小数部分归零
                    }
                } // end if (frac_width)

                // 先写入整数部分
                buf = ngx_sprintf_num(buf, last, ui64, zero, 0, width);

                // 指定显示小数位数
                if (frac_width)
                {
                    // 内存充足
                    if (buf < last)
                    {
                        // 写入小数点
                        *buf++ = '.';
                    }
                    // 写入小数部分，宽度不足用'0'补齐
                    buf = ngx_sprintf_num(buf, last, frac, '0', 0, frac_width);
                }

                fmt++;    // 移动指针到下一位
                continue; // 直接判断下一个字符

            case 's':                       // 字符串型数据
                p = va_arg(args, u_char *); // va_arg():遍历可变参数，var_arg的第二个参数表示遍历的这个可变的参数的类型

                // 字符串未结束且内存充足
                while (*p && buf < last)
                {
                    // 直接写入
                    *buf++ = *p++;
                }

                fmt++;
                continue; // 重新从while开始执行

            case 'd':     // 整型数据，如果和u配合使用，也就是%ud,则是显示无符号整型数据
                if (sign) // 有符号数
                {
                    // va_arg():遍历可变参数，var_arg的第二个参数表示遍历的这个可变的参数的类型
                    i64 = (int64_t)va_arg(args, int);
                }
                else // 无符号数
                {
                    ui64 = (uint64_t)va_arg(args, u_int);
                }
                break; // 格式判断完毕，直接跳出，进行下一步（写入）

            case 'i': // ngx_int_t 型数据
                // 直接保存数据跳出
                if (sign)
                {
                    i64 = (int64_t)va_arg(args, intptr_t);
                }
                else
                {
                    ui64 = (uint64_t)va_arg(args, uintptr_t);
                }

                // if (max_width)
                //{
                //     width = NGX_INT_T_LEN;
                // }

                break;

            case 'L': // int64j 型数据
                // 直接保存数据跳出
                if (sign)
                {
                    i64 = va_arg(args, int64_t);
                }
                else
                {
                    ui64 = va_arg(args, uint64_t);
                }
                break;

            case 'P': // pid_t 类型
                i64 = (int64_t)va_arg(args, pid_t);
                sign = 1;
                break;

            case 'p':
                ui64 = (uintptr_t)va_arg(args, void *);
                hex = 2;    // 标记以大写字母显示十六进制中的A-F
                sign = 0;   // 标记这是个无符号数
                zero = '0'; // 前边0填充
                width = 2 * sizeof(void *);
                break;

            default:
                // 直接拷贝，处理下一个字符
                *buf++ = *fmt++;
                continue;
            } // end switch (*fmt)

            // 处理 %d，%i，%L，%P，%p

            // 统一将显示的数字都保存到 ui64 里去；
            if (sign)
            {
                if (i64 < 0)
                {
                    *buf++ = '-';
                    ui64 = (uint64_t)-i64;
                }
                else // 显示正数
                {
                    ui64 = (uint64_t)i64;
                }
            } // end if (sign)

            // 将数字写入指定内存，本次参数写入完成，可以继续处理下一个字符
            buf = ngx_sprintf_num(buf, last, ui64, zero, hex, width);
            // 移动指针，处理下一个字符
            fmt++;
        }
        // 普通字符直接写入指定内存
        else
        {
            // 逐个写入
            *buf++ = *fmt++;
        } // end if (*fmt == '%')
    }     // end while (*fmt && buf < last)

    return buf;
}
