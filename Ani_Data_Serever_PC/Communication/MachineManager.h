//>>210422

#pragma once

///////////////////////////////////////////////////////////////////////////////
// FILE : MachineManager.h
// Class: CMachineManager
//
// 中文说明：
//   本类继承自 `CSocketComm`，用于管理 MC 与“上位/下位机(Machine)”之间的
//   TCP Socket 通信，是 MC <-> Machine 的专用通信管理类。
//
//   - 连接与会话
//       * `ConnectClient`：根据是否为上位机(bUpper)、服务器 IP/Port，建立到目标 Machine 的连接
//
//   - 发送与日志
//       * `SendMachineMessage`：向指定 PC 编号的 Machine 发送命令/数据
//       * `LogMessage`：把与 Machine 的通信内容记录到日志文件中
//       * `m_csSocketSend`：对发送操作加锁，保证多线程下发送数据的安全性
//
//   - 事件与数据回调
//       * `OnDataReceived`：收到 Machine 端发来的数据包时的回调入口，进行协议解析和业务处理
//       * `OnEvent`：连接断开、错误、超时等 Socket 事件的统一处理
//
//   - 其它
//       * `m_iPcNum`：当前管理的 Machine/PC 号，用于区分多台机台
///////////////////////////////////////////////////////////////////////////////

#include "SocketComm.h"
#include "EZini.h"
#include "StringSupport.h"


class CMachineManager:  public CSocketComm
{
public:
	CMachineManager(int iPcNum);
	virtual ~CMachineManager();

	virtual void OnDataReceived(const LPBYTE lpBuffer, DWORD dwCount);
	virtual void OnEvent(UINT uEvent, LPVOID lpvData);

	bool ConnectClient(bool bUpper, CString strServerIP, CString strServerPort);
	void SendMachineMessage(CString strMsg, int iPcNum, int iCommand);
	void LogMessage(CString strContents);

	CCriticalSection m_csSocketSend;

	int m_iPcNum;
};

//<<