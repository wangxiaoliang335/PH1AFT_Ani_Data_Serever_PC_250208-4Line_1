
#include "stdafx.h"

#if _SYSTEM_AMTAFT_

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

	while (::WaitForSingleObject(m_hQuit, 50) != WAIT_OBJECT_0)
	{
		//// 旧的 Vision PC1/PC2 连接状态检查 (已废弃，现使用 ICW 6501端口)
		//theApp.m_VisionConectStatus[0] = theApp.m_VisionSocketManager[0].getConectCheck();
		//theApp.m_VisionConectStatus[1] = theApp.m_VisionSocketManager[1].getConectCheck();

		// 6501端口（ICW）连接状态
		BOOL bICWConnected = theApp.m_ICWCommManager.IsConnected();

		LogWrite(CStringSupport::FormatString(_T("[Vision] ICW Connected=%d, AOIPassMode=%d"),
			bICWConnected, theApp.m_AOIPassMode), 0);

		//if (theApp.m_bAllPassMode)
		//	continue;

		if (bICWConnected || theApp.m_AOIPassMode)
		{
			if (m_bFirstStatus)
			{
				LogWrite(_T("[Vision] First Status - Initializing"), 0);
				m_bFirstStatus = FALSE;
				time_check.SetCheckTime(60000);
				time_check.StartTimer();
				for (int ii = 0; ii < PCMaxCount; ii++)
				{
					VisionFirstCheckMethod(ii);
					theApp.m_VisionPCStatus[ii] = TRUE;
				}

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

			if (theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_VisionPlcSend, 0))
			{
				LogWrite(_T("[Vision] GetPLC eBitType_VisionPlcSend = TRUE"), 0);
				VisionPanelCheck();
			}
			else
			{
				LogWrite(_T("[Vision] GetPLC eBitType_VisionPlcSend = FALSE, Set eBitType_VisionPcReceiver = FALSE"), 0);
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionPcReceiver, 0, FALSE);
			}

			for (int ii = 0; ii < PanelMaxCount; ii++)
			{
				m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_VisionStart1, OffSet_0 + ii);

				if (m_bStartFlag == FALSE)
				{
					LogWrite(CStringSupport::FormatString(_T("[Vision] Panel %d: StartFlag=FALSE, Reset VisionResult=%d, GrabEnd=FALSE, End=FALSE"),
						ii + 1, m_codeReset), 0);
					theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_VisionResult1 + ii, &m_codeReset);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionGrabEnd1 + ii, OffSet_0, FALSE);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionEnd1 + ii, OffSet_0, FALSE);
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
						//Delay(100);
						m_iPcNum = ii <= PanelNum2 ? PC1 : PC2;
						LogWrite(CStringSupport::FormatString(_T("[Vision] Calling VisionInspectionMethod PC=%d, PanelNum=%d"), m_iPcNum, PanelNum1 + ii), 0);
						VisionInspectionMethod(m_iPcNum, PanelNum1 + ii);
					}
				}
			}

			for (int ii = 0; ii < MaxCamCount; ii++)
			{
				m_bAutoFocusStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_AutoFocusEnd1 + ii, OffSet_0);

				if (m_bAutoFocusStartFlag == TRUE)
				{
					LogWrite(CStringSupport::FormatString(_T("[AutoFocus] Cam %d: GetAutoFocusData, Set AutoFocusSave=FALSE, AutoFocusStart=FALSE"), ii + 1), 0);
					theApp.m_pEqIf->m_pMNetH->SetAutoFocusData(eWordType_AutoFocusMoter1, &pAutoFocusData);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AutoFocusSave1 + ii, OffSet_0, FALSE);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AutoFocusStart1 + ii, OffSet_0, FALSE);
				}

				if (m_bAutoFocusStart[ii] == !m_bAutoFocusStartFlag)
				{
					m_bAutoFocusStart[ii] = m_bAutoFocusStartFlag;
					LogWrite(CStringSupport::FormatString(_T("[AutoFocus] Cam %d End Flag [%s]"), ii + 1, m_bAutoFocusStart[ii] == FALSE ? _T("FALSE") : _T("TRUE")), 0);

					if (m_bAutoFocusStartFlag == TRUE)
					{
						if (theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_AutoFocusSave1 + ii, OffSet_0))
						{
							//save
							LogWrite(CStringSupport::FormatString(_T("[AutoFocus] Cam %d: Save Mode, SocketSend GOOD, MC_FOCUS_SAVE_POS_DONE"), ii + 1), 0);
							theApp.m_VisionSocketManager->SocketSendto(ii, _T("GOOD"), MC_FOCUS_SAVE_POS_DONE);
							theApp.m_pEqIf->m_pMNetH->SetAutoFocusData(eWordType_AutoFocusMoter1, &pAutoFocusData);
							theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AutoFocusSave1 + ii, OffSet_0, FALSE);
							theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AutoFocusStart1 + ii, OffSet_0, FALSE);

							LogWrite(CStringSupport::FormatString(_T("[CAM_%d] Auto Focus Save Success"), ii + 1), ii);
						}
						else
						{
							//axis Move
							theApp.m_pEqIf->m_pMNetH->GetPlcWordData(eWordType_AutoFocusMoter1, &m_AutoFocusPosition);
							if (m_AutoFocusPosition == 1)
							{
								LogWrite(CStringSupport::FormatString(_T("[AutoFocus] Cam %d: Axis Move Mode, Position=%d, SocketSend GOOD, MC_Z_MOVE_DONE"), ii + 1, m_AutoFocusPosition), 0);
								theApp.m_VisionSocketManager->SocketSendto(ii, _T("GOOD"), MC_Z_MOVE_DONE);
							}
							else
							{
								LogWrite(CStringSupport::FormatString(_T("[AutoFocus] Cam %d: Axis Move Mode, Position=%d, SocketSend GOOD, MC_FOCUS_MOVE_DONE"), ii + 1, m_AutoFocusPosition), 0);
								theApp.m_VisionSocketManager->SocketSendto(ii, _T("GOOD"), MC_FOCUS_MOVE_DONE);
							}

							theApp.m_pEqIf->m_pMNetH->SetAutoFocusData(eWordType_AutoFocusMoter1, &pAutoFocusData);
							theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AutoFocusSave1 + ii, OffSet_0, FALSE);
							theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AutoFocusStart1 + ii, OffSet_0, FALSE);

							LogWrite(CStringSupport::FormatString(_T("[CAM_%d] Auto Focus Axis Move Success"), ii + 1), ii);
						}
					}
				}
			}
			
			for (auto &InspResult : theApp.m_lastInspResultVec)
			{
				if (InspResult.m_LastCheck == TRUE)
				{
					if (InspResult.time_check.IsTimeOver())
					{
						LogWrite(CStringSupport::FormatString(_T("[Vision] PC=%d Panel=%d Timeout! Call VisionPLCResult with Code=%d"),
							InspResult.m_iPCNum, InspResult.m_iPanelNum, m_codeTimeOut), 0);
						VisionPLCResult(InspResult.m_iPCNum,
							InspResult.m_iPanelNum,
							PLC_ResultValue[m_codeTimeOut],
							m_codeTimeOut,
							InspResult.m_cellId);

						InspResult.m_bResult = TRUE;
						theApp.m_TimeOutLog->LOG_INFO(CStringSupport::FormatString(_T("[PC : %d] AOI [%s] Time out"), InspResult.m_iPCNum, InspResult.m_cellId));
					}
				}
				else if (InspResult.m_bInspStart == TRUE)
				{
					//// 旧的 Vision 超时发送 MC_INSPECTION_END (已废弃，现使用 ICW)
					//if (InspResult.time_check.IsTimeOver())
					//{
					//	LogWrite(CStringSupport::FormatString(_T("[Vision] PC=%d Panel=%d Last Grab Timeout! SocketSend MC_INSPECTION_END"),
					//		InspResult.m_iPCNum, InspResult.m_iPanelNum), 0);
					//	CString sendMsg;
					//	sendMsg.Format(_T("%d,%s"), MC_INSPECTION_END, InspResult.m_cellId);
					//	SocketSendto(InspResult.m_iPCNum, sendMsg, MC_INSPECTION_END);
					//	LogWrite(CStringSupport::FormatString(_T("Vision %d Inspection Last Request Start")), InspResult.m_iPCNum);
					//	InspResult.m_LastCheck = TRUE;
					//	if (theApp.m_iTimer[VisionLastGrabTimer] == 0)
					//		InspResult.time_check.SetCheckTime(1000);
					//	else
					//		InspResult.time_check.SetCheckTime(theApp.m_iTimer[VisionLastGrabTimer] * 1000);

					//	InspResult.time_check.StartTimer();
					//}
				}

				if (InspResult.m_bResult == TRUE && theApp.m_bVisionDeleteFlag == TRUE)
					InspResult.Reset();

			}
			
		}
		else
		{
			LogWrite(_T("[Vision] ICW Disconnected, Reset VisionPCStatus and FirstStatus"), 0);
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

	SockAddrIn addrin;
	GetSockName(addrin);
	int Num = ntohs(addrin.GetPort()) == _ttoi(VISION_PC1_PORT_NUM) ? PC1 : PC2;

	//????? ???? ??? ?????????? for ?????? ETX ???????? ?????? ???? ??????? ????? ????
	CString strData, m_strHeader, m_strCommand, m_strContents, strParsing;
	int iFind, iFindSTX;
	MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<LPCSTR>(lpBuffer), dwCount, strData.GetBuffer(dwCount + 1), dwCount + 1);
	strData.ReleaseBuffer(dwCount);

	CStringArray responseTokens;
	CStringSupport::GetTokenArray(strData, _ETX, responseTokens);

	if (responseTokens.GetSize() == 1)
	{
		LogWrite(_T("ETX No Message!!!"), Num);
		return;
	}

	for (int ii = 0; ii < responseTokens.GetSize() - 1; ii++)
	{
		strParsing = responseTokens[ii];

		m_strHeader.Format(_T("%x"), strParsing.GetAt(0));

		UINT iHeader = (UINT)_ttoi(m_strHeader);

		if (iHeader != _STX)
		{
			LogWrite(_T("STX No Message!!!"), Num);
			return;
		}

		iFind = strParsing.Find(',');
		m_strCommand = strParsing.Left(iFind);

		iFindSTX = strParsing.Find((char)_STX);
		m_strCommand = m_strCommand.Mid(iFindSTX + 1, m_strCommand.GetLength());

		int iCommand = _ttoi(m_strCommand);

		m_strContents = strParsing.Mid(iFind + 1, strParsing.GetLength());

		m_lastCommand[Num] = VS_PacketNameTable[iCommand];
		m_lastRequest[Num] = m_strContents;

		if (Num == PC1)
			theApp.m_pVisionSendReceiver1Log->LOG_INFO(CStringSupport::FormatString(_T("[VS -> MC] [Command : %s] ->%s"), m_lastCommand[Num], strData));
		else
			theApp.m_pVisionSendReceiver2Log->LOG_INFO(CStringSupport::FormatString(_T("[VS -> MC] [Command : %s] ->%s"), m_lastCommand[Num], strData));

		CString sendMsg;
		switch (iCommand)
		{
		case VS_ARE_YOU_THERE:
			//// 旧的 Vision 连接检查计数 (已废弃)
			//theApp.m_VisionSocketManager[Num].m_iVisionSocketCheckCount = 0;
			break;
		case VS_PCTIME_REQUEST:
			LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s"), Num, _T("RCV : VS_PCTIME_REQUEST")), Num);
			ParsingPcTimeRequest(Num, m_strContents);			
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
			LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s"), Num, _T("RCV : VS_MODEL")), Num);
			ParshingVisionData(Num, m_strContents);
			break;
		case VS_MODEL_REQUEST:
			LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s"), Num, _T("RCV : VS_MODEL_REQUEST")), Num);
			ParsingModelRequest(Num, m_strContents);
			break;
		case VS_MODEL_CREATE:
			LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s"), Num, _T("RCV : VS_MODEL_CREATE")), Num);
			if (Num == PC1)
			{
				theApp.m_CreateModelVision1 = FALSE;
				theApp.m_PlcThread->ModelCreateChangeModify(_T("ModelCreate"), _T("Vision1"), theApp.m_CreateModelVision1);

				//// 旧的 MC_MODEL_CHANGE 发送 (已废弃)
				//if (theApp.m_ChangeModelVision1)
				//{
				//	sendMsg.Format(_T("%d,%s"), MC_MODEL_CHANGE, theApp.m_CurrentModel.m_AlignPcCurrentModelName);
				//	theApp.m_VisionSocketManager[PC1].SocketSendto(PC1, sendMsg, MC_MODEL_CHANGE);
				//}
			}
			else
			{
				theApp.m_CreateModelVision2 = FALSE;
				theApp.m_PlcThread->ModelCreateChangeModify(_T("ModelCreate"), _T("Vision2"), theApp.m_CreateModelVision2);

				//// 旧的 MC_MODEL_CHANGE 发送 (已废弃)
				//if (theApp.m_ChangeModelVision2)
				//{
				//	sendMsg.Format(_T("%d,%s"), MC_MODEL_CHANGE, theApp.m_CurrentModel.m_AlignPcCurrentModelName);
				//	theApp.m_VisionSocketManager[PC2].SocketSendto(PC2, sendMsg, MC_MODEL_CHANGE);
				//}
			}

			theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("Vision %d PC Model Create Success"), Num));
			break;
		case VS_MODEL_CHANGE:
			LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s"), Num, _T("RCV : VS_MODEL_CHANGE")), Num);
			if (Num == PC1)
			{
				theApp.m_ChangeModelVision1 = FALSE;
				theApp.m_PlcThread->ModelCreateChangeModify(_T("ModelChange"), _T("Vision1"), theApp.m_ChangeModelVision1);
			}
			else
			{
				theApp.m_ChangeModelVision2 = FALSE;
				theApp.m_PlcThread->ModelCreateChangeModify(_T("ModelChange"), _T("Vision2"), theApp.m_ChangeModelVision2);
			}

			theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("Vision %d PC Model Change Success"), Num));
			break;
		case VS_GRAB_END:
			LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s->%s"), Num, _T("RCV : VS_GRAB_END"), m_strContents), Num);
			ParsingGrabEnd(Num, m_strContents);
			break;
		case VS_INSPECTION_OK:
			LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s->%s"), Num, _T("RCV : VS_INSPECTION_OK"), m_strContents), Num);
			break;
		case VS_INSPECTION_RESULT:
			LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s->%s"), Num, _T("RCV : VS_INSPECTION_RESULT"), m_strContents), Num);
			theApp.m_pTestLog->LOG_DEBUG(_T("[VS %d -> MC] %s->%s"), Num, _T("RCV : VS_INSPECTION_RESULT"), m_strContents);
			ParsingInspectionResult(Num, m_strContents);
			break;
		case VS_AUTO_CAM_SET_START:
			LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s->%s"), Num, _T("RCV : VS_AUTO_CAM_SET_START"), m_strContents), Num);
			break;
		case VS_Z_MOVE_REQUEST:
			LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s->%s"), Num, _T("RCV : VS_Z_MOVE_REQUEST"), m_strContents), Num);
			AutoFocusAxis(Num, 1, m_strContents);
			break;
		case VS_FOCUS_MOVE_REQUEST:
			LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s->%s"), Num, _T("RCV : VS_FOCUS_MOVE_REQUEST"), m_strContents), Num);
			AutoFocusAxis(Num, 2, m_strContents);
			break;
		case VS_Z_SAVE_POS_REQUEST:
			LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s->%s"), Num, _T("RCV : VS_Z_SAVE_POS_REQUEST"), m_strContents), Num);
			break;
		case VS_FOCUS_SAVE_POS_REQUEST:
			LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s->%s"), Num, _T("RCV : VS_FOCUS_SAVE_POS_REQUEST"), m_strContents), Num);
			AutoFocusSave(Num);
			break;
		case VS_VISION_TEST:
			LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s->%s"), Num, _T("RCV : VS_VISION_TEST"), m_strContents), Num);
			VisionInspectionMethod(0, 0);
			VisionInspectionMethod(1, 1);
			break;
		}

	}
}

