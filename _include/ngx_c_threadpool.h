// 本文件存放 线程池 相关的类声明

#ifndef __NGX_THREADPOOL_H__
#define __NGX_THREADPOOL_H__

#include <vector>
#include <pthread.h>
#include <atomic> //c++11里的原子操作

// 线程池类
class CThreadPool
{
public:
    // 构造函数
    CThreadPool();

    // 析构函数
    ~CThreadPool();

public:
    // 创建线程池中的线程
    bool Create(int threadNum);
    // 退出线程池中全部线程
    void StopAll();

    // 将收到的的完整消息（消息头 + 包头 + 包体）放入消息队列，并触发线程处理
    void inMsgRecvQueueAndSignal(char *buf);
    // 呼唤线程处理消息
    void Call();
    // 获取接收消息队列大小
    int getRecvMsgQueueCount() { return m_iRecvMsgQueueCount; }

private:
    // 新线程的线程回调函数
    static void *ThreadFunc(void *threadData);
    // 清理接收消息队列
    void clearMsgRecvQueue();

    // 将一个消息出消息队列	，不需要，直接在ThreadFunc()中处理
    // char *outMsgRecvQueue();

private:
    // 类内定义结构体，用于封装产生的线程，便于统一存储线程信息和管理
    struct ThreadItem
    {
        pthread_t _Handle;   // 线程句柄
        CThreadPool *_pThis; // 记录线程池的指针
        bool ifrunning;      // 标记是否正式启动起来，启动起来后，才允许调用StopAll()来释放

        // 构造函数
        ThreadItem(CThreadPool *pthis) : _pThis(pthis), ifrunning(false) {}
        // 析构函数
        ~ThreadItem() {}
    };

private:
    // 线程同步互斥量/也叫线程同步锁
    static pthread_mutex_t m_pthreadMutex;
    // 线程同步条件变量
    static pthread_cond_t m_pthreadCond;
    // 线程池退出标志，false不退出，true退出
    static bool m_shutdown;

    // 待创建的线程数量
    int m_iThreadNum;
    // 线程数, 运行中的线程数，原子操作
    std::atomic<int> m_iRunningThreadNum;
    // 上次发生线程不够用【紧急事件】的时间,防止日志报的太频繁
    time_t m_iLastEmgTime;

    // int                        m_iRunningThreadNum; //线程数, 运行中的线程数
    // time_t                     m_iPrintInfoTime;    //打印信息的一个间隔时间，我准备10秒打印出一些信息供参考和调试
    // time_t                     m_iCurrTime;         //当前时间

    // 线程容器，容器内存放全部创建出的封装后的线程指针
    std::vector<ThreadItem *> m_threadVector;

    // 接收消息队列相关

    // 接收数据消息队列
    std::list<char *> m_MsgRecvQueue;
    // 收消息队列大小
    int m_iRecvMsgQueueCount;
};

#endif
