
#include "stdafx.h"

#if _SYSTEM_AMTAFT_

// 最多4个治具，固定最大数量
const int MAX_JIG = 4;

#include "DlgMainView.h"
#include "VisionThread.h"
#include "DFSInfo.h"
#include "DBInterface.h"
#include "DataModels.h"
#include "AniUtil.h"

// 简化版 DFS 时间转换函数（从 PlcThread 移植）
CString DFSDataTimeParser(USHORT Time1, USHORT Time2, USHORT Time3)
{
	CString strResult;
	char charTemp1[10], charTemp2[10], charTemp3[10];
	CString strTemp1, strTemp2, strTemp3;
	CString strTime1, strTime2, strTime3;

	_itoa(Time1, charTemp1, 16);
	_itoa(Time2, charTemp2, 16);
	_itoa(Time3, charTemp3, 16);

	MultiByteToWideChar(CP_UTF8, 0, charTemp1, 10, strTemp1.GetBuffer(10 + 1), 10 + 1);
	strTemp1.ReleaseBuffer();
	MultiByteToWideChar(CP_UTF8, 0, charTemp2, 10, strTemp2.GetBuffer(10 + 1), 10 + 1);
	strTemp2.ReleaseBuffer();
	MultiByteToWideChar(CP_UTF8, 0, charTemp3, 10, strTemp3.GetBuffer(10 + 1), 10 + 1);
	strTemp3.ReleaseBuffer();

	strTime1.Format(_T("%s"), strTemp1);
	strTime2.Format(_T("%s"), strTemp2);
	strTime3.Format(_T("%s"), strTemp3);

	strResult.Format(_T("%s-%s-%s %s:%s:%s"), 
		strTime1.Mid(0, 2), strTime1.Mid(2, 2), strTime2.Mid(0, 2),
		strTime2.Mid(2, 2), strTime3.Mid(0, 2), strTime3.Mid(2, 2));

	return strResult;
}

CString DFSDataTactTimeParser(USHORT Time1, USHORT Time2, USHORT Time3, USHORT Time4)
{
	CString strResult;
	int iStartTime = (Time1 << 16) + Time2;
	int iEndTime = (Time3 << 16) + Time4;
	int iResult = iEndTime - iStartTime;
	if (iResult < 0) iResult = 0;
	strResult.Format(_T("%d"), iResult);
	return strResult;
}


CVisionThread::CVisionThread()
{
	m_hQuit = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_bFirstStatus = TRUE;
	m_lastContent.resize(2);
	m_lastCommand.resize(2);
	m_lastRequest.resize(2);
	theApp.m_bVisionDeleteFlag = TRUE;
	theApp.m_lastInspResultVec.resize(8);

	// 初始化 ICW Start$ 发送标志
	for (int i = 0; i < 4; i++)
		m_bICWStartSent[i] = FALSE;

	// 初始化 DefectCode Start/End 状态
	for (int i = 0; i < ChMaxCount; i++)
	{
		m_bDefectCodeStart[i] = FALSE;
		m_bDefectCodeEnd[i] = FALSE;
	}

	// 初始化 Jig 数据成员变量
	m_strJigPanel = _T("");
	m_strJigFpcID = _T("");
	m_strJigUniqueID = _T("");
	m_iJigCurIndex = 0;
	m_iJigIndexPanelNum = 0;

	// 初始化 ICW 断线日志去重标志
	m_bICWDisconnectedLogged = FALSE;

	// ??? ICW ??????
	theApp.m_ICWCommManager.SetStartCallback([this](const ICW_StartInfo& info) {
		OnICWStart(info);
	});
	theApp.m_ICWCommManager.SetSnapFNCallback([this]() {
		OnICWSnapFN();
	});
	theApp.m_ICWCommManager.SetFinishFNCallback([this](const ICW_LegacyFinishInfo& info) {
		OnICWFinishFN(info);
	});
}

CVisionThread::~CVisionThread()
{
}