void CVisionThread::VisionFirstCheckMethod(int Num)
{
	BOOL bModelCreate, bModelChange;
	//??? ??????????? IO (MC_ARE_YOU_THERE) , PCTime(MC_PCTIME), ???(MC_MODEL)
	CString strCommand = CStringSupport::FormatString(_T("%d,%d"), MC_ARE_YOU_THERE, theApp.m_VisionSocketManager[Num].m_iVisionSocketCheckCount);
	SocketSendto(Num, strCommand, MC_ARE_YOU_THERE);
	Delay(200, TRUE);

	strCommand = CStringSupport::FormatString(_T("%d,%s"), MC_PCTIME, GetDateString4());
	SocketSendto(Num, strCommand, MC_PCTIME);
	Delay(200, TRUE);

	if (Num == PC1)
	{
		bModelCreate = theApp.m_CreateModelVision1;
		bModelChange = theApp.m_ChangeModelVision1;
	}
	else
	{
		bModelCreate = theApp.m_CreateModelVision2;
		bModelChange = theApp.m_ChangeModelVision2;
	}
	
	if (bModelCreate)
	{
		strCommand = CStringSupport::FormatString(_T("%d,%s"), MC_MODEL_CREATE, theApp.m_CurrentModel.m_AlignPcCurrentModelName);
		SocketSendto(Num, strCommand, MC_MODEL_CREATE);
		LogWrite(CStringSupport::FormatString(_T("[MC -> VS %d] %s->%s"), Num, MC_PacketNameTable[MC_MODEL_CREATE], strCommand), Num);
		Delay(200, TRUE);
	}
	
	if (bModelChange)
	{
		strCommand = CStringSupport::FormatString(_T("%d,%s"), MC_MODEL_CHANGE, theApp.m_CurrentModel.m_AlignPcCurrentModelName);
		SocketSendto(Num, strCommand, MC_MODEL_CHANGE);
		LogWrite(CStringSupport::FormatString(_T("[MC -> VS %d] %s->%s"), Num, MC_PacketNameTable[MC_MODEL_CHANGE], strCommand), Num);
		Delay(200, TRUE);
	}

}

