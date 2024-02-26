
// 本文件存放通信过程中相关的宏定义、结构体定义等

#ifndef __NGX_COMM_H__
#define __NGX_COMM_H__

// 宏定义

// 包的最大长度，即包头加包体的最大长度，为留出缓冲空间，实际包长度应比最大值小 1000
#define _PKG_MAX_LENGTH 30000

// 通信 收包状态定义
// 通信过程中，表示收包过程中的各种状态的宏定义
#define _PKG_HD_INIT 0	   // 初始状态，准备接受数据包的包头
#define _PKG_HD_RECVING 1  // 正在接受包头，但包头不完整，仍需继续接收
#define _PKG_BD_INIT 2	   // 正好收到完整包头，可以开始准备接受包体
#define _PKG_BD_RECVING 3  // 正在接受包体，但包体不完整，仍需继续接收
#define _PKG_RV_FINISHED 4 // 完整包收完，在程序中并无实际用处，可直接返回 _PKG_HD_INIT 状态

// 专门存放接收到的包头的数据的数组大小，应当大于包头所占内存 > sizeof(COMM_PKG_HEADER)
#define _DATA_BUFSIZE_ 20

// 结构体定义

// 修改对齐方式为一字节对齐，防止因内存对齐导致在传输过程中出现混乱
#pragma pack(1)

// 网络通讯相关的结构体定义放

// 包头结构
typedef struct _COMM_PKG_HEADER
{
	// 数据包总长度，即包头加包体的总长度，包最大长度为 30000，2字节最大值可达 65532，所以足够保存，
	unsigned short pkgLen;

	// 消息类型代码，区分每个不同的命令，2字节
	unsigned short msgCode;

	// CRC32效验码，4字节，对数据做基本正确性的校验
	int crc32;

} COMM_PKG_HEADER, *LPCOMM_PKG_HEADER;

// 取消指定对齐，恢复缺省对齐
#pragma pack()

#endif
