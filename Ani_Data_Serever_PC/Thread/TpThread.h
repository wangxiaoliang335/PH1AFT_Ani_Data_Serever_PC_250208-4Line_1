#pragma once

#if _SYSTEM_AMTAFT_

#include "Ani_Data_Serever_PC.h"
#include "TimeCheck.h"
#include "StringSupport.h"

///////////////////////////////////////////////////////////////////////////////
// FILE : TpThread.h
// Class: CTpThread
//
// 中文说明：
//   本类为 TP（Touch Panel / 触摸测试机）相关的控制线程，主要与 `CTpManager`
//   / PLC 协同，完成 TP 通讯状态监控、触摸检开始/结束时序等。
//
//   - 主要职责：
//       * 在线程中调用 `ThreadRun`，根据 PLC / 内部标志判断是否需要向 TP
//         发送开始检验指令或轮询 TP 状态
//       * 通过 `CreateTask` / `CloseTask` 创建和销毁 TP 控制线程
//       * 在线程入口 `TpThreadProc` 中循环运行，结合 `CTimerCheck`(`time_check`)
//         对 TP 超时、心跳等进行监控
//
//   - 状态变量：
//       * `m_bFirstStatus`：用于区分上电初始阶段与正常生产阶段的 TP 控制逻辑
///////////////////////////////////////////////////////////////////////////////

class CTpThread
{
public:
	CTpThread();
	virtual ~CTpThread();
	void ThreadRun();
	BOOL CreateTask();
	void CloseTask();

	CWinThread *m_pThreadTp;
	static UINT TpThreadProc(LPVOID pParam);
	HANDLE m_hQuit;

	CTimerCheck time_check;
	void Initialize();
	BOOL m_bFirstStatus;
};
#endif