void CVisionThread::ThreadRun()
{
	AutoFocusData pAutoFocusData;
	for (auto &InspResult : theApp.m_lastInspResultVec)
		InspResult.Reset();

	LogWrite(_T("[Vision] Thread Started"), 0);

	// ICW 状态变化检测（避免日志刷屏）
	static BOOL s_bLastICWConnected = FALSE;
	static int s_iLastAOIPassMode = -1;
	static DWORD s_dwLastICWLogTime = 0;
	const DWORD ICW_LOG_INTERVAL_MS = 60000; // 1分钟

	while (::WaitForSingleObject(m_hQuit, 50) != WAIT_OBJECT_0)
	{
		//// 旧的 Vision PC1/PC2 连接状态检查 (已废弃，现使用 ICW 6501端口)
		//theApp.m_VisionConectStatus[0] = theApp.m_VisionSocketManager[0].getConectCheck();
		//theApp.m_VisionConectStatus[1] = theApp.m_VisionSocketManager[1].getConectCheck();

		// 6501端口（ICW）连接状态
		BOOL bICWConnected = theApp.m_ICWCommManager.IsConnected();
		int iAOIPassMode = theApp.m_AOIPassMode;
		DWORD dwCurrentTime = GetTickCount();

		// ICW 状态变化时输出日志（避免刷屏）
		if (bICWConnected != s_bLastICWConnected || iAOIPassMode != s_iLastAOIPassMode ||
			(dwCurrentTime - s_dwLastICWLogTime > ICW_LOG_INTERVAL_MS))
		{
			LogWrite(CStringSupport::FormatString(_T("[Vision] ICW Connected=%d, AOIPassMode=%d"),
				bICWConnected, iAOIPassMode), 0);
			s_bLastICWConnected = bICWConnected;
			s_iLastAOIPassMode = iAOIPassMode;
			s_dwLastICWLogTime = dwCurrentTime;
		}

		//if (theApp.m_bAllPassMode)
		//	continue;

		if (bICWConnected || theApp.m_AOIPassMode)
		{
			// ICW 重新连接时，重置断线日志去重标志，下次断线时可以再次打印
			if (m_bICWDisconnectedLogged)
			{
				m_bICWDisconnectedLogged = FALSE;
				LogWrite(_T("[Vision] ICW Reconnected, Reset VisionPCStatus and FirstStatus"), 0);
			}
			if (m_bFirstStatus)
			{
				LogWrite(_T("[Vision] First Status - Initializing"), 0);
				m_bFirstStatus = FALSE;
				time_check.SetCheckTime(60000);
				time_check.StartTimer();
				// 旧的 Vision PC 初始化检查（已废弃，现使用 ICW）
				//for (int ii = 0; ii < PCMaxCount; ii++)
				//{
				//	VisionFirstCheckMethod(ii);
				//	theApp.m_VisionPCStatus[ii] = TRUE;
				//}

				for (int jj = 0; jj < PanelMaxCount; jj++)
					m_bStartVision[jj] = FALSE;
			}

			//TEST Model 이 (TRUE) Model 변경도 안보고 그냥 계속 진행 합니다.
			//TEST Model 이 (FALSE) Model 변경 및 생성 계속 check 
			//// 旧的 Vision PC1/PC2 换型信号检查 (已废弃)
			//if (theApp.m_PlcConectStatus == FALSE || theApp.m_ChangeModelVision1 == TRUE || theApp.m_ChangeModelVision2 == TRUE)
			//	continue;
			if (theApp.m_PlcConectStatus == FALSE)
				continue;

			//// 旧的 Vision PC 连接检查 (已废弃，现使用 ICW)
			//if (time_check.IsTimeOver())
			//{
			//	time_check.StartTimer();
			//	for (int ii = 0; ii < PCMaxCount; ii++)
			//	{
			//		VisionCheckMethod(ii);
			//		if (theApp.m_VisionSocketManager[ii].m_iVisionSocketCheckCount > 5)
			//		{
			//			LogWrite(CStringSupport::FormatString(_T("[Vision] Vision PC %d Client Drop"), ii), ii);
			//		}
			//		theApp.m_VisionSocketManager[ii].m_iVisionSocketCheckCount++;
			//	}
			//}

			// VisionPlcSend 状态变化检测（避免刷屏）
			static BOOL s_bLastVisionPlcSend = FALSE;
			BOOL bCurrentVisionPlcSend = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_VisionPlcSend, 0);
			if (bCurrentVisionPlcSend != s_bLastVisionPlcSend)
			{
				if (bCurrentVisionPlcSend)
				{
					LogWrite(_T("[Vision] GetPLC eBitType_VisionPlcSend = TRUE"), 0);
				}
				else
				{
					LogWrite(_T("[Vision] GetPLC eBitType_VisionPlcSend = FALSE, Set eBitType_VisionPcReceiver = FALSE"), 0);
				}
				s_bLastVisionPlcSend = bCurrentVisionPlcSend;
			}

			if (bCurrentVisionPlcSend)
			{
				VisionPanelCheck();
			}
			else
			{
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionPcReceiver, 0, FALSE);
			}

			// 一次性读取所有治具的 VisionStart1 信号到缓存，避免在 VisionPanelCheck 和 SendICWStartMessage 中重复读取
			BOOL startFlagsCache[4] = { FALSE, FALSE, FALSE, FALSE };
			for (int jj = 0; jj < MAX_JIG; ++jj)
				startFlagsCache[jj] = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_VisionStart1, OffSet_0 + jj);

			for (int ii = 0; ii < PanelMaxCount; ii++)
			{
				m_bStartFlag = startFlagsCache[ii];  // 使用缓存的信号

				// Panel StartFlag 状态变化时输出日志（避免刷屏）
				if (m_bStartFlag == FALSE)
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_VisionResult1 + ii, &m_codeReset);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionGrabEnd1 + ii, OffSet_0, FALSE);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionEnd1 + ii, OffSet_0, FALSE);
					// 只在状态从 TRUE 变为 FALSE 时输出日志
					if (m_bStartVision[ii])
					{
						LogWrite(CStringSupport::FormatString(_T("[Vision] Panel %d: StartFlag=FALSE, Reset VisionResult=%d, GrabEnd=FALSE, End=FALSE"),
							ii + 1, m_codeReset), 0);
					}
				}
				else if (!m_bStartVision[ii])
				{
					// 只在状态从 FALSE 变为 TRUE 时输出日志
					LogWrite(CStringSupport::FormatString(_T("[Vision] Panel %d: StartFlag=TRUE"), ii + 1), 0);
				}

				if (m_bStartVision[ii] == !m_bStartFlag)
				{
					m_bStartVision[ii] = m_bStartFlag;
					LogWrite(CStringSupport::FormatString(_T("[Vision] Panel %d Start Flag [%s]"), ii + 1, m_bStartVision[ii] == FALSE ? _T("FALSE") : _T("TRUE")), 0);

					if (m_bStartFlag == TRUE)
					{
						LogWrite(CStringSupport::FormatString(_T("[Vision] Panel %d: StartFlag=TRUE, Reset before inspection, VisionResult=%d, GrabEnd=FALSE, End=FALSE"),
							ii + 1, m_codeReset), 0);
						theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_VisionResult1 + ii, &m_codeReset);
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionGrabEnd1 + ii, OffSet_0, FALSE);
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionEnd1 + ii, OffSet_0, FALSE);
						// 4-Line 系统使用 ICW 统一通信，无需区分 PC1/PC2
						LogWrite(CStringSupport::FormatString(_T("[Vision] Calling VisionInspectionMethod Panel=%d"), ii), 0);
						VisionInspectionMethod(ii, ii, startFlagsCache);  // 传入缓存的信号
					}
				}
			}

			//for (int ii = 0; ii < MaxCamCount; ii++)
			//{
			//	m_bAutoFocusStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_AutoFocusEnd1 + ii, OffSet_0);

			//	if (m_bAutoFocusStartFlag == TRUE)
			//	{
			//		LogWrite(CStringSupport::FormatString(_T("[AutoFocus] Cam %d: GetAutoFocusData, Set AutoFocusSave=FALSE, AutoFocusStart=FALSE"), ii + 1), 0);
			//		theApp.m_pEqIf->m_pMNetH->SetAutoFocusData(eWordType_AutoFocusMoter1, &pAutoFocusData);
			//		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AutoFocusSave1 + ii, OffSet_0, FALSE);
			//		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AutoFocusStart1 + ii, OffSet_0, FALSE);
			//	}

			//	if (m_bAutoFocusStart[ii] == !m_bAutoFocusStartFlag)
			//	{
			//		m_bAutoFocusStart[ii] = m_bAutoFocusStartFlag;
			//		LogWrite(CStringSupport::FormatString(_T("[AutoFocus] Cam %d End Flag [%s]"), ii + 1, m_bAutoFocusStart[ii] == FALSE ? _T("FALSE") : _T("TRUE")), 0);

			//		if (m_bAutoFocusStartFlag == TRUE)
			//		{
			//			if (theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_AutoFocusSave1 + ii, OffSet_0))
			//			{
			//				//save
			//				LogWrite(CStringSupport::FormatString(_T("[AutoFocus] Cam %d: Save Mode, SocketSend GOOD, MC_FOCUS_SAVE_POS_DONE"), ii + 1), 0);
			//				theApp.m_VisionSocketManager->SocketSendto(ii, _T("GOOD"), MC_FOCUS_SAVE_POS_DONE);
			//				theApp.m_pEqIf->m_pMNetH->SetAutoFocusData(eWordType_AutoFocusMoter1, &pAutoFocusData);
			//				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AutoFocusSave1 + ii, OffSet_0, FALSE);
			//				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AutoFocusStart1 + ii, OffSet_0, FALSE);

			//				LogWrite(CStringSupport::FormatString(_T("[CAM_%d] Auto Focus Save Success"), ii + 1), ii);
			//			}
			//			else
			//			{
			//				//axis Move
			//				theApp.m_pEqIf->m_pMNetH->GetPlcWordData(eWordType_AutoFocusMoter1, &m_AutoFocusPosition);
			//				if (m_AutoFocusPosition == 1)
			//				{
			//					LogWrite(CStringSupport::FormatString(_T("[AutoFocus] Cam %d: Axis Move Mode, Position=%d, SocketSend GOOD, MC_Z_MOVE_DONE"), ii + 1, m_AutoFocusPosition), 0);
			//					theApp.m_VisionSocketManager->SocketSendto(ii, _T("GOOD"), MC_Z_MOVE_DONE);
			//				}
			//				else
			//				{
			//					LogWrite(CStringSupport::FormatString(_T("[AutoFocus] Cam %d: Axis Move Mode, Position=%d, SocketSend GOOD, MC_FOCUS_MOVE_DONE"), ii + 1, m_AutoFocusPosition), 0);
			//					theApp.m_VisionSocketManager->SocketSendto(ii, _T("GOOD"), MC_FOCUS_MOVE_DONE);
			//				}

			//				theApp.m_pEqIf->m_pMNetH->SetAutoFocusData(eWordType_AutoFocusMoter1, &pAutoFocusData);
			//				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AutoFocusSave1 + ii, OffSet_0, FALSE);
			//				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AutoFocusStart1 + ii, OffSet_0, FALSE);

			//				LogWrite(CStringSupport::FormatString(_T("[CAM_%d] Auto Focus Axis Move Success"), ii + 1), ii);
			//			}
			//		}
			//	}
			//}
			
			for (auto &InspResult : theApp.m_lastInspResultVec)
			{
				// 检查超时：检测已开始但未完成
				if (InspResult.m_bInspStart == TRUE && InspResult.m_bResult == FALSE)
				{
					if (InspResult.time_check.IsTimeOver())
					{
						LogWrite(CStringSupport::FormatString(_T("[Vision] Panel=%d Timeout! Call VisionPLCResult with Code=%d"),
							InspResult.m_iPanelNum, m_codeTimeOut), 0);
						VisionPLCResult(InspResult.m_iPCNum,
							InspResult.m_iPanelNum,
							PLC_ResultValue[m_codeTimeOut],
							m_codeTimeOut,
							InspResult.m_cellId);

						InspResult.m_bResult = TRUE;
						theApp.m_TimeOutLog->Info(CStringSupport::FormatString(_T("[PC : %d] AOI [%s] Time out"), InspResult.m_iPCNum, InspResult.m_cellId));
					}
				}

				if (InspResult.m_bResult == TRUE && theApp.m_bVisionDeleteFlag == TRUE)
					InspResult.Reset();

			}
			
		}
		else
		{
			// 仅在首次检测到断线时打印一次，防止日志刷屏
			if (!m_bICWDisconnectedLogged)
			{
				LogWrite(_T("[Vision] ICW Disconnected, Reset VisionPCStatus and FirstStatus"), 0);
				m_bICWDisconnectedLogged = TRUE;
			}
			theApp.m_VisionPCStatus[0] = FALSE;
			theApp.m_VisionPCStatus[1] = FALSE;
			m_bFirstStatus = TRUE;
		}
	}

	LogWrite(_T("[Vision] Thread Exit"), 0);
}


