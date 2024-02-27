
// 本文件存放和内存相关的类的函数实现

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ngx_c_memory.h"

// 类静态成员赋值
CMemory *CMemory::m_instance = NULL;

/***************************************************************
 *  @brief     根据要求分配指定大小的内存
 *  @param     memCount    待分配的内存大小
 *  @param     ifmemset    是否清空分配出的内存
 *  @return    喷配得到的内内存指向的空类型指针
 **************************************************************/
void *CMemory::AllocMemory(int memCount, bool ifmemset)
{
    // 不判断是否成功，失败则直崩溃
    void *tmpData = (void *)new char[memCount];

    // 内存清空
    if (ifmemset)
    {
        memset(tmpData, 0, memCount);
    }

    return tmpData;
}

/***************************************************************
 *  @brief     释放指定的区域的内存
 *  @param     point    指向待释放区域内存的指针
 **************************************************************/
void CMemory::FreeMemory(void *point)
{
    // delete [] point;  // warning: deleting ‘void*’ is undefined [-Wdelete-incomplete]

    // new 的时候是char *，这里弄回char *，以免出警告
    delete[] ((char *)point);
}
