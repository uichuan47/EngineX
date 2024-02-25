// CConfig 类的函数实现

// 系统头文件
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

// 自定义头文件

// 相关函数声明
#include "ngx_func.h"
// 读取配置类的头文件
#include "ngx_c_conf.h"

// 静态成员变量赋值，初始为空
CConfig *CConfig::m_instance = NULL;

// 构造函数
CConfig::CConfig() {}

// 析构函数
CConfig::~CConfig()
{
    // 依次释放数组中指针指向的堆区内存
    for (auto pos = m_ConfigItemList.begin(); pos != m_ConfigItemList.end(); ++pos)
    {
        delete (*pos);
    } // end for

    // 清空数组
    m_ConfigItemList.clear();

    return;
}

/***************************************************************
 *  @brief     加载配置文件
 *  @param     pconfName    配置文件的文件描述符
 *  @return    加载成功为 1，失败为 0
 **************************************************************/
bool CConfig::Load(const char *pconfName)
{
    // 初始化文件指针
    FILE *fp;
    // 打开指向的配置文件
    fp = fopen(pconfName, "r");
    // 读取失败则返回 0
    if (fp == NULL)
        return false;

    // 存储读取出的配置信息，限制每行长度
    char linebuf[501];

    // 文件打开成功，开始处理

    // 循环读取每一行文件
    while (!feof(fp))
    {
        // 从文件中读取一行内容到字符串中，限制长度，出错或未读取到则返回空，直接读取下一行
        if (fgets(linebuf, 500, fp) == NULL)
            continue;

        // 内容为空，直接读取下一行
        if (linebuf[0] == 0)
            continue;

        // 若本行为注释，直接读取下一行
        if (*linebuf == ';' || *linebuf == ' ' || *linebuf == '[' || *linebuf == '#' || *linebuf == '\t' || *linebuf == '\n')
            continue;

    // 开始处理读取到的每一行配置信息

    // 处理行尾的换行、回车、空格等无效字符
    lblprocstring:
        if (strlen(linebuf) > 0)
        {
            // 若为换行、回车、空格，则去除尾部，再次检查
            if (linebuf[strlen(linebuf) - 1] == 10 || linebuf[strlen(linebuf) - 1] == 13 || linebuf[strlen(linebuf) - 1] == 32)
            {
                linebuf[strlen(linebuf) - 1] = 0;
                goto lblprocstring;
            }
        }

        // 字符为空，直接读取下一行
        if (linebuf[0] == 0)
            continue;

        // 开始处理正确的配置信息

        // 查找 '=' 所在位置
        char *ptmp = strchr(linebuf, '=');
        // 查找到 '=' 所在位置，若不为空
        if (ptmp != NULL)
        {
            // 创建临时指针保存信息
            LPCConfItem p_confitem = new CConfItem;
            // 清空指针内存
            memset(p_confitem, 0, sizeof(CConfItem));
            // 等号左侧拷贝到项目名称
            strncpy(p_confitem->ItemName, linebuf, (int)(ptmp - linebuf));
            // 等号右侧拷贝到项目内容
            strcpy(p_confitem->ItemContent, ptmp + 1);

            // 去除读取配置信息时可能多余读取的左侧或右侧空格
            Rtrim(p_confitem->ItemName);
            Ltrim(p_confitem->ItemName);
            Rtrim(p_confitem->ItemContent);
            Ltrim(p_confitem->ItemContent);

            // printf("itemname=%s | itemcontent=%s\n",p_confitem->ItemName,p_confitem->ItemContent);
            // 将读取成功后的配置信息保存到类内成员数组当中，需要注意，内存最后需要释放
            m_ConfigItemList.push_back(p_confitem);
        } // end if
    }     // end while(!feof(fp))

    // 关闭打开的文件指针
    fclose(fp);

    return true;
}

/***************************************************************
 *  @brief     读取字符串类型的配置项目
 *  @param     p_itemname    待查找的配置项目名称
 *  @return    查找结果，查到返回字符串，否则返回空
 **************************************************************/
const char *CConfig::GetString(const char *p_itemname)
{
    for (auto pos = m_ConfigItemList.begin(); pos != m_ConfigItemList.end(); ++pos)
    {
        if (strcasecmp((*pos)->ItemName, p_itemname) == 0)
            return (*pos)->ItemContent;
    } // end for

    return NULL;
}

/***************************************************************
 *  @brief     查找数值类型的配置项目
 *  @param     p_itemname     待查找的配置项目名称
 *  @param     def     默认值
 *  @return    返回值
 *  @note      查找结果，查到返回字符串，否则返回给定的默认值
 **************************************************************/
int CConfig::GetIntDefault(const char *p_itemname, const int def)
{
    for (auto pos = m_ConfigItemList.begin(); pos != m_ConfigItemList.end(); ++pos)
    {
        if (strcasecmp((*pos)->ItemName, p_itemname) == 0)
            return atoi((*pos)->ItemContent);
    } // end for

    return def;
}
