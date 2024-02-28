
#ifndef __NGX_LOCKMUTEX_H__
#define __NGX_LOCKMUTEX_H__

#include <pthread.h>

// 本文件存放可实现自动上锁、解锁的类，防止在程序中因未解锁导致的意外发生

class CLock
{
public:
	// 构造函数，利用互斥量进行加锁
	CLock(pthread_mutex_t *pMutex)
	{
		m_pMutex = pMutex;
		pthread_mutex_lock(m_pMutex); // 加锁互斥量
	}

	// 析构函数，对互斥量进行解锁
	~CLock()
	{
		pthread_mutex_unlock(m_pMutex); // 解锁互斥量
	}

private:
	// 成员变量，一把锁
	pthread_mutex_t *m_pMutex;
};

#endif
