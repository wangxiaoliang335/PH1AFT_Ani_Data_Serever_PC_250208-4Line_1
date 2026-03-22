// SerialCom.h: interface for the CSerialCom class.
//
//////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// FILE : SerialRS485.h
// Class: CSerialRS485
//
// 中文说明：
//   本类同样继承自底层串口线程类 `CCommThread`，用于 RS485 总线设备的通信，
//   与 `CSerialCom` 类似，但主要面向多点/长距离的 RS485 设备。
//
//   - 端口参数
//       * 通过 `InitData` / `IndexComPort` / `IndexBaud` / `IndexData`
//         `IndexStop` / `IndexParity` 等函数，对 RS485 串口的参数做统一管理
//
//   - 端口控制
//       * `OnPortOpen` / `OnPortClose`：打开和关闭 RS485 物理端口
//
//   - 线程与任务
//       * `CreateTask` / `CloseTask` / `ThreadRun`：为 RS485 通信创建独立工作线程
//       * `m_pThread` / `m_hQuit`：控制线程的启动和退出
//
//   - 状态与数据
//       * `time_check` / `m_bStartFlag` / `m_bStart` / `m_bFirstCheck`：
//         用于超时监控、启动标志以及首次通讯检查
//       * `m_lastContent` / `m_lastRequest`：保存最近一次 RS485 通讯内容与请求
//
//   - 数据接收
//       * 通过重写 `OnDataReceive`，将底层 `CCommThread` 收到的 RS485 数据按协议解析
//       * `ARSDataStartMethod`：按设备编号启动/触发特定 RS485 通讯流程
///////////////////////////////////////////////////////////////////////////////


#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "CommThread.h"

class CSerialRS485 : public CCommThread
{
public:
	void InitData(int PortNo);

	int		m_iStopBit;
	int		m_iSerialPort;
	int		m_iParity;
	int		m_iDataBit;
	int		m_iBaudRate;


	CSerialRS485();
	virtual ~CSerialRS485();

	CString IndexComPort(int xPort);
	DWORD IndexBaud(int xBaud);
	BYTE IndexData(int xData);
	BYTE IndexStop(int xStop);
	BYTE IndexParity(int xParity);

public:
	bool OnPortOpen(int PortNo);
	void OnPortClose();

	void ThreadRun();
	BOOL CreateTask();
	void CloseTask();

	CWinThread *m_pThread;
	static UINT ThreadProc(LPVOID pParam);
	HANDLE m_hQuit;

	CTimerCheck time_check;
	BOOL m_bStartFlag;
	BOOL m_bStart;
	BOOL m_bFirstCheck;

	void ARSDataStartMethod(int iNum);
	void OnDataReceive(WPARAM wParam, LPARAM lParam);


	CString m_lastContent;
	CString m_lastRequest;

	CString GetLastContents() { return m_lastContent; }
	CString GetLastRequest() { return m_lastRequest; }
};

