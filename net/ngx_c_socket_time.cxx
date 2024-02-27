
// 本文件存放和网络中时间相关的函数实现

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>	   //uintptr_t
#include <stdarg.h>	   //va_start....
#include <unistd.h>	   //STDERR_FILENO等
#include <sys/time.h>  //gettimeofday
#include <time.h>	   //localtime_r
#include <fcntl.h>	   //open
#include <errno.h>	   //errno
#include <sys/ioctl.h> //ioctl
// #include <sys/socket.h>
#include <arpa/inet.h>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"

// 设置踢出时钟(向multimap表中增加内容)，用户三次握手成功连入，然后我们开启了踢人开关【Sock_WaitTimeEnable = 1】，那么本函数被调用；

/***************************************************************
 *  @brief     设置踢人时钟，将指定链接加入到时间队列中
 *  @param     pConn    TCP 连接
 *  @note      踢人开关开启时，用户三次握手接入服务器后，本函数将会被调用
 **************************************************************/
void CSocekt::AddToTimerQueue(lpngx_connection_t pConn)
{
	CMemory *p_memory = CMemory::GetInstance();

	time_t futtime = time(NULL);
	// 检查时间
	futtime += m_iWaitTime;

	// 互斥，访问时间队列
	CLock lock(&m_timequeueMutex);

	// 初始化消息头
	LPSTRUC_MSG_HEADER tmpMsgHeader = (LPSTRUC_MSG_HEADER)p_memory->AllocMemory(m_iLenMsgHeader, false);
	// 将连接赋值到消息头
	tmpMsgHeader->pConn = pConn;
	// 消息头序号
	tmpMsgHeader->iCurrsequence = pConn->iCurrsequence;
	// 将时间和消息头放入队列
	m_timerQueuemap.insert(std::make_pair(futtime, tmpMsgHeader));
	// 时间队列大小增加
	m_cur_size_++;

	// 将计时队列头部时间值保存到m_timer_value_里
	m_timer_value_ = GetEarliestTime();

	return;
}

/***************************************************************
 *  @brief     取出时间队列中最早的时间
 *  @return    时间
 *  @note      因为时间队列按序放入，因此第一个元素的时间一定是最早的，本函数不互斥，由调用者负责
 **************************************************************/
time_t CSocekt::GetEarliestTime()
{
	std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos;
	pos = m_timerQueuemap.begin();
	return pos->first;
}

// 从m_timeQueuemap移除最早的时间，并把最早这个时间所在的项的值所对应的指针 返回，调用者负责互斥，所以本函数不用互斥，

/***************************************************************
 *  @brief     移除时间队列中最早的时间，即队首元素，并将对应的消息头返回
 *  @return    最早的消息头
 *  @note      由上层函数负责互斥
 **************************************************************/
LPSTRUC_MSG_HEADER CSocekt::RemoveFirstTimer()
{
	std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos;

	// 消息头指针
	LPSTRUC_MSG_HEADER p_tmp;

	// 时间队列大小
	if (m_cur_size_ <= 0)
	{
		return NULL;
	}

	// 取出队首元素
	pos = m_timerQueuemap.begin();
	// 取出消息头
	p_tmp = pos->second;
	// 删除元素
	m_timerQueuemap.erase(pos);
	// 队列大小减少
	--m_cur_size_;

	return p_tmp;
}

/***************************************************************
 *  @brief     查询超出给定时间的元素，从队列中移除并返回
 *  @param     cur_time    给定时间
 *  @return    超时的消息头
 *  @note      调用者负责互斥，所以本函数不用互
 **************************************************************/
LPSTRUC_MSG_HEADER CSocekt::GetOverTimeTimer(time_t cur_time)
{
	// 内存对象
	CMemory *p_memory = CMemory::GetInstance();
	// 临时变量，保存取出的元素
	LPSTRUC_MSG_HEADER ptmp;

	// 队列为空，直接返回，两个条件应当同时成立或不成立
	if (m_cur_size_ == 0 || m_timerQueuemap.empty())
		return NULL;

	// 查询最早时间
	time_t earliesttime = GetEarliestTime();

	// 最早的时间早于指定时间
	if (earliesttime <= cur_time)
	{
		// 存在超时节点，移除并返回
		ptmp = RemoveFirstTimer();

		// 超时是否踢出
		if (/*m_ifkickTimeCount == 1 && */ m_ifTimeOutKick != 1)
		{
			// 如果不是要求超时就提出，则才做这里的事：

			// 因为下次超时的时间我们也依然要判断，所以还要把这个节点加回来
			time_t newinqueutime = cur_time + (m_iWaitTime);
			LPSTRUC_MSG_HEADER tmpMsgHeader = (LPSTRUC_MSG_HEADER)p_memory->AllocMemory(sizeof(STRUC_MSG_HEADER), false);
			tmpMsgHeader->pConn = ptmp->pConn;
			tmpMsgHeader->iCurrsequence = ptmp->iCurrsequence;
			m_timerQueuemap.insert(std::make_pair(newinqueutime, tmpMsgHeader)); // 自动排序 小->大
			m_cur_size_++;
		}

		// 时间队列还存在元素
		if (m_cur_size_ > 0)
		{
			// 更新队列最早时间
			m_timer_value_ = GetEarliestTime();
		}

		return ptmp;
	}

	return NULL;
}

