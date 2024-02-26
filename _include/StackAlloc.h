/***********************************
 * @author alkaid
 * @since  2023.2.20
 * @brief  本文件实现一个用于测试内存性能分配的模板链表栈，声明文件
 * @date   2023.3.2
 ***********************************/

#ifndef __STACK_ALLOC__
#define __STACK_ALLOC__

#include <memory>

// 栈节点
template <typename T>
struct StackNode_
{
    // 保存数据类型
    T data;

    // 节点指针，指向前驱节点
    StackNode_ *prev;
};

/***********************************
 * @brief  模板链表栈，可以通过给定的分配器进行内存分配操作
 * @param  T        数据类型
 * @param  Alloc    内存分配器，默认为系统提供
 ***********************************/
template <class T, class Alloc = std::allocator<T>>
class StackAlloc
{
    // 类内自定义简化类型名
    typedef StackNode_<T> Node;
    typedef typename Alloc::template rebind<Node>::other allocator;

private:
    // 链表栈的头节点
    Node *head_;
    // 内存分配器
    allocator allocator_;

public:
    // 默认构造
    StackAlloc();

    // 默认析构
    ~StackAlloc();

    // 判断栈是否为空
    bool empty();

    // 返回栈顶元素
    T top();

    // 入栈，将一个数据压入栈内
    void push(T element);

    // 出栈，将栈顶数据弹出并返回
    T pop();

    // 清空栈内全部元素
    void clear();
};

#endif // __STACK_ALLOC__