void CVisionThread::OnDataReceived(const LPBYTE lpBuffer, DWORD dwCount)
{
	if (theApp.m_bExitFlag == FALSE)
		return;

	// 旧的 Vision PC Socket 通信已废弃（现使用 ICW 6501端口统一通信）
	//SockAddrIn addrin;
	//GetSockName(addrin);
	//int Num = ntohs(addrin.GetPort()) == _ttoi(VISION_PC1_PORT_NUM) ? PC1 : PC2;

	//???????? ???? ??? ?????????? for ?????? ETX ???????? ?????? ???? ??????? ????? ????
	CString strData, m_strHeader, m_strCommand, m_strContents, strParsing;
	int iFind, iFindSTX;
	MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<LPCSTR>(lpBuffer), dwCount, strData.GetBuffer(dwCount + 1), dwCount + 1);
	strData.ReleaseBuffer(dwCount);

	CStringArray responseTokens;
	CStringSupport::GetTokenArray(strData, _ETX, responseTokens);

	if (responseTokens.GetSize() == 1)
	{
		//LogWrite(_T("ETX No Message!!!"), Num);
		return;
	}

	for (int ii = 0; ii < responseTokens.GetSize() - 1; ii++)
	{
		strParsing = responseTokens[ii];

		m_strHeader.Format(_T("%x"), strParsing.GetAt(0));

		UINT iHeader = (UINT)_ttoi(m_strHeader);

		if (iHeader != _STX)
		{
			//LogWrite(_T("STX No Message!!!"), Num);
			return;
		}

		iFind = strParsing.Find(',');
		m_strCommand = strParsing.Left(iFind);

		iFindSTX = strParsing.Find((char)_STX);
		m_strCommand = m_strCommand.Mid(iFindSTX + 1, m_strCommand.GetLength());

		int iCommand = _ttoi(m_strCommand);

		m_strContents = strParsing.Mid(iFind + 1, strParsing.GetLength());

		// 旧的 Vision PC Socket 通信已废弃（现使用 ICW）
		//m_lastCommand[Num] = VS_PacketNameTable[iCommand];
		//m_lastRequest[Num] = m_strContents;

		//if (Num == PC1)
		//	theApp.m_pVisionSendReceiver1Log->Info(CStringSupport::FormatString(_T("[VS -> MC] [Command : %s] ->%s"), m_lastCommand[Num], strData));
		//else
		//	theApp.m_pVisionSendReceiver2Log->Info(CStringSupport::FormatString(_T("[VS -> MC] [Command : %s] ->%s"), m_lastCommand[Num], strData));

		CString sendMsg;
		switch (iCommand)
		{
		case VS_ARE_YOU_THERE:
			//// 旧的 Vision 连接检查计数 (已废弃)
			//theApp.m_VisionSocketManager[Num].m_iVisionSocketCheckCount = 0;
			break;
		case VS_PCTIME_REQUEST:
			//LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s"), Num, _T("RCV : VS_PCTIME_REQUEST")), Num);
			//ParsingPcTimeRequest(Num, m_strContents);
			//// 旧的 MC_STATE 发送 (已废弃，现使用 ICW)
			//sendMsg.Format(_T("%d,%d"), MC_STATE, !theApp.m_PlcThread->m_plcStart);
			//for (int ii = 0; ii < PCMaxCount; ii++)
			//	theApp.m_VisionSocketManager[ii].SocketSendto(ii, sendMsg, MC_STATE);
			break;
		case VS_STATE:
			//// 旧的 Vision 状态接收 (已废弃，现使用 ICW)
			//theApp.m_VisionPCStatus[Num] = m_strContents == _T("0") ? FALSE : TRUE;
			//LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s->%s"), Num, _T("RCV : VS_STATE"), theApp.m_VisionPCStatus[Num] == TRUE ? _T("Start") : _T("Stop")), Num);
			break;
		case VS_MODEL:
			//LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s"), Num, _T("RCV : VS_MODEL")), Num);
			//ParshingVisionData(Num, m_strContents);
			break;
		case VS_MODEL_REQUEST:
			//LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s"), Num, _T("RCV : VS_MODEL_REQUEST")), Num);
			//ParsingModelRequest(Num, m_strContents);
			break;
		case VS_MODEL_CREATE:
			//LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s"), Num, _T("RCV : VS_MODEL_CREATE")), Num);
			// 旧的 Vision PC Socket 通信已废弃
			//if (Num == PC1)
			//{
			//	theApp.m_CreateModelVision1 = FALSE;
			//	theApp.m_PlcThread->ModelCreateChangeModify(_T("ModelCreate"), _T("Vision1"), theApp.m_CreateModelVision1);
			//}
			//else
			//{
			//	theApp.m_CreateModelVision2 = FALSE;
			//	theApp.m_PlcThread->ModelCreateChangeModify(_T("ModelCreate"), _T("Vision2"), theApp.m_CreateModelVision2);
			//}
			break;
		case VS_MODEL_CHANGE:
			//LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s"), Num, _T("RCV : VS_MODEL_CHANGE")), Num);
			// 旧的 Vision PC Socket 通信已废弃
			//if (Num == PC1)
			//{
			//	theApp.m_ChangeModelVision1 = FALSE;
			//	theApp.m_PlcThread->ModelCreateChangeModify(_T("ModelChange"), _T("Vision1"), theApp.m_ChangeModelVision1);
			//}
			//else
			//{
			//	theApp.m_ChangeModelVision2 = FALSE;
			//	theApp.m_PlcThread->ModelCreateChangeModify(_T("ModelChange"), _T("Vision2"), theApp.m_ChangeModelVision2);
			//}
			break;
		case VS_GRAB_END:
			//LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s->%s"), Num, _T("RCV : VS_GRAB_END"), m_strContents), Num);
			//ParsingGrabEnd(Num, m_strContents);
			break;
		case VS_INSPECTION_OK:
			//LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s->%s"), Num, _T("RCV : VS_INSPECTION_OK"), m_strContents), Num);
			break;
		case VS_INSPECTION_RESULT:
			//LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s->%s"), Num, _T("RCV : VS_INSPECTION_RESULT"), m_strContents), Num);
			//theApp.m_pTestLog->Debug(_T("[VS %d -> MC] %s->%s"), Num, _T("RCV : VS_INSPECTION_RESULT"), m_strContents);
			//ParsingInspectionResult(Num, m_strContents);
			break;
		case VS_AUTO_CAM_SET_START:
			//LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s->%s"), Num, _T("RCV : VS_AUTO_CAM_SET_START"), m_strContents), Num);
			break;
		case VS_Z_MOVE_REQUEST:
			//LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s->%s"), Num, _T("RCV : VS_Z_MOVE_REQUEST"), m_strContents), Num);
			//AutoFocusAxis(Num, 1, m_strContents);
			break;
		case VS_FOCUS_MOVE_REQUEST:
			//LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s->%s"), Num, _T("RCV : VS_FOCUS_MOVE_REQUEST"), m_strContents), Num);
			//AutoFocusAxis(Num, 2, m_strContents);
			break;
		case VS_Z_SAVE_POS_REQUEST:
			//LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s->%s"), Num, _T("RCV : VS_Z_SAVE_POS_REQUEST"), m_strContents), Num);
			break;
		case VS_FOCUS_SAVE_POS_REQUEST:
			//LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s->%s"), Num, _T("RCV : VS_FOCUS_SAVE_POS_REQUEST"), m_strContents), Num);
			//AutoFocusSave(Num);
			break;
		case VS_VISION_TEST:
			//LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s->%s"), Num, _T("RCV : VS_VISION_TEST"), m_strContents), Num);
			//VisionInspectionMethod(0, 0);
			//VisionInspectionMethod(1, 1);
			break;
		}

	}
}

// 旧的 Vision PC 初始化检查（已废弃，现使用 ICW）
//void CVisionThread::VisionFirstCheckMethod(int Num)
//{
//	BOOL bModelCreate, bModelChange;
//	//??? ??????????? IO (MC_ARE_YOU_THERE) , PCTime(MC_PCTIME), ???(MC_MODEL)
//	CString strCommand = CStringSupport::FormatString(_T("%d,%d"), MC_ARE_YOU_THERE, theApp.m_VisionSocketManager[Num].m_iVisionSocketCheckCount);
//	SocketSendto(Num, strCommand, MC_ARE_YOU_THERE);
//	Delay(200, TRUE);
//
//	strCommand = CStringSupport::FormatString(_T("%d,%s"), MC_PCTIME, GetDateString4());
//	SocketSendto(Num, strCommand, MC_PCTIME);
//	Delay(200, TRUE);
//
//	if (Num == PC1)
//	{
//		bModelCreate = theApp.m_CreateModelVision1;
//		bModelChange = theApp.m_ChangeModelVision1;
//	}
//	else
//	{
//		bModelCreate = theApp.m_CreateModelVision2;
//		bModelChange = theApp.m_ChangeModelVision2;
//	}
//	
//	if (bModelCreate)
//	{
//		strCommand = CStringSupport::FormatString(_T("%d,%s"), MC_MODEL_CREATE, theApp.m_CurrentModel.m_AlignPcCurrentModelName);
//		SocketSendto(Num, strCommand, MC_MODEL_CREATE);
//		LogWrite(CStringSupport::FormatString(_T("[MC -> VS %d] %s->%s"), Num, MC_PacketNameTable[MC_MODEL_CREATE], strCommand), Num);
//		Delay(200, TRUE);
//	}
//	
//	if (bModelChange)
//	{
//		strCommand = CStringSupport::FormatString(_T("%d,%s"), MC_MODEL_CHANGE, theApp.m_CurrentModel.m_AlignPcCurrentModelName);
//		SocketSendto(Num, strCommand, MC_MODEL_CHANGE);
//		LogWrite(CStringSupport::FormatString(_T("[MC -> VS %d] %s->%s"), Num, MC_PacketNameTable[MC_MODEL_CHANGE], strCommand), Num);
//		Delay(200, TRUE);
//	}
//
//}

// 旧的 Vision PC Socket 检查函数已废弃（现使用 ICW）
//void CVisionThread::VisionCheckMethod(int Num)
//{
//	CString strCommand = CStringSupport::FormatString(_T("%d,%d"), MC_ARE_YOU_THERE, theApp.m_VisionSocketManager[Num].m_iVisionSocketCheckCount);
//	SocketSendto(Num, strCommand, MC_ARE_YOU_THERE);
//}

void CVisionThread::VisionPanelCheck()
{
	PanelData pPanelData; 
	FpcIDData pFpcData;
	CString strPanel, strFpcID;

	for (int ii = 0; ii < PanelMaxCount; ii++)
	{
		theApp.m_pEqIf->m_pMNetH->GetFpcIdData(eWordType_VisionFpcID1 + ii, &pFpcData);
		strFpcID = CStringSupport::ToWString(pFpcData.m_FpcIDData, sizeof(pFpcData.m_FpcIDData));

		theApp.m_pEqIf->m_pMNetH->GetPanelData(eWordType_VisionPanel1 + ii, &pPanelData);
		strPanel = CStringSupport::ToWString(pPanelData.m_PanelData, sizeof(pPanelData.m_PanelData));

		if (strFpcID.GetLength() > 0)
		{
			theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionPcReceiver, 0, TRUE);
			break;
		}
	}
}

// 旧的 Vision Socket 解析函数已废弃（现使用 ICW）
//void CVisionThread::ParsingModelRequest(int Num, CString strContents)
//{
//	CString sendMsg;
//	sendMsg.Format(_T("%d,%s"), MC_MODEL, theApp.m_CurrentModel.m_AlignPcCurrentModelName);
//	SocketSendto(Num, sendMsg, MC_MODEL);
//	LogWrite(CStringSupport::FormatString(_T("[MC -> VS %d] %s->%s"), Num, MC_PacketNameTable[MC_MODEL], sendMsg), Num);
//}

// 旧的 Vision Socket 解析函数已废弃（现使用 ICW）
//void CVisionThread::ParsingPcTimeRequest(int Num, CString strContents)
//{
//	CString sendMsg;
//	sendMsg.Format(_T("%d,%s"), MC_PCTIME, GetDateString4());
//	SocketSendto(Num, sendMsg, MC_PCTIME);
//	LogWrite(CStringSupport::FormatString(_T("[MC -> VS %d] %s->%s"), Num, MC_PacketNameTable[MC_PCTIME], sendMsg), Num);
//}

