
// 本文件存放 线程池 相关的类函数实现

#include <stdarg.h>
#include <unistd.h> //usleep

#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_memory.h"
#include "ngx_macro.h"
#include "ngx_c_threadpool.h"

// 静态成员初始化
pthread_mutex_t CThreadPool::m_pthreadMutex = PTHREAD_MUTEX_INITIALIZER; // #define PTHREAD_MUTEX_INITIALIZER ((pthread_mutex_t) -1)
pthread_cond_t CThreadPool::m_pthreadCond = PTHREAD_COND_INITIALIZER;    // #define PTHREAD_COND_INITIALIZER ((pthread_cond_t) -1)
bool CThreadPool::m_shutdown = false;                                    // 刚开始标记整个线程池的线程是不退出的

/***************************************************************
 *  @brief     线程池构造函数
 *  @note      没有直接创建出全部线程，需要额外调用函数创建，仅初始化部分变量
 **************************************************************/
CThreadPool::CThreadPool()
{
    // 初始状态下正在运行的线程数为 0
    m_iRunningThreadNum = 0;
    // 上次线程不足报告时间，初始为 0
    m_iLastEmgTime = 0;
    // 收消息队列大小初始为 0
    m_iRecvMsgQueueCount = 0;

    // m_iPrintInfoTime = 0;    //上次打印参考信息的时间；
}

/***************************************************************
 *  @brief     析构函数
 *  @note      并未释放线程池中线程内存，需要额外释放
 **************************************************************/
CThreadPool::~CThreadPool()
{
    // 资源释放在StopAll()里统一进行，就不在这里进行了
    // 停止线程的工作由另一函数处理

    // 清空消息队列中的消息
    clearMsgRecvQueue();
}

/***************************************************************
 *  @brief     清空接收消息队列内存
 **************************************************************/
void CThreadPool::clearMsgRecvQueue()
{
    // 临时变量，保存消息队列中的消息
    char *sTmpMempoint;
    // 内存对象
    CMemory *p_memory = CMemory::GetInstance();

    // 准备清空，线程应当已经全部停止，因此无需互斥

    // 队列未清空
    while (!m_MsgRecvQueue.empty())
    {
        // 取出队头消息
        sTmpMempoint = m_MsgRecvQueue.front();
        // 弹出
        m_MsgRecvQueue.pop_front();
        // 释放消息占用内存
        p_memory->FreeMemory(sTmpMempoint);
    }
}

/***************************************************************
 *  @brief     在线程池中创建指定数量的线程
 *  @param     threadNum    待创建的线程数量
 *  @return    true: 创建成功，false: 创建失败、出错
 *  @note      不在构造函数中调用，需要手动调用，更加灵活
 **************************************************************/
