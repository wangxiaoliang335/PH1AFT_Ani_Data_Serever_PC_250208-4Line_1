
#pragma once
#define WM_COMM_READ (WM_USER +1)
#define BUFF_SIZE			4192

///////////////////////////////////////////////////////////////////////////////
// FILE : CommThread.h
// Class: CQueue / CCommThread
//
// 中文说明：
//   本文件定义了串口通信的基础队列类 `CQueue` 与串口线程类 `CCommThread`，
//   是所有串口类（如 `CSerialCom`、`CSerialRS485`）的底层通用实现。
//
//   - `CQueue`
//       * 提供一个简单的环形缓冲区，用于缓存从串口收到的原始字节流
//       * 支持入队/出队操作（`PutByte` / `GetByte`），便于逐字节解析协议帧
//
//   - `CCommThread`
//       * 负责打开/关闭串口（`OpenPort` / `ClosePort`）
//       * 通过 `ThreadWatchComm` 创建后台线程，使用重叠 I/O 持续接收数据
//       * 收到数据后放入 `m_QueueRead`，并通过 `OnDataReceive` 虚函数回调上层
//       * 上层串口类只需继承并重写 `OnDataReceive`，即可基于此框架实现各自协议
///////////////////////////////////////////////////////////////////////////////

class CQueue
{
public:
	BYTE buff[BUFF_SIZE];
	int m_iHead, m_iTail;
	CQueue();
	void Clear();
	int GetSize();
	BOOL PutByte(BYTE b);
	BOOL GetByte(BYTE *pb);
};

class CCommThread
{
public:
	CCommThread();
	~CCommThread();
	
	BOOL        check;
	HANDLE		m_hComm;				
	CString		m_sPortName;			
	BOOL		m_bConnected;			
	OVERLAPPED	m_osRead, m_osWrite;	
	HANDLE		m_hThreadWatchComm;		
	WORD		m_wPortID;		
	CQueue      m_QueueRead;			

	BOOL	OpenPort(CString strPortName, DWORD dwBaud, BYTE byData, BYTE byStop, BYTE byParity);
	void	ClosePort();
	DWORD	WriteComm(BYTE *pBuff, DWORD nToWrite);

	DWORD	ReadComm(BYTE *pBuff, DWORD nToRead);

	virtual void OnDataReceive(WPARAM wParam, LPARAM lParam);
};

DWORD	ThreadWatchComm(CCommThread* pComm);