///////////////////////////////////////////////////////////////////////////////
// 发送 ICW Start$ 消息给点灯检系统
// 格式: Start$ABCD$WXYZ@
// - 工位号根据 m_CurrentIndexZone 动态生成:
//     m_CurrentIndexZone=0 → 01020304 (工位0,1,2,3)
//     m_CurrentIndexZone=1 → 05060708 (工位4,5,6,7)
//     m_CurrentIndexZone=2 → 08091011 (工位8,9,10,11)
//     m_CurrentIndexZone=3 → 12131415 (工位12,13,14,15)
// - startFlagsCache[] = 各治具的 VisionStart1 信号缓存（在调用处统一读取）
// 例: Start$01020304$01020304@ (Zone A)
///////////////////////////////////////////////////////////////////////////////
void CVisionThread::SendICWStartMessage(BOOL bSimulation, const BOOL startFlagsCache[4])
{
#if _SYSTEM_AMTAFT_
	LogWrite(_T("[ICW Start$] ========== SendICWStartMessage 开始 =========="), 0);
	LogWrite(CStringSupport::FormatString(_T("[ICW Start$] Simulation=%d, ICW Connected=%d, CurrentIndexZone=%d"),
		bSimulation, theApp.m_ICWCommManager.IsConnected(), theApp.m_CurrentIndexZone), 0);

	// 工位号根据 IndexCheck 计算的 m_indexList[iCurIndex].m_indexNum 确定
	theApp.IndexCheck();
	int iCurIndex = (theApp.m_CurrentIndexZone + (MaxZone - CZone)) % 4;
	if (iCurIndex < 0 || iCurIndex >= theApp.m_indexList.size())
	{
		LogWrite(CStringSupport::FormatString(_T("[SendICWStartMessage] iCurIndex=%d 越界, use 0"), iCurIndex), 0);
		iCurIndex = 0;
	}
	int iBaseIndex = theApp.m_indexList[iCurIndex].m_indexNum;
	LogWrite(CStringSupport::FormatString(_T("[SendICWStartMessage] IndexCalc: m_CurrentIndexZone=%d, iCurIndex=%d[%s], iBaseIndex=m_indexList[%d].m_indexNum=%d"),
		theApp.m_CurrentIndexZone, iCurIndex, PG_IndexName[iCurIndex], iCurIndex, iBaseIndex), 0);

	// 最多4个治具，固定最大数量（MAX_JIG 在文件顶部定义）
	CString strCurrentJigs = _T("");   // 当前检测的治具
	CString strMaxJigs = _T("");        // 最大治具数量

	// AUTO_TEST 模拟模式：直接发送所有槽位都有产品
	if (bSimulation)
	{
		strCurrentJigs = CStringSupport::FormatString(_T("%02d%02d%02d%02d"),
			iBaseIndex, iBaseIndex + 1, iBaseIndex + 2, iBaseIndex + 3);
		strMaxJigs = strCurrentJigs;
		LogWrite(CStringSupport::FormatString(_T("[AUTO_TEST] Simulation mode: Send Start$%s$%s@"), strCurrentJigs, strMaxJigs), 0);
	}
	else
	{
		// 如果外部传入了缓存，使用缓存；否则内部直接读取
		BOOL startFlags[4] = { FALSE, FALSE, FALSE, FALSE };
		if (startFlagsCache != NULL)
		{
			for (int jj = 0; jj < MAX_JIG; ++jj)
				startFlags[jj] = startFlagsCache[jj];
		}
		else
		{
			for (int jj = 0; jj < MAX_JIG; ++jj)
				startFlags[jj] = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_VisionStart1, OffSet_0 + jj);
		}

		for (int i = 0; i < MAX_JIG; i++)
		{
			PanelData pPanelData;
			FpcIDData pFpcData;
			CString strPanel = _T(""), strFpcID = _T("");

			theApp.m_pEqIf->m_pMNetH->GetPanelData(eWordType_VisionPanel1 + i, &pPanelData);
			strPanel = CStringSupport::ToWString(pPanelData.m_PanelData, sizeof(pPanelData.m_PanelData));

			theApp.m_pEqIf->m_pMNetH->GetFpcIdData(eWordType_VisionFpcID1 + i, &pFpcData);
			strFpcID = CStringSupport::ToWString(pFpcData.m_FpcIDData, sizeof(pFpcData.m_FpcIDData));

			if (strPanel.IsEmpty())
				strPanel = strFpcID;

			// 使用 startFlags 位信号判断治具：有信号用工位号，无信号用00
			int iStation = iBaseIndex + i;  // 工位号: 与 VisionInspectionMethod 保持一致
			if (startFlags[i] && !strPanel.IsEmpty())
			{
				strCurrentJigs += CStringSupport::FormatString(_T("%02d"), iStation);
			}
			else
			{
				strCurrentJigs += _T("00");
			}

			LogWrite(CStringSupport::FormatString(_T("[ICW Start$] Jig %d: startFlag=%d, Station=%02d, strPanel=%s, strFpcID=%s"),
				i + 1, startFlags[i], iStation, strPanel, strFpcID), 0);

			// 最大工位号根据 m_CurrentIndexZone 动态生成
			strMaxJigs += CStringSupport::FormatString(_T("%02d"), iStation);
		}
	}

	// 组装 Start$ 消息
	CString strStartMsg;
	strStartMsg.Format(_T("Start$%s$%s@"), strCurrentJigs, strMaxJigs);

	// AUTO_TEST 模拟：发送 Start$ 前按前段治具模式 UPDATE IVS_LCD_IDMap（每个治具生成新 GUID）
	if (bSimulation)
	{
		if (GetDBInterface().IsConnected())
		{
			if (!GetDBInterface().UpdateIDMapForStartPattern(strCurrentJigs))
				theApp.m_PlcLog->LOG_ERR(_T("[AUTO_TEST] UpdateIDMapForStartPattern failed: %s"), GetDBInterface().GetLastError());
			else
				theApp.m_PlcLog->Info(_T("[AUTO_TEST] IVS_LCD_IDMap updated for Start$ pattern [%s]"), strCurrentJigs);
		}
		else
			theApp.m_PlcLog->LOG_ERR(_T("[AUTO_TEST] DB not connected, skip IVS_LCD_IDMap update before Start$"));
	}

	// 发送 ICW Start$ 消息
	LogWrite(CStringSupport::FormatString(_T("[ICW Start$] Sending message: %s"), strStartMsg), 0);
	theApp.m_ICWCommManager.SendMessage(strStartMsg);
	LogWrite(CStringSupport::FormatString(_T("[ICW Start$] ========== SendICWStartMessage 完成 ==========")), 0);
#endif
}

void CVisionThread::VisionInspectionMethod(int Num, int panelNum, const BOOL startFlagsCache[4])
{
	LogWrite(CStringSupport::FormatString(_T("[VisionInspectionMethod] Panel=%d Start"), panelNum), 0);
	if (theApp.m_CurrentIndexZone < 0)
	{
		LogWrite(CStringSupport::FormatString(_T("[VisionInspectionMethod] Panel=%d Error: CurrentIndexZone=%d < 0"), panelNum, theApp.m_CurrentIndexZone), 0);
		VisionPLCResult(Num, panelNum, _T("PLC Vision IndexZone Error"), m_codeResponseError , _T("NG"));
		return;
	}

	PanelData pPanelData;
	FpcIDData pFpcData;
	CString sendMsg = _T(""), strPanel = _T(""), strProcessID = _T(""), strFpcID = _T("");
	int iCurIndex = 0, indexPanelNum = 0;

	if (theApp.m_PanelTestStart)
	{
		strPanel.Format(_T("TEST%d%s"), panelNum, GetDateString4());
		strFpcID.Format(_T("TEST%d%s"), panelNum, GetDateString4());
	}
	else
	{
		LogWrite(CStringSupport::FormatString(_T("[VisionInspectionMethod] Read PLC Panel Data from eWordType_VisionPanel1+%d"), panelNum), 0);
		theApp.m_pEqIf->m_pMNetH->GetPanelData(eWordType_VisionPanel1 + panelNum, &pPanelData);
		strPanel = CStringSupport::ToWString(pPanelData.m_PanelData, sizeof(pPanelData.m_PanelData));

		LogWrite(CStringSupport::FormatString(_T("[VisionInspectionMethod] Read PLC FPC ID from eWordType_VisionFpcID1+%d"), panelNum), 0);
		theApp.m_pEqIf->m_pMNetH->GetFpcIdData(eWordType_VisionFpcID1 + panelNum, &pFpcData);
		strFpcID = CStringSupport::ToWString(pFpcData.m_FpcIDData, sizeof(pFpcData.m_FpcIDData));
	}

	if (strPanel.IsEmpty())
		strPanel = strFpcID;

	if (strFpcID.IsEmpty())
	{
		LogWrite(CStringSupport::FormatString(_T("[VisionInspectionMethod] Panel=%d Error: FPC ID Empty"), panelNum), 0);
		VisionPLCResult(Num, panelNum, CStringSupport::FormatString(_T("PLC Vision Panel #%d ID Error"), panelNum), m_codePlcSendReceiverError, _T("NG"));
		return;
	}
	theApp.IndexCheck();
	// Index 计算日志（详细输出所有参与计算的值）
	iCurIndex = (theApp.m_CurrentIndexZone + (MaxZone - CZone)) % 4;
	LogWrite(CStringSupport::FormatString(_T("[VisionInspectionMethod] IndexCalc: m_CurrentIndexZone=%d, MaxZone=%d, CZone=%d, (MaxZone-CZone)=%d"),
		theApp.m_CurrentIndexZone, MaxZone, CZone, (MaxZone - CZone)), 0);
	LogWrite(CStringSupport::FormatString(_T("[VisionInspectionMethod] IndexCalc: formula=(%d+%d)%%4=%d"),
		theApp.m_CurrentIndexZone, (MaxZone - CZone), iCurIndex), 0);
	LogWrite(CStringSupport::FormatString(_T("[VisionInspectionMethod] IndexCalc: m_indexList[%d]=%s, m_indexNum=%d + panelNum=%d -> indexPanelNum=%d"),
		iCurIndex, PG_IndexName[iCurIndex], theApp.m_indexList[iCurIndex].m_indexNum, panelNum, theApp.m_indexList[iCurIndex].m_indexNum + panelNum), 0);
	// 边界检查：m_indexList 大小为 4（ABCZone 各占 4 个区域）
	if (iCurIndex < 0 || iCurIndex >= theApp.m_indexList.size())
	{
		LogWrite(CStringSupport::FormatString(_T("[VisionInspectionMethod] iCurIndex=%d 越界 (m_indexList.size()=%d), Panel=%d"),
			iCurIndex, theApp.m_indexList.size(), panelNum), 0);
		iCurIndex = 0;  // 安全默认值
	}
	indexPanelNum = theApp.m_indexList[iCurIndex].m_indexNum + panelNum;
	LogWrite(CStringSupport::FormatString(_T("[VisionInspectionMethod] IndexCalc: Final: iCurIndex=%d[%s], indexPanelNum=%d"), 
		iCurIndex, PG_IndexName[iCurIndex], indexPanelNum), 0);
	strProcessID = theApp.GetProcessID(strPanel);

	// 发送开始检测前更新 ivs_lcd_idmap，供检测软件使用；UniqueID 保证不重复
	CString strMarkID;
	strMarkID.Format(_T("%02d"), panelNum + 1);   // 治具号 01~04
	CString strUniqueID = GetDBInterface().GenerateUniqueIDForJig(panelNum);
	LogWrite(CStringSupport::FormatString(_T("[VisionInspectionMethod] Generate UniqueID for Jig %d, UniqueID=%s"), panelNum + 1, strUniqueID), 0);
	if (GetDBInterface().IsConnected())
	{
		if (!GetDBInterface().UpsertIDMapBeforeStart(strMarkID, panelNum, strUniqueID, strPanel, strMarkID))
			LogWrite(CStringSupport::FormatString(_T("Panel %d UpsertIDMap failed: %s"), panelNum, GetDBInterface().GetLastError()), Num);
	}

#if _SYSTEM_AMTAFT_
	// 发送 ICW Start$ 消息给点灯检系统（仅在第一个治具时发送一次）
	// 注意：此处传入 startFlagsCache，让 SendICWStartMessage 使用主循环缓存的信号
	if (/*panelNum == 0 &&*/ !m_bICWStartSent[0])
	{
		LogWrite(CStringSupport::FormatString(_T("[VisionInspectionMethod] Send ICW Start Message (AutoTestMode=%d)"), theApp.m_iAutoTestMode == 1), 0);
		SendICWStartMessage(theApp.m_iAutoTestMode == 1, startFlagsCache);
		m_bICWStartSent[0] = TRUE;
	}
#endif

	// 保存数据到成员变量，传递给 AddJigToInspection（避免重复读取PLC）
	m_strJigPanel = strPanel;
	m_strJigFpcID = strFpcID;
	m_strJigUniqueID = strUniqueID;
	m_iJigCurIndex = iCurIndex;
	m_iJigIndexPanelNum = indexPanelNum;

	// 统一调用 AddJigToInspection 处理当前治具
	AddJigToInspection(panelNum);
}

