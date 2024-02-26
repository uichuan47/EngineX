
#ifndef __NGX_MEMORY_H__
#define __NGX_MEMORY_H__

#include <stddef.h> //NULL

// 本文件声明了一个内存相关的单例类

class CMemory
{
private:
	CMemory() {}

public:
	~CMemory(){};

private:
	// 在主线程中就完成初始化，无需考虑多线程安全性
	static CMemory *m_instance;

public:
	static CMemory *GetInstance() // 单例
	{
		if (m_instance == NULL)
		{
			// 锁
			if (m_instance == NULL)
			{
				// 第一次调用应在主进程中，以免和其他线程调用冲突从而导致同时执行两次 new CMemory()
				m_instance = new CMemory();
				static CGarhuishou cl;
			}
			// 放锁
		}

		return m_instance;
	}

	class CGarhuishou
	{
	public:
		~CGarhuishou()
		{
			// 系统退出时，系统调用释放内存
			if (CMemory::m_instance)
			{
				delete CMemory::m_instance;
				CMemory::m_instance = NULL;
			}
		}
	};

public:
	// 申请指定大小的内存
	void *AllocMemory(int memCount, bool ifmemset);
	// 释放申请得到的内存
	void FreeMemory(void *point);
};

#endif