/***************************************************************
 *  @brief     将指定的 TCP 连接从时间队列中移除
 *  @param     pConn    指定链接
 **************************************************************/
void CSocekt::DeleteFromTimerQueue(lpngx_connection_t pConn)
{
	std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos, posend;
	CMemory *p_memory = CMemory::GetInstance();

	// 上锁，访问时间队列
	CLock lock(&m_timequeueMutex);

	// 因为实际情况可能比较复杂，将来可能还扩充代码等等，所以如下我们遍历整个队列找 一圈，而不是找到一次就拉倒，以免出现什么遗漏

lblMTQM:
	pos = m_timerQueuemap.begin();
	posend = m_timerQueuemap.end();

	// 遍历查找指定连接
	for (; pos != posend; ++pos)
	{
		// 连接匹配
		if (pos->second->pConn == pConn)
		{
			// 释放内存
			p_memory->FreeMemory(pos->second); // 释放内存
			// 删除元素
			m_timerQueuemap.erase(pos);
			// 队列元素减少
			--m_cur_size_;

			goto lblMTQM;
		}
	}

	// 时间队列仍存在元素，更新时间
	if (m_cur_size_ > 0)
	{
		m_timer_value_ = GetEarliestTime();
	}

	return;
}

/***************************************************************
 *  @brief     清空时间队列中的全部链接
 **************************************************************/
void CSocekt::clearAllFromTimerQueue()
{
	std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos, posend;

	CMemory *p_memory = CMemory::GetInstance();
	pos = m_timerQueuemap.begin();
	posend = m_timerQueuemap.end();

	for (; pos != posend; ++pos)
	{
		// 释放内存
		p_memory->FreeMemory(pos->second);
		--m_cur_size_;
	}

	// 清空队列
	m_timerQueuemap.clear();
}

/***************************************************************
 *  @brief     检测心跳包是否超时
 *  @param     tmpmsg    消息头
 *  @param     cur_time    当前时间
 **************************************************************/
void CSocekt::procPingTimeOutChecking(LPSTRUC_MSG_HEADER tmpmsg, time_t cur_time)
{
	CMemory *p_memory = CMemory::GetInstance();
	p_memory->FreeMemory(tmpmsg);
}

/***************************************************************
 *  @brief     监控时间队列线程的入口函数，处理到期不发心跳包的连接
 *  @param     threadData    线程对象
 **************************************************************/
void *CSocekt::ServerTimerQueueMonitorThread(void *threadData)
{
	// 线程对象
	ThreadItem *pThread = static_cast<ThreadItem *>(threadData);
	// 取出线程池指针
	CSocekt *pSocketObj = pThread->_pThis;

	// 临时变量
	time_t absolute_time, cur_time;
	int err;

	// 不退出程序
	while (g_stopEvent == 0)
	{
		// 暂时无需互斥判断，先判断队列中是否存在元素

		// 队列存在元素
		if (pSocketObj->m_cur_size_ > 0)
		{
			// 记录最先发生的事件事件
			absolute_time = pSocketObj->m_timer_value_;

			// 取得当前时间
			cur_time = time(NULL);

			// 最先发生事件的时间小于当前时间
			if (absolute_time < cur_time)
			{
				// 时间到了，可以处理

				// 消息头队列，存放待处理元素
				std::list<LPSTRUC_MSG_HEADER> m_lsIdleList;
				// 消息头结构体
				LPSTRUC_MSG_HEADER result;

				// 先上锁，访问时间队列
				err = pthread_mutex_lock(&pSocketObj->m_timequeueMutex);
				if (err != 0)
					ngx_log_stderr(err, "CSocekt::ServerTimerQueueMonitorThread()中pthread_mutex_lock()失败，返回的错误码为%d!", err);

				// 将队列中发生在当前时间之前的元素全部取出，准备后续检查
				while ((result = pSocketObj->GetOverTimeTimer(cur_time)) != NULL)
				{
					m_lsIdleList.push_back(result);
				}

				// 解锁
				err = pthread_mutex_unlock(&pSocketObj->m_timequeueMutex);
				if (err != 0)
					ngx_log_stderr(err, "CSocekt::ServerTimerQueueMonitorThread()pthread_mutex_unlock()失败，返回的错误码为%d!", err);

				// 消息头结构体
				LPSTRUC_MSG_HEADER tmpmsg;
				// 队列非空
				while (!m_lsIdleList.empty())
				{
					// 取出并删除队首元素
					tmpmsg = m_lsIdleList.front();
					m_lsIdleList.pop_front();

					// 检查是否超时，超时则踢出
					pSocketObj->procPingTimeOutChecking(tmpmsg, cur_time);
				}
			}

		} // end if(pSocketObj->m_cur_size_ > 0)

		// 每间隔 500 毫秒循环一次，检测队列
		usleep(500 * 1000);
	} // end while

	return (void *)0;
}