// 旧的 Vision Socket 解析函数已废弃（现使用 ICW）
//void CVisionThread::ParsingGrabEnd(int Num, CString strContents)
//{
//	CString sendMsg, strPanelID, strFpcID;
//	CStringArray responseTokens;
//	CStringSupport::GetTokenArray(strContents, _T(','), responseTokens);
//
//	strPanelID = responseTokens[0];
//	strFpcID = responseTokens[1];
//	strPanelID.Trim();
//	strFpcID.Trim();
//
//	LogWrite(CStringSupport::FormatString(_T("[ParsingGrabEnd] RCV MC_GRAB_END: PanelID=%s, FpcID=%s"), strPanelID, strFpcID), 0);
//	//// 旧的 MC_GRAB_END_RECEIVE 发送 (已废弃，现使用 ICW)
//	//sendMsg.Format(_T("%d,%s,%s"), MC_GRAB_END_RECEIVE, strPanelID, strFpcID);
//	//LogWrite(CStringSupport::FormatString(_T("[ParsingGrabEnd] Send MC_GRAB_END_RECEIVE: %s"), sendMsg), 0);
//	//SocketSendto(Num, sendMsg, MC_GRAB_END_RECEIVE);
//	
//	for (auto &InspResult : theApp.m_lastInspResultVec)
//	{
//		if (!InspResult.m_cellId.CompareNoCase(strPanelID))
//		{
//			if (InspResult.m_bInspStart == TRUE)
//			{
//				LogWrite(CStringSupport::FormatString(_T("Panel [%s] Vision Grab End"), strPanelID), Num);
//				LogWrite(CStringSupport::FormatString(_T("[ParsingGrabEnd] Set PLC VisionGrabEnd1+%d = TRUE"), InspResult.m_iPanelNum), 0);
//				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionGrabEnd1 + InspResult.m_iPanelNum, OffSet_0, TRUE);
//				InspResult.m_bGrabEnd = TRUE;
//				break;
//			}
//		}
//	}
//}

// 旧的 Vision Socket 解析函数已废弃（现使用 ICW）
//void CVisionThread::ParsingInspectionResult(int Num, CString strContents)
//{
//	theApp.m_bVisionDeleteFlag = FALSE;
//	CString sendMsg, strPanelID, strFpcID;
//	CStringArray responseTokens;
//	CStringSupport::GetTokenArray(strContents, _T(','), responseTokens);
//
//	strPanelID = responseTokens[0];
//	strFpcID = responseTokens[1];
//	strPanelID.Trim();
//	strFpcID.Trim();
//
//	//// 旧的 MC_INSPECTION_RESULT_RECEIVE 发送 (已废弃，现使用 ICW)
//	//sendMsg.Format(_T("%d,%s,%s"), MC_INSPECTION_RESULT_RECEIVE, strPanelID, strFpcID);
//	//SocketSendto(Num, sendMsg, MC_INSPECTION_RESULT_RECEIVE);
//
//	int iokng = 0;
//	for (auto &InspResult : theApp.m_lastInspResultVec)
//	{
//		if (!InspResult.m_cellId.CompareNoCase(strPanelID) || !InspResult.m_FpcID.CompareNoCase(strFpcID))
//		{
//			if (InspResult.m_bGrabEnd == TRUE)
//			{
//				// ... 解析结果并调用 VisionPLCResult ...
//				InspResult.time_check.StopTimer();
//				InspResult.m_bResult = TRUE;
//			}
//			break;
//		}
//	}
//	theApp.m_bVisionDeleteFlag = TRUE;
//}

void CVisionThread::OnEvent(UINT uEvent, LPVOID lpvData)
{
	if (theApp.m_bExitFlag == FALSE)
		return;

	// 旧的 Vision PC Socket 通信已废弃（现使用 ICW 6501端口统一通信）
	//SockAddrIn addrin;
	//GetSockName(addrin);
	//int VisionNum = ntohs(addrin.GetPort()) == _ttoi(VISION_PC1_PORT_NUM) ? PC1 : PC2;
	
	//switch (uEvent)
	//{
	//case EVT_CONDROP:
	//	LogWrite(CStringSupport::FormatString(_T("Vision Connect Drop %d Ch"), VisionNum), VisionNum);
	//	break;
	//case EVT_CONSUCCESS:
	//	LogWrite(CStringSupport::FormatString(_T("Vision Connect Success %d Ch"), VisionNum), VisionNum);
	//	break;
	//case EVT_ZEROLENGTH:
	//	LogWrite(CStringSupport::FormatString(_T("Vision EVT_ZEROLENGTH %d Ch"), VisionNum), VisionNum);
	//	break;
	//case EVT_CONFAILURE:
	//	LogWrite(CStringSupport::FormatString(_T("Vision EVT_CONFAILURE %d Ch"), VisionNum), VisionNum);
	//	break;
	//default:
	//	LogWrite(CStringSupport::FormatString(_T("Vision Unknown Socket event %d Ch"), VisionNum), VisionNum);
	//	break;
	//}
}

BOOL CVisionThread::getConectCheck()
{
	SockAddrIn addrin;
	GetSockName(addrin);
	LONG  uAddr = addrin.GetIPAddr();
	if (uAddr == 0)
		return FALSE;	//???????
	else
		return TRUE;	//??????
}

void CVisionThread::RemoveClient()
{
	ShutdownConnection((SOCKET)m_hComm);
}

bool CVisionThread::SocketServerOpen(CString strServerPort)
{
	m_bMelsecSimulaion = true;
	SetSmartAddressing(false);
	SetServerState(true);
	bool ret = CreateSocket(strServerPort, AF_INET, SOCK_STREAM, 0);
	if (ret) return WatchComm();
	else return false;
}

UINT CVisionThread::VisionThreadProc(LPVOID pParam)
{
	CVisionThread* pThis = reinterpret_cast<CVisionThread*>(pParam);
	_ASSERTE(pThis != NULL);
	pThis->ThreadRun();
	return 1L;

}

BOOL CVisionThread::CreateTask(){
	BOOL bRet = TRUE;
	m_pThreadVision = ::AfxBeginThread(VisionThreadProc, this, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
	if (!m_pThreadVision)
		bRet = FALSE;
	m_pThreadVision->m_bAutoDelete = FALSE;
	m_pThreadVision->ResumeThread();
	return bRet;
}

void CVisionThread::CloseTask()
{
	if (m_pThreadVision != NULL)
	{
		SetEvent(m_hQuit);
		Delay(100, TRUE);
		if (::WaitForSingleObject(m_pThreadVision->m_hThread, 1000) == WAIT_TIMEOUT)
		{
			SetEvent(m_hQuit);
			Delay(100, TRUE);
			if (::WaitForSingleObject(m_pThreadVision->m_hThread, 1000) == WAIT_TIMEOUT) {
				::TerminateThread(m_pThreadVision->m_hThread, 1L);
				theApp.m_VisionLog->Info(_T("Terminate Vision Thread"));
			}
		}
		delete m_pThreadVision;
		m_pThreadVision = NULL;
	}
	if (m_hQuit)
	{
		CloseHandle(m_hQuit);
		m_hQuit = NULL;
	}
}

// 旧的 Vision PC Socket 发送函数已废弃（现使用 ICW）
//void CVisionThread::SocketSendto(int Num, CString strContents, int iCommand)
//{
//	if (theApp.m_bExitFlag == FALSE)
//		return;
//
//	m_csSocketSend.Lock();
//	CString strCommand = CStringSupport::FormatString(_T("%c%s,%c"), _STX, strContents, _ETX);
//	char *lpCommand = StringToChar(strCommand);
//	theApp.m_VisionSocketManager[Num].WriteComm((BYTE*)lpCommand, strlen(lpCommand), 100L);
//	delete lpCommand;
//
//	m_lastContent[Num] = strContents;
//
//	//if (Num == PC1)
//	//	theApp.m_pVisionSendReceiver1Log->Info(CStringSupport::FormatString(_T("[MC -> VS] [Command : %s] ->%s"), MC_PacketNameTable[iCommand], strContents));
//	//else
//	//	theApp.m_pVisionSendReceiver2Log->Info(CStringSupport::FormatString(_T("[MC -> VS] [Command : %s] ->%s"), MC_PacketNameTable[iCommand], strContents));
//
//	m_csSocketSend.Unlock();
//}

void CVisionThread::LogWrite(CString strContents, int Num)
{
	if (theApp.m_bExitFlag == FALSE)
		return;

	// 先写入日志文件（线程安全）
	theApp.m_VisionLog->Info(strContents);

	// 通过 PostMessage 发送到 UI 线程更新 ListBox（避免跨线程操作 MFC 控件）
	// wParam: ListBox 索引, lParam: 字符串指针
	CString* pStrLog = new CString(strContents);
	if (g_DlgMainView && g_DlgMainView->m_hWnd)
	{
		g_DlgMainView->PostMessage(WM_VISION_LOG, (WPARAM)Num, (LPARAM)pStrLog);
	}
	else
	{
		delete pStrLog;  // 窗口未就绪时释放内存
	}
}

void CVisionThread::VisionPLCResult(int Num, int iPanelNum, CString ResultMsg, int ResultCode, CString strPanelID, int iSendNGBuffer)
{
	LogWrite(CStringSupport::FormatString(_T("[VisionPLCResult] Panel=%d, ResultCode=%d (%s), NGBuffer=%d, PanelID=%s"),
		iPanelNum, ResultCode, ResultMsg, iSendNGBuffer, strPanelID), 0);
	if (theApp.m_AOIPassMode){
		ResultCode = m_codeOk;
		//int Rand = 0;
		////srand(time(NULL));
		//Rand = rand() % 10;
		//if (Rand > 1)
		//	ResultCode = m_codeOk;
		//else
		//	ResultCode = m_codeFail;
	}
		

	LogWrite(CStringSupport::FormatString(_T("[VisionPLCResult] Set PLC VisionResult1+%d=%d, SendNGBuffer=%d"),
		iPanelNum, ResultCode, iSendNGBuffer), 0);
	theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_SendNgBufferResult1 + iPanelNum, &iSendNGBuffer);
	theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_VisionResult1 + iPanelNum, &ResultCode);
	LogWrite(CStringSupport::FormatString(_T("[VisionPLCResult] Set VisionGrabEnd1+%d = TRUE, VisionEnd1+%d = TRUE"), iPanelNum, iPanelNum), 0);
	theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionGrabEnd1 + iPanelNum, OffSet_0, TRUE);
	theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionEnd1 + iPanelNum, OffSet_0, TRUE);

	// 通知 ICW (FN$...@)
	LogWrite(CStringSupport::FormatString(_T("[VisionPLCResult] Send ICW Finish Info: Position=%d, Result=%s"),
		iPanelNum + 1, (ResultCode == m_codeOk) ? _T("OK") : _T("NG")), 0);
	ICW_FinishInfo finishInfo;
	finishInfo.Action = _T("Finish");
	ICW_ProductResult prodResult;
	prodResult.Position = iPanelNum + 1;  // 1-based
	prodResult.Result = (ResultCode == m_codeOk) ? ICW_RESULT_OK : ICW_RESULT_NG;
	finishInfo.ProducResults.push_back(prodResult);
	theApp.m_ICWCommManager.SendFinishInfoAuto(finishInfo);

	LogWrite(CStringSupport::FormatString(_T("Panel %d %s Vision Result %s"), Num, strPanelID, ResultMsg), Num);

	// Release slot: set m_bResult=TRUE so the slot can be reused
	for (auto &InspResult : theApp.m_lastInspResultVec)
	{
		if (!InspResult.m_cellId.CompareNoCase(strPanelID))
		{
			InspResult.m_bResult = TRUE;
			InspResult.time_check.StopTimer();
			InspResult.m_bInspStart = FALSE;
			LogWrite(CStringSupport::FormatString(_T("[VisionPLCResult] Slot released: PanelID=%s, Jig=%d"),
				strPanelID, iPanelNum), 0);
			break;
		}
	}
}

