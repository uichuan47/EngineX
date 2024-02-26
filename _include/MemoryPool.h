/***********************************
 * @author alkaid
 * @since  2023.2.23
 * @brief  本文件实现一个用于测试内存性能分配的模板链表栈，声明文件
 * @date   2023.3.2
 ***********************************/

#ifndef MEMORY_POOL_HPP
#define MEMORY_POOL_HPP

#include <climits>
#include <cstddef>
#include <utility>
#include <cstdint>

/***********************************
 * @brief  模板链表栈，可以通过给定的分配器进行内存分配操作
 * @param  T            数据类型
 * @param  BlockSize    内存块大小，默认为 4096
 ***********************************/
template <typename T, size_t BlockSize = 4096>
class MemoryPool
{
private:
    // 用于存储内存池中的对象槽，要么被实例化为一个存放对象的槽，要么被实例化为一个指向存放对象槽的槽指针
    union Slot_
    {
        T element;
        Slot_ *next;
    };

    // 数据指针
    typedef char *data_pointer_;
    // 对象槽
    typedef Slot_ slot_type_;
    // 对象槽指针
    typedef Slot_ *slot_pointer_;

    // 指向当前内存区块
    slot_pointer_ currentBlock_;

    // 指向当前内存区块的一个对象槽
    slot_pointer_ currentSlot_;
    // 指向当前内存区块的最后一个对象槽
    slot_pointer_ lastSlot_;
    // 指向当前内存区块中的空闲对象槽
    slot_pointer_ freeSlots_;

    // 检查定义的内存池大小是否过小
    static_assert(BlockSize >= 2 * sizeof(slot_type_), "BlockSize too small.");

public:
    // 数据类型指针
    typedef T *pointer;

    // 定义 rebind<U>::other 接口
    template <typename U>
    struct rebind
    {
        typedef MemoryPool<U> other;
    };

    // 默认构造, 初始化所有的槽指针
    MemoryPool() noexcept;

    // 析构函数，销毁当前内存池
    ~MemoryPool() noexcept;

    // 分配内存区块，并返回一个数据指针
    // 同一时间只能分配一个对象, n 和 hint 会被忽略
    pointer allocate(size_t n = 1, const T *hint = 0);

    // 销毁指针 p 指向的内存区块
    void deallocate(pointer p, size_t n = 1);

    // 调用对象构造函数，使用 std::forward 转发变参模板
    template <typename U, typename... Args>
    void construct(U *p, Args &&...args);

    // 调用对象析构函数
    template <typename U>
    void destroy(U *p);
};

#endif // MEMORY_POOL_HPP