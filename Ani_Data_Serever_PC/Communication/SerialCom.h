// SerialCom.h: interface for the CSerialCom class.
//
//////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// FILE : SerialCom.h
// Class: CSerialCom
//
// 中文说明：
//   本类是在底层串口线程类 `CCommThread` 之上的“普通串口(FFU 等)”封装，
//   用于与 FFU 等设备通过 RS232 串口进行通信，是 MC <-> FFU 的专用通道。
//
//   - 端口参数
//       * 通过 `InitData` / `IndexComPort` / `IndexBaud` / `IndexData`
//         `IndexStop` / `IndexParity` 等函数，对串口号、波特率、数据位、停止位、校验位做统一管理
//
//   - 端口控制
//       * `OnPortOpen` / `OnPortClose`：打开和关闭实际串口
//       * `SetWriteComm`：将要发送给 FFU 的命令写入串口（底层调用 `WriteComm`）
//
//   - 线程与任务
//       * `CreateTask` / `CloseTask` / `ThreadRun`：为 FFU 通信创建独立工作线程，进行循环收发
//       * `m_pThreadFFU` / `m_hQuit`：控制该工作线程的生命周期
//
//   - 状态与数据
//       * `m_CurrentFFUBuf` / `m_FFUSettingBuf`：缓存 FFU 当前状态和设定值
//       * `m_bStartFlag` / `m_bStart`：指示 FFU 通信是否处于启动/运行状态
//       * `m_lastContent` / `m_lastRequest`：保存最近一次与 FFU 的通讯内容与请求
//
//   - 数据接收
//       * 重写 `OnDataReceive`，由底层 `CCommThread` 接收到的数据在此进行协议解析与业务处理
///////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_SERIALCOM_H__8F80055D_6F0B_4855_A081_648F97096CB3__INCLUDED_)
#define AFX_SERIALCOM_H__8F80055D_6F0B_4855_A081_648F97096CB3__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "CommThread.h"

class CSerialCom : public CCommThread
{
public:
	void InitData(int PortNo);

	int		m_iStopBit;
	int		m_iSerialPort;
	int		m_iParity;
	int		m_iDataBit;
	int		m_iBaudRate;
	BOOL	m_bFirstFlag;

	CSerialCom();
	virtual ~CSerialCom();

	
	CString IndexComPort(int xPort);
	DWORD IndexBaud(int xBaud);
	BYTE IndexData(int xData);
	BYTE IndexStop(int xStop);
	BYTE IndexParity(int xParity);

public:
	bool OnPortOpen(int PortNo);
	void OnPortClose();

	void SetWriteComm(CStringA msg);

	void ThreadRun();
	BOOL CreateTask();
	void CloseTask();

	CWinThread *m_pThreadFFU;
	static UINT FFUThreadProc(LPVOID pParam);
	HANDLE m_hQuit;

	unsigned char m_CurrentFFUBuf[10];
	unsigned char m_FFUSettingBuf[10];
	CTimerCheck time_check;
	BOOL m_bStartFlag;
	BOOL m_bStart;

	void CurrentFFUData();
	void FFUDataStartMethod();
	void OnDataReceive(WPARAM wParam, LPARAM lParam);


	CString m_lastContent;
	CString m_lastRequest;

	CString GetLastContents() { return m_lastContent; }
	CString GetLastRequest() { return m_lastRequest; }
};

#endif