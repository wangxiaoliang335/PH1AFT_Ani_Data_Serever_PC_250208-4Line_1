#pragma once

#include "Ani_Data_Serever_PC.h"
#include "SocketComm.h"
#include "TimeCheck.h"
#include "StringSupport.h"

///////////////////////////////////////////////////////////////////////////////
// FILE : AlignThread.h
// Class: CAlignThread
//
// 中文说明：
//   本类为“对位(Align) 相关”的逻辑线程类，主要负责驱动 `CAlignManager` /
//   PLC 等完成对位流程控制。每一路对位工位（上对位 / 下对位 / Tray-Align 等）
//   对应一个 `CAlignThread` 实例。
//
//   - 主要职责：
//       * 在线程中周期性检查对位启动条件（`AlignCheckMethod` /
//         `AlignFirstCheckMethod`）
//       * 根据 Panel / Tray 状态向 Align 设备发出 Grab / Align 命令
//         （`AlignGrabMethod`、`TrayCheckNTrayAlignGrabMethod`、
//          `TrayLowerAlignGrabMethod`）
//       * 解析 Align 结果，并通过 `AlignPLCResult` 把结果写入 PLC，
//         完成 OK/NG 判定及位置补偿
//
//   - 线程与同步：
//       * 通过 `CreateTask` / `CloseTask` 创建和关闭 MFC 工作线程
//       * 线程入口函数为 `AlignThreadProc`，循环调用 `ThreadRun`
//       * 使用 `m_csSocketSend` 对与 Align 设备的发送操作加锁，避免多线程竞争
///////////////////////////////////////////////////////////////////////////////

class CAlignThread
{
public:
	CAlignThread(int iAlignType, int iAlignTypeNum, int iAlignNum);
	virtual ~CAlignThread();
	void ThreadRun();

	BOOL CreateTask();
	void CloseTask();
	
	void AlignCheckMethod();
	void AlignFirstCheckMethod();
	
	void AlignGrabMethod(int PnaelNum, int iPositionTXY, int iAlignNum);
	void TrayCheckNTrayAlignGrabMethod(int iPanelNum);
	void TrayLowerAlignGrabMethod(int iPanelCount, int iAlignNum);

	void AlignPanelCheck(int iPanelAddr, int iReceivedAddr, int iAlignNum);
	void AlignLightControl(int iPanelNum);

	void AlignPLCResult(int iPanelNum, int iAlignType, int iAlignNum, int iResultCode, CString strResultMsg, CString strPanelID);


	CWinThread *m_pThreadAlign;
	static UINT AlignThreadProc(LPVOID pParam);
	HANDLE m_hQuit;

public:
	int m_iAlignType;
	int m_iAlignTypeNum;
	int m_iAlignNum;

	int m_AlignCheckCount;
	long m_positionResult;
	long m_AlignCount;
	long m_PanelCount;
	long m_AlignReverse;

	BOOL m_bFirstStatus;
	CTimerCheck time_check;

	BOOL m_bStartAlign[6];
	BOOL m_bStartFlag[6];
	CCriticalSection m_csSocketSend;
};