void CVisionThread::VisionCheckMethod(int Num)
{
	CString strCommand = CStringSupport::FormatString(_T("%d,%d"), MC_ARE_YOU_THERE, theApp.m_VisionSocketManager[Num].m_iVisionSocketCheckCount);
	SocketSendto(Num, strCommand, MC_ARE_YOU_THERE);
}

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

void CVisionThread::ParsingModelRequest(int Num, CString strContents)
{
	CString sendMsg;
	sendMsg.Format(_T("%d,%s"), MC_MODEL, theApp.m_CurrentModel.m_AlignPcCurrentModelName);
	SocketSendto(Num, sendMsg, MC_MODEL);
	LogWrite(CStringSupport::FormatString(_T("[MC -> VS %d] %s->%s"), Num, MC_PacketNameTable[MC_MODEL], sendMsg), Num);
}

void CVisionThread::ParsingPcTimeRequest(int Num, CString strContents)
{
	CString sendMsg;
	sendMsg.Format(_T("%d,%s"), MC_PCTIME, GetDateString4());
	SocketSendto(Num, sendMsg, MC_PCTIME);
	LogWrite(CStringSupport::FormatString(_T("[MC -> VS %d] %s->%s"), Num, MC_PacketNameTable[MC_PCTIME], sendMsg), Num);
}

