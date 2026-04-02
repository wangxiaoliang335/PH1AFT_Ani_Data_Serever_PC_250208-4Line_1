#pragma once

#include "Ani_Data_Serever_PC.h"

#if _SYSTEM_AMTAFT_

#include "SocketComm.h"
#include "TimeCheck.h"
#include "StringSupport.h"
#include "ICWProtocol.h"
#include "MNetH.h"
#include "MNetHData.h"

class CVisionThread : public CSocketComm
{
public:
	CVisionThread();
	virtual ~CVisionThread();

	void ThreadRun();
	bool SocketServerOpen(CString strServerPort);
	BOOL getConectCheck();
	BOOL CreateTask();
	void CloseTask();
	void RemoveClient();
	void VisionFirstCheckMethod(int Num);
	void VisionCheckMethod(int Num);
	void VisionInspectionMethod(int Num, int panelNum);
	void ParsingGrabEnd(int Num, CString strContents);
	void ParsingInspectionResult(int Num, CString strContents);
	void ParsingModelRequest(int Num, CString strContents);
	void ParsingPcTimeRequest(int Num, CString strContents);
	void SocketSendto(int Num, CString strContents, int iCommand);
	void LogWrite(CString strContents, int Num);
	BOOL VisionVecAdd(CString strPanel, CString strFpcID, int iPanelNum, int iIndexNum, int iPCNo, int iCurIndex, const CString& strUniqueID = _T(""));

	void AutoFocusAxis(int Num, int iCommand, CString strContents);
	void AutoFocusSave(int Num);

	void VisionPanelCheck();

	void VisionPLCResult(int Num, int iPanelNum, CString ResultMsg, int ResultCode, CString strPanelID, int iSendNGBuffer = 0);

	void ParshingVisionData(int Num, CString strContents);

	// ICW通信回调函数
	void OnICWStart(const ICW_StartInfo& startInfo);
	void OnICWSnapFN();
	void OnICWFinishFN(const ICW_LegacyFinishInfo& finishInfo);

	// 发送 ICW Start$ 消息（包含所有治具信息）
	// bSimulation: TRUE=模拟模式直接发送所有槽位，FALSE=从PLC读取实际槽位状态
	void SendICWStartMessage(BOOL bSimulation = FALSE);

	// 检查 ICW Start$ 是否正在等待响应（用于防止重复发送）
	BOOL IsICWStartInProgress() const
	{
		for (int i = 0; i < 4; i++)
		{
			if (m_bICWStartSent[i])
				return TRUE;
		}
		return FALSE;
	}

	// 设置 ICW Start$ 发送标志（用于模拟测试模式）
	void SetICWStartSent(BOOL bSent)
	{
		for (int i = 0; i < 4; i++)
			m_bICWStartSent[i] = bSent;
	}

	// ICW Start$ 消息发送标志（public以便外部访问）
	BOOL m_bICWStartSent[4];

	virtual void OnDataReceived(const LPBYTE lpBuffer, DWORD dwCount);
	virtual void OnEvent(UINT uEvent, LPVOID lpvData);

	CWinThread *m_pThreadVision;
	static UINT VisionThreadProc(LPVOID pParam);
	HANDLE m_hQuit;

	std::vector<CString> m_lastContent;
	std::vector<CString> m_lastCommand;
	std::vector<CString> m_lastRequest;

	CString GetLastContents(int index) { return m_lastContent[index]; }
	CString GetLastCommand(int index) { return m_lastCommand[index]; }
	CString GetLastRequest(int index) { return m_lastRequest[index]; }
	
private:
	CString m_strModelName;

	long m_AutoFocusPosition;
	int m_iVisionSocketCheckCount;
	int m_iPcNum;

	CCriticalSection m_csDfsData;
	CCriticalSection m_csSocketSend;
	BOOL m_bFirstStatus;
	CTimerCheck time_check;

	BOOL m_bStartVision[4];
	BOOL m_bStartFlag;

	BOOL m_bAutoFocusStart[MaxCamCount];
	BOOL m_bAutoFocusStartFlag;

};

#endif