BOOL CVisionThread::VisionVecAdd(CString strPanel, CString strFpcID, int iPanelNum, int iIndexNum, int iPCNo, int iCurIndex, const CString& strUniqueID)
{
	BOOL flag = TRUE;
	InspResult panelData;
	panelData.Reset();

	panelData.m_bInspStart = TRUE;
	int iTimeoutMs = (theApp.m_iTimer[VisionGrabTimer] == 0) ? 40000 : theApp.m_iTimer[VisionGrabTimer] * 1000;
	panelData.time_check.SetCheckTime(iTimeoutMs);
	panelData.time_check.StartTimer();
	LogWrite(CStringSupport::FormatString(_T("[VisionVecAdd] Jig %d: Set timeout=%d ms (TimerCfg=%d)"),
		iPanelNum + 1, iTimeoutMs, theApp.m_iTimer[VisionGrabTimer]), 0);
	panelData.m_iIndexPanelNum = iIndexNum;
	panelData.m_iPanelNum = iPanelNum;
	panelData.m_iPCNum = iPCNo;
	panelData.m_cellId = strPanel;
	panelData.m_FpcID = strFpcID;
	panelData.m_UniqueID = strUniqueID;
	panelData.m_iCurIndex = iCurIndex;

	if (panelData.m_cellId.IsEmpty())
		panelData.m_cellId = panelData.m_FpcID;

	for (auto &InspResult : theApp.m_lastInspResultVec)
	{
		if (!InspResult.m_cellId.CompareNoCase(strPanel))
		{
			flag = FALSE;
			VisionPLCResult(iPCNo, iPanelNum, _T("PLC ID Error"), m_codePlcPanelError, strPanel);
			break;
		}
	}

	for (auto &InspResult : theApp.m_lastInspResultVec)
	{
		if (InspResult.m_bInspStart == FALSE && flag == TRUE)
		{
			InspResult = panelData;
			break;
		}

	}

	return flag;
}

void CVisionThread::AddJigToInspection(int panelNum)
{
	// 使用 VisionInspectionMethod 保存到成员变量的数据
	// 避免重复读取 PLC 和重复计算 Index

	// 添加到检测队列
	LogWrite(CStringSupport::FormatString(_T("%s Panel %d [%s][%s] Vision Grab Start"),
		PG_IndexName[m_iJigCurIndex], panelNum, m_strJigPanel, m_strJigFpcID), panelNum);

	BOOL bFlag = VisionVecAdd(m_strJigPanel, m_strJigFpcID, panelNum, m_iJigIndexPanelNum, panelNum, m_iJigCurIndex, m_strJigUniqueID);
	if (bFlag)
	{
		LogWrite(CStringSupport::FormatString(_T("[AddJigToInspection] Jig %d: VisionVecAdd OK, Panel=%s, FpcID=%s"),
			panelNum + 1, (LPCTSTR)m_strJigPanel, (LPCTSTR)m_strJigFpcID), panelNum);
	}
}

void CVisionThread::AutoFocusAxis(int Num, int iCommand, CString strContents)
{
	if (theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_AutoFocusReady1 + Num, OffSet_0))
	{
		CStringArray responseTokens;
		CStringSupport::GetTokenArray(strContents, _T(','), responseTokens);
		AutoFocusData pAutoFocusData;
		pAutoFocusData.m_MoterValue = _ttof(responseTokens[0]) * 10000;

		theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_AutoFocusMoter1 + Num, &iCommand);
		theApp.m_pEqIf->m_pMNetH->SetAutoFocusData(eWordType_AutoFocusValue1 + Num, &pAutoFocusData);
		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AutoFocusStart1 + Num, OffSet_0, TRUE);
	}
	else
	{
		LogWrite(CStringSupport::FormatString(_T("Auto Focus Ready Error!!!!!")), Num);
	}
}

void CVisionThread::AutoFocusSave(int Num)
{
	if (theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_AutoFocusReady1 + Num, OffSet_0))
	{
		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AutoFocusSave1 + Num, OffSet_0, TRUE);
		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AutoFocusStart1 + Num, OffSet_0, TRUE);
	}
	else
	{
		LogWrite(CStringSupport::FormatString(_T("Auto Focus Ready Error!!!!!")), Num);
	}
}

void CVisionThread::ParshingVisionData(int Num, CString strContents)
{
	CString strVisionModelName;
	CStringArray responseTokens;
	CStringSupport::GetTokenArray(strContents, _T(','), responseTokens);

	if (responseTokens.GetSize() < 3)
		return;

	strVisionModelName = responseTokens[0];
	theApp.m_strOpvImageWidth = responseTokens[1];
	theApp.m_strOpvImageHeight = responseTokens[2];

	LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] ModelName[%s], ImageWidth[%s], ImageHeight[%s]"),
		Num, strVisionModelName, theApp.m_strOpvImageWidth, theApp.m_strOpvImageHeight), Num);
}

// ICW 收到 Start$ 消息后的处理
// ICW 发送 Start$...@ 时，接收 {"Action":"Start",...} 格式的消息
void CVisionThread::OnICWStart(const ICW_StartInfo& startInfo)
{
	LogWrite(_T("[ICW Start$] ========== Start$ 接收处理开始 =========="), 0);
	LogWrite(CStringSupport::FormatString(_T("[ICW Start$] JigNumber: %d, Products count: %d"),
		startInfo.JigNumber, (int)startInfo.Products.size()), 0);

	// 遍历所有产品信息，触发对应治具的检测
	// JigNumber: 1=Panel1, 2=Panel2, 3=Panel3, 4=Panel4
	for (const auto& product : startInfo.Products)
	{
		int iPanelNum = product.Position - 1;  // 转为 0-based 索引
		LogWrite(CStringSupport::FormatString(_T("[ICW Start$] Product: Position=%d, UniqueId=%s, Barcode=%s"),
			product.Position, product.UniqueId, product.Barcode), 0);
		if (iPanelNum >= 0 && iPanelNum < 4)
		{
			// 4-Line 系统使用 ICW 统一通信，直接使用治具号
			LogWrite(CStringSupport::FormatString(_T("[ICW Start$] Trigger VisionInspectionMethod: Panel=%d"),
				iPanelNum), 0);
			VisionInspectionMethod(iPanelNum, iPanelNum);
		}
		else
		{
			LogWrite(CStringSupport::FormatString(_T("[ICW Start$] Invalid panel number: %d"), iPanelNum), 0);
		}
	}

	LogWrite(_T("[ICW Start$] ========== Start$ 接收处理完成 =========="), 0);
}