///////////////////////////////////////////////////////////////////////////////
// 发送 ICW Start$ 消息给点灯检系统
// 格式: Start$ABCD$WXYZ@
// - 前一组(ABCD) = 当前检测哪几个治具，有产品用序号(01~04)，无产品用00
// - 后一组(WXYZ) = 最多可检测哪几个治具(固定01020304)
// 例: Start$01020304$01020304@ (检测4片)
//     Start$01020000$01020304@ (只检测01和02)
///////////////////////////////////////////////////////////////////////////////
void CVisionThread::SendICWStartMessage(BOOL bSimulation)
{
#if _SYSTEM_AMTAFT_
	LogWrite(_T("[ICW Start$] ========== SendICWStartMessage 开始 =========="), 0);
	LogWrite(CStringSupport::FormatString(_T("[ICW Start$] Simulation=%d, ICW Connected=%d"),
		bSimulation, theApp.m_ICWCommManager.IsConnected()), 0);

	// 最多4个治具，固定最大数量
	const int MAX_JIG = 4;
	CString strCurrentJigs = _T("");   // 当前检测的治具
	CString strMaxJigs = _T("");        // 最大治具数量

	// AUTO_TEST 模拟模式：直接发送所有槽位都有产品
	if (bSimulation)
	{
		strCurrentJigs = _T("01020304");
		strMaxJigs = _T("01020304");
		LogWrite(_T("[AUTO_TEST] Simulation mode: Send Start$01020304$01020304@"), 0);
	}
	else
	{
		// 读取当前4个槽位的Panel数据，判断哪些槽位有产品
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

			// 有产品用治具号(01~04)，无产品用00
			if (!strPanel.IsEmpty())
			{
				strCurrentJigs += CStringSupport::FormatString(_T("%02d"), i + 1);
				LogWrite(CStringSupport::FormatString(_T("[ICW Start$] Jig %d has product: Panel=%s, FPC=%s"),
					i + 1, strPanel, strFpcID), 0);
			}
			else
			{
				strCurrentJigs += _T("00");
				LogWrite(CStringSupport::FormatString(_T("[ICW Start$] Jig %d no product"), i + 1), 0);
			}

			// 最大治具固定为 01,02,03,04
			strMaxJigs += CStringSupport::FormatString(_T("%02d"), i + 1);
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
				theApp.m_PlcLog->LOG_INFO(_T("[AUTO_TEST] IVS_LCD_IDMap updated for Start$ pattern [%s]"), strCurrentJigs);
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

void CVisionThread::VisionInspectionMethod(int Num, int panelNum)
{
	LogWrite(CStringSupport::FormatString(_T("[VisionInspectionMethod] PC=%d, PanelNum=%d Start"), Num, panelNum), 0);
	if (theApp.m_CurrentIndexZone < 0)
	{
		LogWrite(CStringSupport::FormatString(_T("[VisionInspectionMethod] PC=%d, PanelNum=%d Error: CurrentIndexZone=%d < 0"), Num, panelNum, theApp.m_CurrentIndexZone), 0);
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
		LogWrite(CStringSupport::FormatString(_T("[VisionInspectionMethod] PC=%d, PanelNum=%d Error: FPC ID Empty"), Num, panelNum), 0);
		VisionPLCResult(Num, panelNum, CStringSupport::FormatString(_T("PLC Vision Panel #%d ID Error"), panelNum), m_codePlcSendReceiverError, _T("NG"));
		return;
	}
	theApp.IndexCheck();
	iCurIndex = (theApp.m_CurrentIndexZone + (MaxZone - CZone)) % 4;
	indexPanelNum = theApp.m_indexList[iCurIndex].m_indexNum + panelNum;
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
	if (panelNum == 0 && !m_bICWStartSent[0])
	{
		LogWrite(CStringSupport::FormatString(_T("[VisionInspectionMethod] Send ICW Start Message (AutoTestMode=%d)"), theApp.m_iAutoTestMode == 1), 0);
		SendICWStartMessage(theApp.m_iAutoTestMode == 1);
		m_bICWStartSent[0] = TRUE;
	}
#endif

	LogWrite(CStringSupport::FormatString(_T("%s Panel %d [%s][%s] Vision Grab Start"), PG_IndexName[iCurIndex], Num, strPanel, strFpcID), Num);

	BOOL bFlag = VisionVecAdd(strPanel, strFpcID, panelNum, indexPanelNum, Num, iCurIndex, strUniqueID);
	if (bFlag)
	{
		//// 旧的 MC_INSPECTION_START 发送 (已废弃，现使用 ICW)
		//LogWrite(CStringSupport::FormatString(_T("[VisionInspectionMethod] Send MC_INSPECTION_START: %s"), sendMsg), 0);
		//sendMsg.Format(_T("%d,%s,%d,%s,%s"), MC_INSPECTION_START, strPanel, indexPanelNum, strProcessID, strFpcID);
		//SocketSendto(Num, sendMsg, MC_INSPECTION_START);
	}
}

void CVisionThread::ParsingGrabEnd(int Num, CString strContents)
{
	CString sendMsg, strPanelID, strFpcID;
	CStringArray responseTokens;
	CStringSupport::GetTokenArray(strContents, _T(','), responseTokens);

	strPanelID = responseTokens[0];
	strFpcID = responseTokens[1];
	strPanelID.Trim();
	strFpcID.Trim();

	LogWrite(CStringSupport::FormatString(_T("[ParsingGrabEnd] RCV MC_GRAB_END: PanelID=%s, FpcID=%s"), strPanelID, strFpcID), 0);
	//// 旧的 MC_GRAB_END_RECEIVE 发送 (已废弃，现使用 ICW)
	//sendMsg.Format(_T("%d,%s,%s"), MC_GRAB_END_RECEIVE, strPanelID, strFpcID);
	//LogWrite(CStringSupport::FormatString(_T("[ParsingGrabEnd] Send MC_GRAB_END_RECEIVE: %s"), sendMsg), 0);
	//SocketSendto(Num, sendMsg, MC_GRAB_END_RECEIVE);
	
	for (auto &InspResult : theApp.m_lastInspResultVec)
	{
		if (!InspResult.m_cellId.CompareNoCase(strPanelID))
		{
			if (InspResult.m_bInspStart == TRUE)
			{
				LogWrite(CStringSupport::FormatString(_T("Panel [%s] Vision Grab End"), strPanelID), Num);
				LogWrite(CStringSupport::FormatString(_T("[ParsingGrabEnd] Set PLC VisionGrabEnd1+%d = TRUE"), InspResult.m_iPanelNum), 0);
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionGrabEnd1 + InspResult.m_iPanelNum, OffSet_0, TRUE);
				InspResult.m_bGrabEnd = TRUE;
				break;
			}
		}
	}
}

void CVisionThread::ParsingInspectionResult(int Num, CString strContents)
{
	theApp.m_bVisionDeleteFlag = FALSE;
	CString sendMsg, strPanelID, strFpcID;
	CStringArray responseTokens;
	CStringSupport::GetTokenArray(strContents, _T(','), responseTokens);

	strPanelID = responseTokens[0];
	strFpcID = responseTokens[1];
	strPanelID.Trim();
	strFpcID.Trim();

	//// 旧的 MC_INSPECTION_RESULT_RECEIVE 发送 (已废弃，现使用 ICW)
	//sendMsg.Format(_T("%d,%s,%s"), MC_INSPECTION_RESULT_RECEIVE, strPanelID, strFpcID);
	//SocketSendto(Num, sendMsg, MC_INSPECTION_RESULT_RECEIVE);

	int iokng = 0;
	for (auto &InspResult : theApp.m_lastInspResultVec)
	{
		if (!InspResult.m_cellId.CompareNoCase(strPanelID) || !InspResult.m_FpcID.CompareNoCase(strFpcID))
		{
			if (InspResult.m_bGrabEnd == TRUE)
			{
				if (theApp.m_AOIPassMode)
				{
					InspResult.m_iResultValue = m_codeOk;
					theApp.m_pTestLog->LOG_INFO(_T("AddRankCodeList m_AOIPassMode ON %s"), InspResult.m_cellId);
				}
					
				else
				{
					InspResult.m_iResultValue = responseTokens[2] == _T("0") ? m_codeOk : m_codeFail;
					theApp.m_pTestLog->LOG_INFO(_T("AddRankCodeList m_AOIPassMode Off %s, Result %d"), InspResult.m_cellId, InspResult.m_iResultValue);
				}
					
				iokng = InspResult.m_iResultValue;
				if (InspResult.m_iResultValue == m_codeFail)
				{
					theApp.m_pRankTread->AddRankCodeList(InspResult.m_cellId, InspResult.m_FpcID, InspResult.m_iPanelNum, InspResult.m_iCurIndex, RankAOI);
					theApp.m_pTestLog->LOG_INFO(_T("AddRankCodeList %s NG"), InspResult.m_cellId);
				}
				else
				{
					theApp.m_pTestLog->LOG_INFO(_T("AddRankCodeList %s OK"), InspResult.m_cellId);
				}

				int iSendNGBuffer = 1;
				if (theApp.m_AOIPassMode)
					iSendNGBuffer = 1;/* 1 : go OK, 2 : go NG Buff*/	
				else
					iSendNGBuffer = _ttoi(responseTokens[3]) + 1;/* 1 : go OK, 2 : go NG Buff*/
				//iSendNGBuffer = 1; //TEST
				VisionPLCResult(InspResult.m_iPCNum,
					InspResult.m_iPanelNum,
					PLC_ResultValue[InspResult.m_iResultValue],
					InspResult.m_iResultValue,
					InspResult.m_cellId, iSendNGBuffer);

#if 0  // DFS上传已移至PlcThread::DFSDataStart，由PLC DFSStart触发
				// 从 PLC 读取所有工序数据 + 从 MySQL 读取 AOI 结果，合并后上传 DFS (FTP)
				if (theApp.m_pFTP != NULL)
				{
					// 1. 从 PLC 读取其他工序数据（Contact、PreGamma、TP、Lumitop、OPView 等）
					DfsData dfsDataPlc;
					int iNum = InspResult.m_iPanelNum;  // 槽位号 0-3
					BOOL bIsOK = (InspResult.m_iResultValue == m_codeOk);  // TRUE=OK, FALSE=NG

#if _SYSTEM_AMTAFT_
					if (bIsOK)
						theApp.m_pEqIf->m_pMNetH->GetDfsData(eWordType_UnloadOKDFSValue1 + iNum, &dfsDataPlc);
					else
						theApp.m_pEqIf->m_pMNetH->GetDfsData(eWordType_UnloadNGDFSValue1 + iNum, &dfsDataPlc);
#else
					if (bIsOK)
						theApp.m_pEqIf->m_pMNetH->GetDfsData(eWordType_DFSValue1 + iNum, &dfsDataPlc);
					else
						theApp.m_pEqIf->m_pMNetH->GetDfsData(eWordType_GammaNGDFSValue1 + iNum, &dfsDataPlc);
#endif

					// 2. 转换为 DfsDataValue 格式
					DfsDataValue dfsData;
					dfsData.m_FpcID = CStringSupport::ToWString(dfsDataPlc.m_FpcID, sizeof(dfsDataPlc.m_FpcID));
					dfsData.m_PanelID = CStringSupport::ToWString(dfsDataPlc.m_PanelID, sizeof(dfsDataPlc.m_PanelID));
					dfsData.m_StartTime = DFSDataTimeParser(dfsDataPlc.m_StartTime1, dfsDataPlc.m_StartTime2, dfsDataPlc.m_StartTime3);
					dfsData.m_LoadHandlerTime = DFSDataTimeParser(dfsDataPlc.m_LoadHandlerTime1, dfsDataPlc.m_LoadHandlerTime2, dfsDataPlc.m_LoadHandlerTime3);
					dfsData.m_UnloadHandlerTime = DFSDataTimeParser(dfsDataPlc.m_UnLoadHandlerTime1, dfsDataPlc.m_UnLoadHandlerTime2, dfsDataPlc.m_UnLoadHandlerTime3);
					dfsData.m_TpTime = Int2String(dfsDataPlc.m_TPTime);
					dfsData.m_PreGammaTime = Int2String(dfsDataPlc.m_PreGammaTime);
					dfsData.m_EndTime = DFSDataTimeParser(dfsDataPlc.m_EndTime1, dfsDataPlc.m_EndTime2, dfsDataPlc.m_EndTime3);
					dfsData.m_TactTime = DFSDataTactTimeParser(dfsDataPlc.m_StartTime2, dfsDataPlc.m_StartTime3, dfsDataPlc.m_EndTime2, dfsDataPlc.m_EndTime3);
					dfsData.m_PreGammaContactStatus = Int2String(dfsDataPlc.m_PreGammaContactStatus);
					dfsData.m_ModelID = CStringSupport::ToWString(dfsDataPlc.m_ModelID, sizeof(dfsDataPlc.m_ModelID));
					dfsData.m_IndexNum = Int2String(dfsDataPlc.m_IndexNum);
					dfsData.m_ChNum = Int2String(dfsDataPlc.m_ChNum);
					dfsData.m_TpResult = Int2String(dfsDataPlc.m_TpResult);
					dfsData.m_Contact = dfsDataPlc.m_Contact == 1 ? _T("OK") : dfsDataPlc.m_Contact == 2 ? _T("NG") : _T("BYPASS");
					dfsData.m_PreGamma = dfsDataPlc.m_PreGamma == 1 ? _T("OK") : dfsDataPlc.m_PreGamma == 2 ? _T("NG") : _T("BYPASS");
					dfsData.m_TpResult2 = dfsDataPlc.m_TpResult2 == 1 ? _T("OK") : dfsDataPlc.m_TpResult2 == 2 ? _T("NG") : _T("BYPASS");
					dfsData.m_Lumitop = dfsDataPlc.m_Lumitop == 1 ? _T("OK") : dfsDataPlc.m_Lumitop == 2 ? _T("NG") : _T("BYPASS");
					dfsData.m_ContactCount = Int2String(dfsDataPlc.m_ContactCount);
					dfsData.m_LoadeHandlerNUM = Int2String(dfsDataPlc.m_LoadeHandlerNUM);
					dfsData.m_UnLoadeHandlerNUM = Int2String(dfsDataPlc.m_UnLoadeHandlerNUM);
					dfsData.m_opViewResult = dfsDataPlc.m_OPView == 1 ? _T("OK") : dfsDataPlc.m_OPView == 2 ? _T("NG") : _T("BYPASS");
					dfsData.m_TypeNum = Machine_AOI;  // 点灯检是 AOI 设备
					dfsData.m_StageNum = iNum + 1;

					// 3. 从 MySQL 读取 AOI 结果，覆盖 m_AOIInpsect
					if (!InspResult.m_UniqueID.IsEmpty())
					{
						CInspectionResult aoiResult;
						if (GetDBInterface().QueryByUniqueID(InspResult.m_UniqueID, aoiResult))
						{
							// 根据 MySQL 的 AOIResult 设置 AOI 工序结果
							if (aoiResult.AOIResult.CompareNoCase(_T("OK")) == 0)
								dfsData.m_AOIInpsect = _T("OK");
							else
								dfsData.m_AOIInpsect = _T("NG");  // 其他（NG/BrightDot等）都按 NG 处理

							LogWrite(CStringSupport::FormatString(_T("Panel [%s] AOI Result from MySQL: %s"), 
								strPanelID, aoiResult.AOIResult), Num);
						}
						else
						{
							// MySQL 查询失败时，用本地结果
							dfsData.m_AOIInpsect = (InspResult.m_iResultValue == m_codeOk) ? _T("OK") : _T("NG");
							LogWrite(CStringSupport::FormatString(_T("Panel [%s] AOI Result from Local: %s, MySQL Error: %s"), 
								strPanelID, dfsData.m_AOIInpsect, GetDBInterface().GetLastError()), Num);
						}
					}
					else
					{
						// 没有 UniqueID 时，用本地结果
						dfsData.m_AOIInpsect = (InspResult.m_iResultValue == m_codeOk) ? _T("OK") : _T("NG");
					}

					// 4. 上传到 FTP
					theApp.m_pFTP->AddTransferFile(dfsData);
					LogWrite(CStringSupport::FormatString(_T("Panel [%s] DFS Upload Success (Contact:%s, PreGamma:%s, AOI:%s, TP:%s, Lumitop:%s, OPView:%s)"), 
						strPanelID, dfsData.m_Contact, dfsData.m_PreGamma, dfsData.m_AOIInpsect, 
						dfsData.m_TpResult, dfsData.m_Lumitop, dfsData.m_opViewResult), Num);
				}
#endif

				InspResult.time_check.StopTimer();
				InspResult.m_bResult = TRUE;
			}
			else
			{
				theApp.m_pTestLog->LOG_DEBUG(_T("Panel [%s] Vision Grab Error"), strPanelID);
				LogWrite(CStringSupport::FormatString(_T("Panel [%s] Vision Grab Error"), strPanelID), Num);
			}
			break;
		}
		else
		{
			theApp.m_pTestLog->LOG_DEBUG(_T("PanelID Err : %s"), strPanelID);
		}
	}
	theApp.m_bVisionDeleteFlag = TRUE;
	if (!theApp.m_AOIPassMode)
	{
		if (iokng == 2)
		{
			CString strMsg;
			strMsg = CStringSupport::FormatString(_T("%d,%d"), MC_NG_PANEL, _AOI);
			theApp.m_OpvSocketManager[0].SendOpvMessage(strMsg, 0, MC_NG_PANEL);
			theApp.m_OpvSocketManager[1].SendOpvMessage(strMsg, 1, MC_NG_PANEL);
			
		}
	}
	else
	{
		theApp.m_pTestLog->LOG_DEBUG(_T("AOI PassMode %s"), strPanelID);
	}
	
}

void CVisionThread::OnEvent(UINT uEvent, LPVOID lpvData)
{
	if (theApp.m_bExitFlag == FALSE)
		return;

	SockAddrIn addrin;
	GetSockName(addrin);
	int VisionNum = ntohs(addrin.GetPort()) == _ttoi(VISION_PC1_PORT_NUM) ? PC1 : PC2;
	
	switch (uEvent)
	{
	case EVT_CONDROP:
		LogWrite(CStringSupport::FormatString(_T("Vision Connect Drop %d Ch"), VisionNum), VisionNum);
		break;
	case EVT_CONSUCCESS:
		LogWrite(CStringSupport::FormatString(_T("Vision Connect Success %d Ch"), VisionNum), VisionNum);
		break;
	case EVT_ZEROLENGTH:
		LogWrite(CStringSupport::FormatString(_T("Vision EVT_ZEROLENGTH %d Ch"), VisionNum), VisionNum);
		break;
	case EVT_CONFAILURE:
		LogWrite(CStringSupport::FormatString(_T("Vision EVT_CONFAILURE %d Ch"), VisionNum), VisionNum);
		break;
	default:
		LogWrite(CStringSupport::FormatString(_T("Vision Unknown Socket event %d Ch"), VisionNum), VisionNum);
		break;
	}
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
				theApp.m_VisionLog->LOG_INFO(_T("Terminate Vision Thread"));
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

void CVisionThread::SocketSendto(int Num, CString strContents, int iCommand)
{
	if (theApp.m_bExitFlag == FALSE)
		return;

	m_csSocketSend.Lock();
	CString strCommand = CStringSupport::FormatString(_T("%c%s,%c"), _STX, strContents, _ETX);
	char *lpCommand = StringToChar(strCommand);
	theApp.m_VisionSocketManager[Num].WriteComm((BYTE*)lpCommand, strlen(lpCommand), 100L);
	delete lpCommand;

	m_lastContent[Num] = strContents;

	if (Num == PC1)
		theApp.m_pVisionSendReceiver1Log->LOG_INFO(CStringSupport::FormatString(_T("[MC -> VS] [Command : %s] ->%s"), MC_PacketNameTable[iCommand], strContents));
	else
		theApp.m_pVisionSendReceiver2Log->LOG_INFO(CStringSupport::FormatString(_T("[MC -> VS] [Command : %s] ->%s"), MC_PacketNameTable[iCommand], strContents));

	m_csSocketSend.Unlock();
}

void CVisionThread::LogWrite(CString strContents, int Num)
{
	if (theApp.m_bExitFlag == FALSE)
		return;

	// 先写入日志文件（线程安全）
	theApp.m_VisionLog->LOG_INFO(strContents);

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
}

BOOL CVisionThread::VisionVecAdd(CString strPanel, CString strFpcID, int iPanelNum, int iIndexNum, int iPCNo, int iCurIndex, const CString& strUniqueID)
{
	BOOL flag = TRUE;
	InspResult panelData;
	panelData.Reset();

	panelData.m_bInspStart = TRUE;
	if (theApp.m_iTimer[VisionGrabTimer] == 0)
		panelData.time_check.SetCheckTime(8000);
	else
		panelData.time_check.SetCheckTime(theApp.m_iTimer[VisionGrabTimer] * 1000);

	panelData.time_check.StartTimer();
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
			int iPcNum = iPanelNum <= 1 ? PC1 : PC2;
			LogWrite(CStringSupport::FormatString(_T("[ICW Start$] Trigger VisionInspectionMethod: PC=%d, Panel=%d"),
				iPcNum, iPanelNum), 0);
			VisionInspectionMethod(iPcNum, iPanelNum);
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
	LogWrite(CStringSupport::FormatString(_T("[ICW] OnICWFinishFN: %d slots"), (int)finishInfo.Results.size()), 0);
	LogWrite(_T("[ICW FN$] ========== FN$ 处理开始 =========="), 0);
	LogWrite(CStringSupport::FormatString(_T("[ICW FN$] 收到 %d 个槽位结果"), (int)finishInfo.Results.size()), 0);

	// FN$ 每 2 位一个工位，results[0] 对应治具 1，results[1] 对应治具 2，...
	for (size_t i = 0; i < finishInfo.Results.size(); i++)
	{
		int nResult = finishInfo.Results[i];
		int nFixtureNo = (int)i + 1;  // 治具号 1~4

		// 跳过无效结果（0 表示该槽无产品）
		if (nResult == 0)
		{
			LogWrite(CStringSupport::FormatString(_T("[ICW] Slot %d: no product (result=0), skip"), nFixtureNo), 0);
			LogWrite(CStringSupport::FormatString(_T("[ICW FN$] 治具 %d: 无产品 (result=0), 跳过"), nFixtureNo), nFixtureNo - 1);
			continue;
		}

		LogWrite(CStringSupport::FormatString(_T("[ICW] Slot %d: result=%d"), nFixtureNo, nResult), 0);
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] ========== 处理治具 %d =========="), nFixtureNo), nFixtureNo - 1);
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] 治具 %d: 点灯检结果=%d"), nFixtureNo, nResult), nFixtureNo - 1);

		// Step 1: 查询 ivs_lcd_idmap（MainAoiFixID → UniqueID/Barcode）
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] 治具 %d: Step1 - 查询 ivs_lcd_idmap (MainAoiFixID=%d)"), nFixtureNo, nFixtureNo), nFixtureNo - 1);
		CIDMapInfo idMapInfo;
		if (!GetDBInterface().QueryIDMapByFixtureNo(nFixtureNo, idMapInfo))
		{
			LogWrite(CStringSupport::FormatString(
				_T("[ICW] QueryIDMapByFixtureNo failed for fixture %d: %s"),
				nFixtureNo, (LPCTSTR)GetDBInterface().GetLastError()), 0);
			LogWrite(CStringSupport::FormatString(
				_T("[ICW FN$] 治具 %d: Step1 失败 - %s"),
				nFixtureNo, (LPCTSTR)GetDBInterface().GetLastError()), nFixtureNo - 1);
			continue;
		}

		CString strUniqueID = idMapInfo.UniqueID;
		CString strBarcode = idMapInfo.ScreenID;  // ivs_lcd_idmap.Barcode
		LogWrite(CStringSupport::FormatString(_T("[ICW] Fixture %d: UniqueID=%s, Barcode=%s"),
			nFixtureNo, (LPCTSTR)strUniqueID, (LPCTSTR)strBarcode), 0);
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] 治具 %d: Step1 完成 - UniqueID=%s, Barcode=%s"),
			nFixtureNo, (LPCTSTR)strUniqueID, (LPCTSTR)strBarcode), nFixtureNo - 1);

		// Step 2: 查询 IVS_LCD_InspectionResult（UniqueID → AOIResult/Code/Grade）
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] 治具 %d: Step2 - 查询 IVS_LCD_InspectionResult"), nFixtureNo), nFixtureNo - 1);
		CInspectionResult inspResult;
		if (!GetDBInterface().QueryByUniqueID(strUniqueID, inspResult))
		{
			LogWrite(CStringSupport::FormatString(_T("[ICW] QueryByUniqueID failed for %s: %s"),
				(LPCTSTR)strUniqueID, (LPCTSTR)GetDBInterface().GetLastError()), 0);
			LogWrite(CStringSupport::FormatString(
				_T("[ICW FN$] 治具 %d: Step2 失败/无记录 - %s"),
				nFixtureNo, (LPCTSTR)GetDBInterface().GetLastError()), nFixtureNo - 1);
			// 即使查不到也继续写 PLC（使用 FN$ 的结果）
		}
		else
		{
			LogWrite(CStringSupport::FormatString(_T("[ICW FN$] 治具 %d: Step2 完成 - InspectionResult SysID=%d"),
				nFixtureNo, inspResult.SysID), nFixtureNo - 1);
		}

		// Step 3: 写入 PLC 主结果（PreGammaResult）
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] 治具 %d: Step3 - 写入 PLC PreGammaResult"), nFixtureNo), nFixtureNo - 1);
		int nPlcResult = (nResult == 1) ? m_codeOk : m_codeFail;  // 1=OK, 2=NG/ERROR
		theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_PreGammaResult1 + (nFixtureNo - 1), &nPlcResult);
		LogWrite(CStringSupport::FormatString(_T("[ICW] Set PLC PreGammaResult%d = %d"), nFixtureNo, nPlcResult), 0);
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] 治具 %d: Step3 完成 - PreGammaResult=%d (%s)"),
			nFixtureNo, nPlcResult, nPlcResult == m_codeOk ? _T("OK") : _T("NG")), nFixtureNo - 1);

		// Step 4: 查询 ivs_lcd_aoidefect（获取缺陷详情）
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] 治具 %d: Step4 - 查询 ivs_lcd_aoidefect"), nFixtureNo), nFixtureNo - 1);
		CDefectInfoList defectList;
		CString strDefectCode = _T("");
		CString strGrade = _T("");

		if (!strUniqueID.IsEmpty() && inspResult.SysID > 0)
		{
			// 通过 GUID 查询缺陷
			if (GetDBInterface().QueryDefectsByParentGUID(inspResult.GUID, defectList))
			{
				LogWrite(CStringSupport::FormatString(_T("[ICW FN$] 治具 %d: Step4 完成 - 找到 %d 个缺陷"),
					nFixtureNo, (int)defectList.size()), nFixtureNo - 1);
				// 汇总缺陷码和等级（取最严重）
				for (const auto& defect : defectList)
				{
					if (!defect.Code_AOI.IsEmpty())
					{
						if (strDefectCode.IsEmpty() || defect.Grade_AOI.CompareNoCase(_T("A")) < 0)
						{
							strDefectCode = defect.Code_AOI;
							strGrade = defect.Grade_AOI;
						}
					}
				}
			}
			else
			{
				LogWrite(CStringSupport::FormatString(_T("[ICW FN$] 治具 %d: Step4 完成 - 无缺陷记录"), nFixtureNo), nFixtureNo - 1);
			}
		}
		else
		{
			LogWrite(CStringSupport::FormatString(_T("[ICW FN$] 治具 %d: Step4 跳过 - UniqueID 为空或无检测记录"), nFixtureNo), nFixtureNo - 1);
		}

		// Step 5: 写入 PLC 缺陷代码 + 等级
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] 治具 %d: Step5 - 写入 PLC VisionResult"), nFixtureNo), nFixtureNo - 1);
		// 写入 VisionResult1~4（对应治具 1~4）
		int nVisionResult = nPlcResult;  // 默认用主结果
		if (!strDefectCode.IsEmpty())
		{
			// 如果有缺陷码，设置 NG
			nVisionResult = m_codeFail;
		}
		theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_VisionResult1 + (nFixtureNo - 1), &nVisionResult);
		LogWrite(CStringSupport::FormatString(_T("[ICW] Set PLC VisionResult%d = %d (DefectCode=%s, Grade=%s)"),
			nFixtureNo, nVisionResult, (LPCTSTR)strDefectCode, (LPCTSTR)strGrade), 0);
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] 治具 %d: Step5 完成 - VisionResult=%d, DefectCode=%s, Grade=%s"),
			nFixtureNo, nVisionResult, (LPCTSTR)strDefectCode, (LPCTSTR)strGrade), nFixtureNo - 1);

		// 写入缺陷码（如果有的话，存储到 PLC 对应区域）
		if (!strDefectCode.IsEmpty())
		{
			// TODO: 根据现场 PLC 地址规划写入缺陷码
			LogWrite(CStringSupport::FormatString(_T("[ICW] Defect for fixture %d: Code=%s, Grade=%s"),
				nFixtureNo, (LPCTSTR)strDefectCode, (LPCTSTR)strGrade), 0);
		}

		// Step 6: DFS 数据上传（点灯/Lumitop 设备）
		LogWrite(CStringSupport::FormatString(_T("[ICW FN$] 治具 %d: Step6 - DFS 数据上传"), nFixtureNo), nFixtureNo - 1);
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
			LogWrite(CStringSupport::FormatString(_T("[ICW FN$] 治具 %d: Step6 完成 - DFS上传: PanelID=%s, Lumitop=%s"),
				nFixtureNo, (LPCTSTR)strBarcode, (LPCTSTR)strLumitopResult), nFixtureNo - 1);
		}
		else
		{
			LogWrite(CStringSupport::FormatString(_T("[ICW FN$] 治具 %d: Step6 跳过 - Barcode 为空"), nFixtureNo), nFixtureNo - 1);
		}

		LogWrite(CStringSupport::FormatString(
			_T("[ICW FN$] 治具 %d: ========== 处理完成 =========="), nFixtureNo), nFixtureNo - 1);
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
}

#endif