bool CThreadPool::Create(int threadNum)
{
    // 线程对象指针
    ThreadItem *pNew;
    // 错误码
    int err;

    // 保存要创建的线程数量
    m_iThreadNum = threadNum;

    // 循环，依次创建指定数量的变量
    for (int i = 0; i < m_iThreadNum; ++i)
    {
        // 创建一个线程对象指针，保存到容器中
        m_threadVector.push_back(pNew = new ThreadItem(this));

        // 调用系统函数，创建线程
        err = pthread_create(&pNew->_Handle, NULL, ThreadFunc, pNew);

        // 出现错误，创建失败
        if (err != 0)
        {
            // 创建线程有错
            ngx_log_stderr(err, "CThreadPool::Create()创建线程%d失败, 返回的错误码为%d!", i, err);
            return false;
        }
        else
        {
            // 创建线程成功
            // ngx_log_stderr(0,"CThreadPool::Create()创建线程%d成功,线程id=%d",pNew->_Handle);
        }

    } // end for

    // 我们必须保证每个线程都启动并运行到pthread_cond_wait()，本函数才返回，只有这样，这几个线程才能进行后续的正常工作
    std::vector<ThreadItem *>::iterator iter;

lblfor:

    for (iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
    {
        // 保证每个创建出的线程都正常启动
        if ((*iter)->ifrunning == false)
        {
            // 这说明有没有启动完全的线程
            usleep(100 * 1000); // 单位是微妙,又因为1毫秒=1000微妙，所以 100 *1000 = 100毫秒

            goto lblfor;
        }
    }

    return true;
}

/***************************************************************
 *  @brief     将受到的完整消息（消息头 + 包头 + 包体）放入消息队列，并触发线程池中的一个线程进行处理
 *  @param     buf    完整消息保存地址
 **************************************************************/
void CThreadPool::inMsgRecvQueueAndSignal(char *buf)
{
    // 互斥
    int err = pthread_mutex_lock(&m_pthreadMutex);

    // 互斥失败
    if (err != 0)
    {
        ngx_log_stderr(err, "CThreadPool::inMsgRecvQueueAndSignal()pthread_mutex_lock()失败，返回的错误码为%d!", err);
    }

    // 将消息加入消息队列
    m_MsgRecvQueue.push_back(buf);
    // 消息队列中消息数增加
    m_iRecvMsgQueueCount++;

    // 取消互斥
    err = pthread_mutex_unlock(&m_pthreadMutex);

    // 取消互斥失败
    if (err != 0)
    {
        ngx_log_stderr(err, "CThreadPool::inMsgRecvQueueAndSignal()pthread_mutex_unlock()失败，返回的错误码为%d!", err);
    }

    // 触发一个线程，处理刚加入消息队列的消息
    Call();

    return;
}

/***************************************************************
 *  @brief     调用一个线程，处理消息队列中的消息
 *  @note      往往发生在将一个消息放入接收消息队列之后
 **************************************************************/
void CThreadPool::Call()
{
    // ngx_log_stderr(0,"m_pthreadCondbegin--------------=%ui!",m_pthreadCond);  //数字5，此数字不靠谱
    // for(int i = 0; i <= 100; i++)
    //{

    // 唤醒一个等待该条件的线程，也就是可以唤醒卡在pthread_cond_wait()的线程
    int err = pthread_cond_signal(&m_pthreadCond);
    if (err != 0)
    {
        // 这是有问题啊，要打印日志啊
        ngx_log_stderr(err, "CThreadPool::Call()中pthread_cond_signal()失败，返回的错误码为%d!", err);
    }

    //}
    // 唤醒完100次，试试打印下m_pthreadCond值;
    // ngx_log_stderr(0,"m_pthreadCondend--------------=%ui!",m_pthreadCond);  //数字1

    //(1)如果当前的工作线程全部都忙，则要报警
    // bool ifallthreadbusy = false;

    // 线程池中线程数量与正在进行的线程数相等，即全部线程都在处理业务
    if (m_iThreadNum == m_iRunningThreadNum)
    {
        // 线程不够用了
        // ifallthreadbusy = true;
        // 记录当前时间
        time_t currtime = time(NULL);

        // 距离上次线程跑满过去 10s 后
        if (currtime - m_iLastEmgTime > 10)
        {
            // 两次报告之间的间隔必须超过10秒，不然如果一直出现当前工作线程全忙，但频繁报告日志也够烦的
            // 更新上次跑满时间
            m_iLastEmgTime = currtime; // 更新时间
            // 写日志，通知这种紧急情况给用户，用户要考虑增加线程池中线程数量了
            ngx_log_stderr(0, "CThreadPool::Call()中发现线程池中当前空闲线程数量为0, 要考虑扩容线程池了!");
        }
    } // end if

    /*
        //-------------------------------------------------------如下内容都是一些测试代码；
        //唤醒丢失？--------------------------------------------------------------------------
        //(2)整个工程中，只在一个线程（主线程）中调用了Call，所以不存在多个线程调用Call的情形。
        if(ifallthreadbusy == false)
        {
            //有空闲线程  ，有没有可能我这里调用   pthread_cond_signal()，但因为某个时刻线程曾经全忙过，导致本次调用 pthread_cond_signal()并没有激发某个线程的pthread_cond_wait()执行呢？
               //我认为这种可能性不排除，这叫 唤醒丢失。如果真出现这种问题，我们如何弥补？
            if(irmqc > 5) //我随便来个数字比如给个5吧
            {
                //如果有空闲线程，并且 接收消息队列中超过5条信息没有被处理，则我总感觉可能真的是 唤醒丢失
                //唤醒如果真丢失，我是否考虑这里多唤醒一次？以尝试逐渐补偿回丢失的唤醒？此法是否可行，我尚不可知，我打印一条日志【其实后来仔细相同：唤醒如果真丢失，也无所谓，因为ThreadFunc()会一直处理直到整个消息队列为空】
                ngx_log_stderr(0,"CThreadPool::Call()中感觉有唤醒丢失发生，irmqc = %d!",irmqc);

                int err = pthread_cond_signal(&m_pthreadCond); //唤醒一个等待该条件的线程，也就是可以唤醒卡在pthread_cond_wait()的线程
                if(err != 0 )
                {
                    //这是有问题啊，要打印日志啊
                    ngx_log_stderr(err,"CThreadPool::Call()中pthread_cond_signal 2()失败，返回的错误码为%d!",err);
                }
            }
        }  //end if

        //(3)准备打印一些参考信息【10秒打印一次】,当然是有触发本函数的情况下才行
        m_iCurrTime = time(NULL);
        if(m_iCurrTime - m_iPrintInfoTime > 10)
        {
            m_iPrintInfoTime = m_iCurrTime;
            int irunn = m_iRunningThreadNum;
            ngx_log_stderr(0,"信息：当前消息队列中的消息数为%d,整个线程池中线程数量为%d,正在运行的线程数量为 = %d!",irmqc,m_iThreadNum,irunn); //正常消息，三个数字为 1，X，0
        }
        */

    return;
}

/***************************************************************
 *  @brief     所有线程的入口函数，全部线程创建后都立即执行本函数
 *  @param     threadData    创建、运行本线程函数的线程对象
 **************************************************************/
void *CThreadPool::ThreadFunc(void *threadData)
{
    // 这个是静态成员函数，是不存在this指针的

    // 临时变量，保存线程对象
    ThreadItem *pThread = static_cast<ThreadItem *>(threadData);
    // 保存线程对象所属的线程池
    CThreadPool *pThreadPoolObj = pThread->_pThis;

    // 内存分配对象
    CMemory *p_memory = CMemory::GetInstance();
    int err;

    // 获取线程 id
    pthread_t tid = pthread_self(); // 获取线程自身id，以方便调试打印信息等

    // 处理消息的进程，进入无限循环
    while (true)
    {
        // 线程用pthread_mutex_lock()函数去锁定指定的mutex变量，若该mutex已经被另外一个线程锁定了，该调用将会阻塞线程直到mutex被解锁

        // 访问共享变量前，必须上锁
        err = pthread_mutex_lock(&m_pthreadMutex);
        if (err != 0)
            ngx_log_stderr(err, "CThreadPool::ThreadFunc()中pthread_mutex_lock()失败，返回的错误码为%d!", err); // 有问题，要及时报告

        // 以下这行程序写法技巧十分重要，必须要用while这种写法，
        // 因为：pthread_cond_wait()是个值得注意的函数，调用一次pthread_cond_signal()可能会唤醒多个【惊群】
        // 【官方描述是 至少一个/pthread_cond_signal 在多处理器上可能同时唤醒多个线程】
        // 老师也在《c++入门到精通 c++ 98/11/14/17》里第六章第十三节谈过虚假唤醒，实际上是一个意思；
        // 老师也在《c++入门到精通 c++ 98/11/14/17》里第六章第八节谈过条件变量、wait()、notify_one()、notify_all()
        // 其实跟这里的pthread_cond_wait、pthread_cond_signal、pthread_cond_broadcast非常类似
        // pthread_cond_wait() 函数，如果只有一条消息 唤醒了两个线程干活，
        // 那么其中有一个线程拿不到消息，那如果不用while写，就会出问题，所以被惊醒后必须再次用while拿消息，拿到才走下来；
        // while( (jobbuf = g_socket.outMsgRecvQueue()) == NULL && m_shutdown == false)

        // 通知函数可能会意外唤醒多个线程从队列中取数据，因此即便被唤醒，也需要判断队列内是否存在数据
        //
        while ((pThreadPoolObj->m_MsgRecvQueue.size() == 0) && m_shutdown == false)
        {
            // 如果这个pthread_cond_wait被唤醒【被唤醒后程序执行流程往下走的前提是拿到了锁--官方：pthread_cond_wait()返回时，互斥量再次被锁住】，
            // 那么会立即再次执行g_socket.outMsgRecvQueue()，如果拿到了一个NULL，则继续在这里wait着();

            // 线程未启动，则启动线程
            if (pThread->ifrunning == false)
                pThread->ifrunning = true;
            // 标记为true了才允许调用StopAll()：测试中发现如果Create()和StopAll()紧挨着调用，就会导致线程混乱，
            // 所以每个线程必须执行到这里，才认为是启动成功了；

            // ngx_log_stderr(0,"执行了pthread_cond_wait-------------begin");
            // 刚开始执行pthread_cond_wait()的时候，会卡在这里，而且m_pthreadMutex会被释放掉；

            // 系统启动，线程被创建时，都会卡在此处，等待消息队列放入一个消息时被唤醒，即便被唤醒任然需要判断条件是否成立，不成立则继续等待
            pthread_cond_wait(&m_pthreadCond, &m_pthreadMutex);

            // ngx_log_stderr(0,"执行了pthread_cond_wait-------------end");
        }

        // 能走下来的，必然是 拿到了真正的 消息队列中的数据   或者 m_shutdown == true

        /*
        jobbuf = g_socket.outMsgRecvQueue(); //从消息队列中取消息
        if( jobbuf == NULL && m_shutdown == false)
        {
            //消息队列为空，并且不要求退出，则
            //pthread_cond_wait()阻塞调用线程直到指定的条件有信号（signaled）。
                //该函数应该在互斥量锁定时调用，当在等待时会自动解锁互斥量【这是重点】。在信号被发送，线程被激活后，互斥量会自动被锁定，当线程结束时，由程序员负责解锁互斥量。
                  //说白了，某个地方调用了pthread_cond_signal(&m_pthreadCond);，这个pthread_cond_wait就会走下来；

            ngx_log_stderr(0,"--------------即将调用pthread_cond_wait,tid=%d--------------",tid);


            if(pThread->ifrunning == false)
                pThread->ifrunning = true; //标记为true了才允许调用StopAll()：测试中发现如果Create()和StopAll()紧挨着调用，就会导致线程混乱，所以每个线程必须执行到这里，才认为是启动成功了；

            err = pthread_cond_wait(&m_pthreadCond, &m_pthreadMutex);
            if(err != 0) ngx_log_stderr(err,"CThreadPool::ThreadFunc()pthread_cond_wait()失败，返回的错误码为%d!",err);//有问题，要及时报告



            ngx_log_stderr(0,"--------------调用pthread_cond_wait完毕,tid=%d--------------",tid);
        }
        */
        // if(!m_shutdown)  //如果这个条件成立，表示肯定是拿到了真正消息队列中的数据，要去干活了，干活，则表示正在运行的线程数量要增加1；
        //     ++m_iRunningThreadNum; //因为这里是互斥的，所以这个+是OK的；

        // 走到这里时刻，互斥量肯定是锁着的。。。。。。

        // 判断线程是否退出
        if (m_shutdown)
        {
            // 退出则释放互斥量，直接跳出循环，整个函数返回
            pthread_mutex_unlock(&m_pthreadMutex); // 解锁互斥量
            break;
        }

        // 走到这里，可以取得消息进行处理了【消息队列中必然有消息】,注意，目前还是互斥着呢

        // 消息队列中存在消息

        // 取出第一个消息
        char *jobbuf = pThreadPoolObj->m_MsgRecvQueue.front();
        // 队列可删除消息
        pThreadPoolObj->m_MsgRecvQueue.pop_front();
        // 消息队列中的消息数减少
        --pThreadPoolObj->m_iRecvMsgQueueCount;

        // 已经取出消息，可释放互斥量
        err = pthread_mutex_unlock(&m_pthreadMutex);
        if (err != 0)
            ngx_log_stderr(err, "CThreadPool::ThreadFunc()中pthread_mutex_unlock()失败，返回的错误码为%d!", err);

        // 正确取得消息，可以开始处理

        // 线程池中运行的线程数增加
        ++pThreadPoolObj->m_iRunningThreadNum;

        // 处理消息
        g_socket.threadRecvProcFunc(jobbuf);

        // ngx_log_stderr(0,"执行开始---begin,tid=%ui!",tid);
        // sleep(5); //临时测试代码
        // ngx_log_stderr(0,"执行结束---end,tid=%ui!",tid);

        // 处理结束，释放消息内存
        p_memory->FreeMemory(jobbuf);
        // 线程本次处理完毕，恢复为空闲线程
        --pThreadPoolObj->m_iRunningThreadNum;

    } // end while(true)

    // 能走出来表示整个程序要结束啊，怎么判断所有线程都结束？
    return (void *)0;
}

// 停止所有线程【等待结束线程池中所有线程，该函数返回后，应该是所有线程池中线程都结束了】
void CThreadPool::StopAll()
{
    //(1)已经调用过，就不要重复调用了
    if (m_shutdown == true)
    {
        return;
    }
    m_shutdown = true;

    //(2)唤醒等待该条件【卡在pthread_cond_wait()的】的所有线程，一定要在改变条件状态以后再给线程发信号
    int err = pthread_cond_broadcast(&m_pthreadCond);
    if (err != 0)
    {
        // 这肯定是有问题，要打印紧急日志
        ngx_log_stderr(err, "CThreadPool::StopAll()中pthread_cond_broadcast()失败，返回的错误码为%d!", err);
        return;
    }

    //(3)等等线程，让线程真返回
    std::vector<ThreadItem *>::iterator iter;
    for (iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
    {
        pthread_join((*iter)->_Handle, NULL); // 等待一个线程终止
    }

    // 流程走到这里，那么所有的线程池中的线程肯定都返回了；
    pthread_mutex_destroy(&m_pthreadMutex);
    pthread_cond_destroy(&m_pthreadCond);

    //(4)释放一下new出来的ThreadItem【线程池中的线程】
    for (iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
    {
        if (*iter)
            delete *iter;
    }
    m_threadVector.clear();

    ngx_log_stderr(0, "CThreadPool::StopAll()成功返回，线程池中线程全部正常结束!");
    return;
}