// ICW ??? SnapFN@ ??????????????
void CVisionThread::OnICWSnapFN()
{
	LogWrite(_T("[ICW] Received SnapFN@ - Image Grab Complete"), 0);

#if _SYSTEM_AMTAFT_
	// 【重要修复】不要在这里重置 m_bICWStartSent！
	// 否则会导致无限循环：SnapFN@ → 重置标志 → SendICWStartMessage() → SnapFN@ → 循环...
	// Start$ 发送标志应该在收到 FN$ 完整结果后才重置
	// 在 OnICWFinishFN() 的最后统一重置所有标志
	LogWrite(_T("[ICW] SnapFN@ received - keeping ICW Start$ flags (will reset after FN$)"), 0);
#endif

	// ????????????????????? GrabEnd ??
	for (auto& InspResult : theApp.m_lastInspResultVec)
	{
		if (InspResult.m_bInspStart == TRUE && InspResult.m_bGrabEnd == FALSE)
		{
			LogWrite(CStringSupport::FormatString(_T("Panel [%s] Vision Grab End (from ICW)"), InspResult.m_cellId), InspResult.m_iPCNum);
			theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionGrabEnd1 + InspResult.m_iPanelNum, OffSet_0, TRUE);
			InspResult.m_bGrabEnd = TRUE;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
// ICW 收到 FN$ 后的处理：点灯检完成检测 → 主检接收结果并回写 PLC
// 流程：FN$ → 解析治具号 → 查询 idmap → 查询检测结果 → 写PLC → 查询缺陷 → 写PLC缺陷码 → DFS上传
///////////////////////////////////////////////////////////////////////////////
void CVisionThread::OnICWFinishFN(const ICW_LegacyFinishInfo& finishInfo)
{
	theApp.m_bVisionDeleteFlag = FALSE;
	LogWrite(CStringSupport::FormatString(_T("[ICW] OnICWFinishFN: %d slots"), (int)finishInfo.Results.size()), 0);
	LogWrite(_T("[ICW FN$] ========== FN$ 处理开始 =========="), 0);
	LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Received %d slot results"), (int)finishInfo.Results.size()), 0);

	// FN$ 每 2 位一个工位，results[0] 对应治具 1，results[1] 对应治具 2，...
	for (size_t i = 0; i < finishInfo.Results.size(); i++)
	{
		int nResult = finishInfo.Results[i];
		int nFixtureNo = (int)i + 1;  // 治具号 1~4

		// 跳过无效结果（0 表示该槽无产品）
		if (nResult == 0)
		{
			LogWrite(CStringSupport::FormatString(_T("[ICW] Slot %d: no product (result=0), skip"), nFixtureNo), 0);
			LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: No product (result=0), skip"), nFixtureNo), nFixtureNo - 1);
			continue;
		}

		LogWrite(CStringSupport::FormatString(_T("[ICW] Slot %d: result=%d"), nFixtureNo, nResult), 0);
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] ========== Process Fixture %d =========="), nFixtureNo), nFixtureNo - 1);
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: Light inspection result=%d"), nFixtureNo, nResult), nFixtureNo - 1);

		// Step 1: 查询 ivs_lcd_idmap（MainAoiFixID → UniqueID/Barcode）
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: Step1 - Query ivs_lcd_idmap (MainAoiFixID=%d)"), nFixtureNo, nFixtureNo), nFixtureNo - 1);
		CIDMapInfo idMapInfo;
		if (!GetDBInterface().QueryIDMapByFixtureNo(nFixtureNo, idMapInfo))
		{
			LogWrite(CStringSupport::FormatString(
				_T("[ICW] QueryIDMapByFixtureNo failed for fixture %d: %s"),
				nFixtureNo, (LPCTSTR)GetDBInterface().GetLastError()), 0);
			LogWrite(CStringSupport::FormatString(
				_T("[ICW FN$] Fixture %d: Step1 failed - %s"),
				nFixtureNo, (LPCTSTR)GetDBInterface().GetLastError()), nFixtureNo - 1);
			continue;
		}

		CString strUniqueID = idMapInfo.UniqueID;
		CString strBarcode = idMapInfo.ScreenID;  // ivs_lcd_idmap.Barcode
		LogWrite(CStringSupport::FormatString(_T("[ICW] Fixture %d: UniqueID=%s, Barcode=%s"),
			nFixtureNo, (LPCTSTR)strUniqueID, (LPCTSTR)strBarcode), 0);
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: Step1 completed - UniqueID=%s, Barcode=%s"),
			nFixtureNo, (LPCTSTR)strUniqueID, (LPCTSTR)strBarcode), nFixtureNo - 1);

		// Step 2: 查询 IVS_LCD_InspectionResult（UniqueID → AOIResult/Code/Grade）
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: Step2 - Query IVS_LCD_InspectionResult"), nFixtureNo), nFixtureNo - 1);
		CInspectionResult inspResult;
		if (!GetDBInterface().QueryByUniqueID(strUniqueID, inspResult))
		{
			LogWrite(CStringSupport::FormatString(_T("[ICW] QueryByUniqueID failed for %s: %s"),
				(LPCTSTR)strUniqueID, (LPCTSTR)GetDBInterface().GetLastError()), 0);
			LogWrite(CStringSupport::FormatString(
				_T("[ICW FN$] Fixture %d: Step2 failed/no record - %s"),
				nFixtureNo, (LPCTSTR)GetDBInterface().GetLastError()), nFixtureNo - 1);
			// 即使查不到也继续写 PLC（使用 FN$ 的结果）
		}
		else
		{
			LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: Step2 completed - InspectionResult SysID=%d"),
				nFixtureNo, inspResult.SysID), nFixtureNo - 1);
		}

		// Step 3: 确定 PLC 检测结果（OK/NG）
		// 优先使用数据库 IVS_LCD_InspectionResult.AOIResult 覆盖 FN$ 结果
		int nPlcResult;
		if (!strUniqueID.IsEmpty() && inspResult.SysID > 0 && !inspResult.AOIResult.IsEmpty())
		{
			if (inspResult.AOIResult.CompareNoCase(_T("OK")) == 0)
				nPlcResult = m_codeOk;
			else
				nPlcResult = m_codeFail;

			LogWrite(CStringSupport::FormatString(_T("[ICW] Fixture %d: 使用数据库检测结果 - AOIResult=%s -> nPlcResult=%d"),
				nFixtureNo, (LPCTSTR)inspResult.AOIResult, nPlcResult), 0);
			LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: Step3 completed - AOIResult=%s -> nPlcResult=%d (%s)"),
				nFixtureNo, (LPCTSTR)inspResult.AOIResult, nPlcResult, nPlcResult == m_codeOk ? _T("OK") : _T("NG")), nFixtureNo - 1);
		}
		else
		{
			nPlcResult = (nResult == 1) ? m_codeOk : m_codeFail;
			LogWrite(CStringSupport::FormatString(_T("[ICW] Fixture %d: No DB record, use FN$ result=%d -> nPlcResult=%d"),
				nFixtureNo, nResult, nPlcResult), 0);
		}

		// 写入 AOI csv 文件（格式兼容旧版 Vision PC，供 DFS 后续读取汇总）
		// 策略：缺陷坐标(X/Y/Size) → 从数据库 QueryDefectsByParentGUID 查询
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: Step3 - Query defect coordinates - inspResult.GUID=[%s]"),
			nFixtureNo, (LPCTSTR)inspResult.GUID), nFixtureNo - 1);
		CDefectInfoList defectList;
		if (!inspResult.GUID.IsEmpty())
		{
			LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: Call QueryDefectsByParentGUID(GUID=%s)"),
				nFixtureNo, (LPCTSTR)inspResult.GUID), nFixtureNo - 1);
			if (!GetDBInterface().QueryDefectsByParentGUID(inspResult.GUID, defectList))
			{
				LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: QueryDefectsByParentGUID failed - %s"),
					nFixtureNo, (LPCTSTR)GetDBInterface().GetLastError()), nFixtureNo - 1);
			}
			else
			{
			LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: QueryDefectsByParentGUID completed, found %d defects"),
				nFixtureNo, (int)defectList.size()), nFixtureNo - 1);
			}
		}
		else
		{
			LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: inspResult.GUID is empty, skip defect query"),
				nFixtureNo), nFixtureNo - 1);
		}
		CDFSInfo dfsInfo;
		if (dfsInfo.WriteAOICSVFile(inspResult, defectList, nFixtureNo, strBarcode))
		{
			theApp.m_pTestLog->Info(_T("[ICW FN$] Fixture %d: WriteAOICSVFile success (DefectCount=%d)"), nFixtureNo, (int)defectList.size());
		}
		else
		{
			theApp.m_pTestLog->Info(_T("[ICW FN$] Fixture %d: WriteAOICSVFile failed"), nFixtureNo);
		}

		// AOI NG 时加入 RankCode 列表（供等级码管理使用）
		// 注意：第二个参数传 strBarcode（真实 FpcID），与 WriteAOICSVFile 写入路径保持一致
		if (nPlcResult == m_codeFail)
		{
			theApp.m_pRankTread->AddRankCodeList(strUniqueID, strBarcode, nFixtureNo - 1, nFixtureNo - 1, RankAOI);
			theApp.m_pTestLog->Info(_T("[ICW FN$] Fixture %d: AddRankCodeList %s NG (FpcID=%s)"), nFixtureNo, (LPCTSTR)strUniqueID, (LPCTSTR)strBarcode);
		}
		else
		{
			theApp.m_pTestLog->Info(_T("[ICW FN$] Fixture %d: AddRankCodeList %s OK"), nFixtureNo, (LPCTSTR)strUniqueID);
		}

		// Step 4: 从 IVS_LCD_InspectionResult 读取缺陷码和等级（已在 Step2 查询得到）
		// 无需再查 ivs_lcd_aoidefect，直接使用 inspResult 中的字段
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: Step4 - Read defect code from InspectionResult"),
			nFixtureNo), nFixtureNo - 1);
		CString strDefectCode = inspResult.Code_AOI;
		CString strGrade = inspResult.Grade_AOI;

		if (!strDefectCode.IsEmpty())
		{
			LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: Step4 completed - DefectCode=%s, Grade=%s"),
				nFixtureNo, (LPCTSTR)strDefectCode, (LPCTSTR)strGrade), nFixtureNo - 1);
		}
		else
		{
			LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: Step4 completed - No defect code or grade"), nFixtureNo), nFixtureNo - 1);
		}

		// Step 5: 写入 SendNgBufferResult（用于产品流向分类）
		// iSendNGBuffer 值含义：
		// 1 = OK Buffer（检测合格）
		// 2 = NG Buffer 1（等级0的缺陷：A/R1）
		// 3 = NG Buffer 2（等级1的缺陷：B/R2）
		// 4 = NG Buffer 3（等级2的缺陷：C/R3）
		// 5 = NG Buffer 4（等级3的缺陷：R4）
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: Step5 - Write SendNgBufferResult"), nFixtureNo), nFixtureNo - 1);
		int nSendNGBuffer = 0;
		if (nPlcResult == m_codeOk)
		{
			nSendNGBuffer = 1;  // OK Buffer
		}
		else if (!strGrade.IsEmpty())
		{
			// 缺陷等级转换为数值：Grade A=0最优, B=1, C=2, R1=0, R2=1, R3=2, R4=3...
			int nNGGrade = 0;
			if (strGrade.GetLength() >= 2 && strGrade[0] == _T('R'))
			{
				nNGGrade = _ttoi(strGrade.Mid(1)) - 1;  // R1=0, R2=1, R3=2...
				if (nNGGrade < 0) nNGGrade = 0;
				if (nNGGrade > 3) nNGGrade = 3;
			}
			else if (strGrade.GetLength() >= 1)
			{
				if (strGrade[0] >= _T('A') && strGrade[0] <= _T('C'))
					nNGGrade = strGrade[0] - _T('A');
				else
					nNGGrade = 0;
			}
			nSendNGBuffer = nNGGrade + 2;
		}
		else
		{
			nSendNGBuffer = 2;  // 无等级信息，默认 NG Buffer 1
		}
		theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_SendNgBufferResult1 + (nFixtureNo - 1), &nSendNGBuffer);
		LogWrite(CStringSupport::FormatString(_T("[ICW] Set PLC SendNgBufferResult%d = %d (Grade=%s -> Buffer=%d)"),
			nFixtureNo, nSendNGBuffer, (LPCTSTR)strGrade, nSendNGBuffer), 0);
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: Step5 completed - SendNgBufferResult=%d"),
			nFixtureNo, nSendNGBuffer), nFixtureNo - 1);

		// Step 6: 写入 VisionResult（PLC 主结果）
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: Step6 - Write VisionResult"), nFixtureNo), nFixtureNo - 1);
		int nVisionResult = nPlcResult;
		//if (!strDefectCode.IsEmpty())
		//{
		//	nVisionResult = m_codeFail;
		//}
		theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_VisionResult1 + (nFixtureNo - 1), &nVisionResult);
		LogWrite(CStringSupport::FormatString(_T("[ICW] Set PLC VisionResult%d = %d (DefectCode=%s, Grade=%s)"),
			nFixtureNo, nVisionResult, (LPCTSTR)strDefectCode, (LPCTSTR)strGrade), 0);
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: Step6 completed - VisionResult=%d, DefectCode=%s, Grade=%s"),
			nFixtureNo, nVisionResult, (LPCTSTR)strDefectCode, (LPCTSTR)strGrade), nFixtureNo - 1);

		// Step 7: 设置 VisionEnd 信号（点灯检完成标志，通知 PLC 取走结果）
		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionEnd1 + (nFixtureNo - 1), OffSet_0, TRUE);
		LogWrite(CStringSupport::FormatString(_T("[ICW] Set PLC VisionEnd%d = TRUE"), nFixtureNo), 0);
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: Step7 completed - VisionEnd=TRUE"), nFixtureNo), nFixtureNo - 1);

		// 写入缺陷码（如果有的话，存储到 PLC 对应区域）
		// 参考 PlcThread 的 DefectCodeStart 逻辑：检测 Start bit 上升沿，写入数据后设置 End bit
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: Step10 - DefectCode handshake"), nFixtureNo), nFixtureNo - 1);

		// 读取 PLC DefectCodeStart bit（与 PlcThread 逻辑一致）
		//BOOL bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_DefectCodeStart1 + (nFixtureNo - 1), OffSet_0);
		//LogWrite(CStringSupport::FormatString(_T("[ICW FN$] 治具 %d: DefectCodeStart=%d, Previous=%d"),
		//	nFixtureNo, bStartFlag, m_bDefectCodeStart[nFixtureNo - 1]), nFixtureNo - 1);

		//// 如果 Start bit = FALSE，设置 End bit = FALSE
		//if (bStartFlag == FALSE)
		//{
		//	theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_DefectCodeEnd1 + (nFixtureNo - 1), OffSet_0, FALSE);
		//}

		//// 检测 Start 信号的上升沿（从 FALSE → TRUE）
		//if (m_bDefectCodeStart[nFixtureNo - 1] == !bStartFlag)
		//{
		//	m_bDefectCodeStart[nFixtureNo - 1] = bStartFlag;

		//	if (bStartFlag == TRUE)
		//	{
		//		// 上升沿：PLC 请求读取 DefectCode，清除 End bit
		//		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_DefectCodeEnd1 + (nFixtureNo - 1), OffSet_0, FALSE);

		//		// 写入 DefectCode 和 Grade 到 PLC
		//		if (!strDefectCode.IsEmpty())
		//		{
		//			DefectCodeRank pDefectCodeRank;
		//			DefectGradeRank pDefectGradeRank;
		//			CStringSupport::ToAString(strDefectCode, pDefectCodeRank.m_DefectCode, sizeof(pDefectCodeRank.m_DefectCode));
		//			CStringSupport::ToAString(strGrade, pDefectGradeRank.m_DefectGrade, sizeof(pDefectGradeRank.m_DefectGrade));
		//			theApp.m_pEqIf->m_pMNetH->SetDefectRankData(eWordType_DefectCodeResult1 + (nFixtureNo - 1), &pDefectCodeRank);
		//			theApp.m_pEqIf->m_pMNetH->SetDefectGradeRankData(eWordType_DefectGradeResult1 + (nFixtureNo - 1), &pDefectGradeRank);
		//			LogWrite(CStringSupport::FormatString(_T("[ICW] Set PLC DefectCodeResult%d=%s, DefectGradeResult%d=%s"),
		//				nFixtureNo, (LPCTSTR)strDefectCode, nFixtureNo, (LPCTSTR)strGrade), 0);
		//		}

		//		// 设置 DefectCodeEnd bit = TRUE（表示数据已写入）
		//		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_DefectCodeEnd1 + (nFixtureNo - 1), OffSet_0, TRUE);
		//		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] 治具 %d: Set DefectCodeEnd=TRUE"), nFixtureNo), nFixtureNo - 1);
		//	}
		//}

		if (!strDefectCode.IsEmpty())
		{
			LogWrite(CStringSupport::FormatString(_T("[ICW] Defect for fixture %d: Code=%s, Grade=%s"),
				nFixtureNo, (LPCTSTR)strDefectCode, (LPCTSTR)strGrade), 0);
		}

		// Step 9: DFS 数据上传（点灯/Lumitop 设备）
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: Step9 - DFS data upload"), nFixtureNo), nFixtureNo - 1);
		// 根据文档：触发时机为 FN$ 处理完成时同时触发 PLC 写入和 DFS 上传
		if (!strBarcode.IsEmpty())
		{
			DfsDataValue dfsData;
			dfsData.Reset();
			dfsData.m_TypeNum = 4;  // 设备类型：4=点灯/Lumitop 设备
			dfsData.m_StageNum = nFixtureNo;  // 工位号 1~4
			dfsData.m_FpcID = strBarcode;  // 玻璃条码
			dfsData.m_PanelID = strBarcode;  // 玻璃条码（PanelID = FpcID）
			dfsData.m_IndexNum.Format(_T("%02d"), nFixtureNo);  // 治具号：01~04
			dfsData.m_ChNum.Format(_T("%d"), nFixtureNo - 1);  // 通道号：0~3

			// 点灯结果：OK/NG（根据 VisionResult 判定）
			CString strLumitopResult = (nVisionResult == m_codeOk) ? _T("OK") : _T("NG");
			dfsData.m_Lumitop = strLumitopResult;

			// 时间戳
			dfsData.m_StartTime = GetNowSystemTimeMilliseconds();
			dfsData.m_EndTime = GetNowSystemTimeMilliseconds();

			// 上传到 DFS（异步队列处理）
			theApp.m_pFTP->AddTransferFile(dfsData);
			LogWrite(CStringSupport::FormatString(_T("[ICW] DFS upload queued: PanelID=%s, Lumitop=%s, Fixture=%d"),
				(LPCTSTR)strBarcode, (LPCTSTR)strLumitopResult, nFixtureNo), 0);
			LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: Step6 completed - DFS upload: PanelID=%s, Lumitop=%s"),
				nFixtureNo, (LPCTSTR)strBarcode, (LPCTSTR)strLumitopResult), nFixtureNo - 1);
		}
		else
		{
			LogWrite(CStringSupport::FormatString(_T("[ICW FN$] Fixture %d: Step6 skipped - Barcode is empty"), nFixtureNo), nFixtureNo - 1);
		}

		// 非 PassMode 且 NG 时通知 OPV 两个通道（供 OPV 界面显示 NG 状态）
		if (!theApp.m_AOIPassMode)
		{
			if (nPlcResult == m_codeFail)
			{
				CString strMsg;
				strMsg = CStringSupport::FormatString(_T("%d,%d"), MC_NG_PANEL, _AOI);
				theApp.m_OpvSocketManager[0].SendOpvMessage(strMsg, 0, MC_NG_PANEL);
				theApp.m_OpvSocketManager[1].SendOpvMessage(strMsg, 1, MC_NG_PANEL);
				theApp.m_pTestLog->Info(_T("[ICW FN$] Fixture %d: Send OPV NG Panel=%s"), nFixtureNo, (LPCTSTR)strUniqueID);
			}
		}

		LogWrite(CStringSupport::FormatString(
			_T("[ICW FN$] Fixture %d: ========== Process completed =========="), nFixtureNo), nFixtureNo - 1);

		// 【Bug1 修复】停止该槽位的超时计时器，并设置 m_bResult=TRUE
		// 防止 30 秒超时定时器在 FN$ 正常完成后仍然触发 VisionPLCResult(TimeOut)
		for (auto& insp : theApp.m_lastInspResultVec)
		{
			if (insp.m_bInspStart == TRUE && insp.m_iPCNum == nFixtureNo - 1)
			{
				insp.time_check.StopTimer();
				insp.m_bResult = TRUE;
				LogWrite(CStringSupport::FormatString(_T("[ICW FN$] 治具 %d: Stop timer, m_bResult=TRUE (prevent timeout override)"), nFixtureNo), 0);
				break;
			}
		}
	}

	LogWrite(_T("[ICW FN$] ========== FN$ 处理完成 =========="), 0);

#if _SYSTEM_AMTAFT_
	// 【重要修复】在 FN$ 处理完成后，重置 ICW Start$ 发送标志
	// 这样下次 PLC 发出 VisionPlcSend 信号时，才允许发送新的 Start$
	// 注意：只有非 AUTO_TEST 模式下才重置标志，AUTO_TEST 模式下只发送一次 Start$
	if (theApp.m_iAutoTestMode != 1)
	{
		for (int i = 0; i < 4; i++)
			m_bICWStartSent[i] = FALSE;
		LogWrite(_T("[ICW FN$] All ICW Start$ flags reset - ready for next cycle"), 0);
	}
	else
	{
		LogWrite(_T("[ICW FN$] AUTO_TEST mode - Keep ICW Start$ flags (no more sends in test mode)"), 0);
	}
#endif

	theApp.m_bVisionDeleteFlag = TRUE;  // 允许 Slot 回收复用
}

#endif