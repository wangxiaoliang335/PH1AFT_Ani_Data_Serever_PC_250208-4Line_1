#pragma once

///////////////////////////////////////////////////////////////////////////////
// FILE : AlignManager.h
// Class: CAlignManager
//
// 中文说明：
//   本类继承自 `CSocketComm`，用于管理 MC 与对位/Tray-Align/下对准等
//   相关设备之间的 Socket 通信，是 MC <-> Align 系统的专用通信管理类。
//
//   - 连接与服务器
//       * `SocketServerOpen`：在指定端口上打开 Align 相关设备的服务器监听
//       * `getConectCheck`：检查当前是否与对位设备保持连接
//
//   - 发送与日志
//       * `SocketSendto`：向指定通道/设备号发送 Align 命令或数据
//       * `LogWrite`：记录与 Align 设备的通信日志
//
//   - 事件与数据回调
//       * `OnDataReceived`：对位/Tray-Align 等设备返回数据的统一接收入口
//       * `OnEvent`：处理连接、断线、错误等 Socket 事件
//
//   - 业务功能接口
//       * `AlignLightOff`：对位光源关闭控制
//       * `AlignModelRequest` / `AlignPcTimeRequest`：模型信息/时间同步请求
//       * `AlignGrabEnd` / `AlignTrayCheckGrabEnd` / `AlignTrayAlignGrabEnd`
//         / `AlignTrayLowerAlignGrabEnd`：对应各类抓图结束/对位完成的通知处理
//       * `AlignDataSum`：对位结果统计与汇总
//
//   - 状态记录
//       * `m_bCheck[]`：通道连接或状态检查标志
//       * `m_lastContent` / `m_lastCommand` / `m_lastRequest`：
//         保存最近一次通信的内容、命令和请求字符串，便于调试和追溯
//       * `m_iAlignType` / `m_iAlignTypeNum` / `m_iAlignNum`：对位类型及通道编号等配置
///////////////////////////////////////////////////////////////////////////////

#include "Ani_Data_Serever_PC.h"
//#include "ClientSocket.h"
#include "SocketComm.h"
#include "EZini.h"
#include "StringSupport.h"


class CAlignManager : public CSocketComm
{
public:
	CAlignManager(int iAlignType, int iAlignTypeNum, int iAlignNum);
	virtual ~CAlignManager();

	void LogWrite(int iNum, CString strContents);
	void SocketSendto(int iNum, CString strContents, int iCommand);

	bool SocketServerOpen(CString strServerPort);
	BOOL getConectCheck();
	virtual void OnDataReceived(const LPBYTE lpBuffer, DWORD dwCount);
	virtual void OnEvent(UINT uEvent, LPVOID lpvData);

	void AlignLightOff(int iNum);

	void AlignModelRequest();
	void AlignPcTimeRequest();

	void AlignGrabEnd(CString strContents);
	void AlignTrayCheckGrabEnd(CString strContents);
	void AlignTrayAlignGrabEnd(CString strContents);
	void AlignTrayLowerAlignGrabEnd(CString strContents);

	void AlignDataSum(int iChNum, int iResultValue, int iInspSection);

	BOOL m_bCheck[2];
	CString m_lastContent;
	CString m_lastCommand;
	CString m_lastRequest;

	CString GetLastContents() { return m_lastContent; }
	CString GetLastCommand() { return m_lastCommand; }
	CString GetLastRequest() { return m_lastRequest; }

public:
	BOOL m_bstart = FALSE;//HDM test
	BOOL m_testT[ChMaxCount];

	int m_iNum;
	int m_iAlignType;
	int m_iAlignTypeNum;
	int m_iAlignNum;
};