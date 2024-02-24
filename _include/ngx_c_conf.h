
// 防卫式声明
#ifndef __NGX_CONF_H__
#define __NGX_CONF_H__

#include <vector>

// 包含声明的全局变量
#include "ngx_global.h"

// 单例类，专门用于读取配置文件中的各项配置信息并保存在类内成员数组中
class CConfig
{
private:
	// 将构造函数设为私有
	CConfig();
	// 禁用拷贝构造和重载赋值运算符函数
	CConfig(const CConfig &temp) = delete;
	CConfig &operator=(const CConfig &temp) = delete;

public:
	~CConfig();

private:
	// 指向全局唯一的类对象的指针
	static CConfig *m_instance;

public:
	static CConfig *GetInstance()
	{
		if (m_instance == NULL)
		{
			// 考虑到会在主线程中就实例化出对象，因此无需加锁
			// 锁
			if (m_instance == NULL)
			{
				m_instance = new CConfig();
				static CGarhuishou cl;
			}
			// 放锁
		}

		return m_instance;
	}

	// 类中定义类，专门用于释放类对象指向的堆区内存
	class CGarhuishou
	{
	public:
		~CGarhuishou()
		{
			if (CConfig::m_instance)
			{
				delete CConfig::m_instance;
				CConfig::m_instance = NULL;
			}
		}
	};


public:
	// 加载配置文件信息到类内成员数组中
	bool Load(const char *pconfName);
	// 获取字符串类配置信息
	const char *GetString(const char *p_itemname);
	// 获取数值类配置信息
	int GetIntDefault(const char *p_itemname, const int def);

public:
	// 根据定义的结构体形式读取配置文件信息并存储到列表当中
	std::vector<LPCConfItem> m_ConfigItemList; 
};

#endif
