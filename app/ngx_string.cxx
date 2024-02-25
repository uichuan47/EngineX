
// 本文件存放字符串处理相关函数的实现

#include <stdio.h>
#include <string.h>

// 去除字符串尾部空格
void Rtrim(char *string)
{
	// 字符串长度
	size_t len = 0;

	// 字符串为空直接返回
	if (string == NULL)
		return;

	// 获取字符串长度
	len = strlen(string);

	// 尾部为空格时删除尾部字符，len--
	while (len > 0 && string[len - 1] == ' ') // 位置换一下
		string[--len] = 0;

	return;
}

// 去除字符串首部空格
void Ltrim(char *string)
{
	// 字符串长度
	size_t len = 0;
	len = strlen(string);

	// 指向字符串头部的指针
	char *p_tmp = string;

	// 不是以空格开头，则无空格，直接返回
	if ((*p_tmp) != ' ')
		return;

	// 查询第一个不为空格的字符位置
	while ((*p_tmp) != '\0')
	{
		if ((*p_tmp) == ' ')
			p_tmp++;
		else
			break;
	}

	// 此时 p_tmp 指向字符串中第一个不为空格的位置或字符串尾部

	// 若此时指向字符串结尾，则全是空格，直接返回
	if ((*p_tmp) == '\0')
	{
		*string = '\0';
		return;
	}

	// 重新定义指针指向字符串头部
	char *p_tmp2 = string;

	// 依次拷贝
	while ((*p_tmp) != '\0')
	{
		(*p_tmp2) = (*p_tmp);
		p_tmp++;
		p_tmp2++;
	}

	// 设置结束标志
	(*p_tmp2) = '\0';

	return;
}