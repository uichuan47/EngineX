/***********************************
 * @author alkaid
 * @since  2023.2.20
 * @brief  本文件实现一个用于测试内存性能分配的模板链表栈，函数实现
 * @date   2023.3.2
 ***********************************/
StackAlloc::StackAlloc()
{
	// 头指针置空
	head_ = nullptr;
}

// 默认析构
StackAlloc::~StackAlloc()
{
	// 清空链表栈
	clear();
}

// 判断栈是否为空
bool StackAlloc::empty()
{
	return head_ == nullptr;
}

// 返回栈顶元素
T StackAlloc::top()
{
	return head_->data;
}

// 入栈，将一个数据压入栈内
void StackAlloc::push(T element)
{
	// 调用分配器分配内存
	Node* newNode = allocator_.allocate(1);
	// 调用节点的构造函数
	allocator_.construct(newNode, Node());

	// 将节点压入链表栈
	newNode->data = element;
	newNode->prev = head_;
	head_ = newNode;
}

// 出栈，将栈顶数据弹出并返回
T StackAlloc::pop()
{
	// 出栈操作，返回出栈结果

	// 保存栈顶数据
	T result = head_->data;

	// 保存下一个节点
	Node* tmp = head_->prev;

	// 调用析构函数
	allocator_.destroy(head_);
	// 分配器释放内存
	allocator_.deallocate(head_, 1);

	// 头指针指向下一个节点
	head_ = tmp;

	// 返回结果
	return result;
}

// 清空栈内全部元素
void StackAlloc::clear()
{
	// 保存当前头节点
	Node* curr = head_;

	// 当头节点非空
	while (curr != nullptr)
	{
		// 保存下一个节点
		Node* tmp = curr->prev;

		// 释放当前节点
		allocator_.destroy(curr);
		allocator_.deallocate(curr, 1);

		// 当前节点指向下一个节点
		curr = tmp;
	}

	// 最后将头节点置空
	head_ = nullptr;
}
