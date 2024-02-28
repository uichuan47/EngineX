/***********************************
 * @author uichuan47
 * @since  2024.1.1
 * @brief  数据池 类声明
 * @date   2024.1.2
 ***********************************/

#ifndef __DOTA_POOL__
#define __DOTA_POOL__

// 头文件
#include <iostream>
#include <thread>
#include <mutex>
#include <deque>
#include <unistd.h>
using namespace std;

// 宏定义
#define Dota_Nul -1
#define Dota_Mat 0
#define Dota_Dat 1

// 类声明
class Mat;
class Dat;
class Dota;
class Dota_Pool;

// 类定义
class Mat
{
    // 友元声明
    friend Dota;
    friend Dota_Pool;

public:
    Mat(double _time = 10.0) : time_stamp(_time);

    Mat(const Mat &m);

    void show() const;

private:
    // u_char text[256];  // 文本信息
    double time_stamp; // 本条数据时间戳
};

class Dat
{
    friend Dota;
    friend Dota_Pool;

public:
    Dat(double _steer = 5.0, double _speed = 20.0, double _time = 10.0) : steer(_steer), speed(_speed), time_stamp(_time);

    Dat(const Dat &d);

    void show() const;

private:
    double steer;      // 引导方向
    double speed;      // 行驶速度
    double time_stamp; // 本条数据时间戳
};

class Dota
{
    friend Dota_Pool;

public:
    // 根据标识，初始化指定数据指针
    Dota(int _sign = Dota_Nul) : sign(_sign);

    Dota(const Mat &m);

    Dota(const Dat &d);

    // 拷贝构造
    Dota(const Dota &dtmp);

    ~Dota();

private:
    int sign; // 标识数据类型

    Mat mat; // 数据指针
    Dat dat; // 数据指针

public:
    void show() const;
};

class Dota_Pool
{
public:
    Dota_Pool();

    ~Dota_Pool();

private:
    deque<Dota> dotabox; // 保存元素队列容器
    mutex dota_mutex;    // 保护数据队列锁

public:
    void push(Mat m);

    void push(Dat d);

    Dota pop();

    // 判断数据池是否为空
    bool empty() const;

    // 返回当前数据池中元素个数
    int size() const;

    // 清空数据池全部元素
    void clear();
};

#endif