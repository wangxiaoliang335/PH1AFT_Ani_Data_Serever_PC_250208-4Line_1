
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
}

CVisionThread::~CVisionThread()
{
}

void CVisionThread::ThreadRun()
{
	AutoFocusData pAutoFocusData;
	for (auto &InspResult : theApp.m_lastInspResultVec)
		InspResult.Reset();

	while (::WaitForSingleObject(m_hQuit, 50) != WAIT_OBJECT_0)
	{
		theApp.m_VisionConectStatus[0] = theApp.m_VisionSocketManager[0].getConectCheck();
		theApp.m_VisionConectStatus[1] = theApp.m_VisionSocketManager[1].getConectCheck();

		//if (theApp.m_bAllPassMode)
		//	continue;

		if ((theApp.m_VisionConectStatus[0] && theApp.m_VisionConectStatus[1]) || theApp.m_AOIPassMode)
		{
			if (m_bFirstStatus)
			{
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

			//TEST Model ?? (TRUE) Model ???? ????? ??? ??? ???? ????.
			//TEST Model ?? (FALSE) Model ???? ?? ???? ??? check 
			if (theApp.m_AOIPassMode == FALSE)
			{
				if (theApp.m_PlcConectStatus == FALSE || theApp.m_ChangeModelVision1 == TRUE || theApp.m_ChangeModelVision2 == TRUE)
					continue;
			}

			if (time_check.IsTimeOver())
			{
				time_check.StartTimer();
				for (int ii = 0; ii < PCMaxCount; ii++)
				{
					VisionCheckMethod(ii);
					if (theApp.m_VisionSocketManager[ii].m_iVisionSocketCheckCount > 5)
					{
						LogWrite(CStringSupport::FormatString(_T("Vision PC %d Client Drop"), ii), ii);
					}
					theApp.m_VisionSocketManager[ii].m_iVisionSocketCheckCount++;
				}
			}

			if (theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_VisionPlcSend, 0))
				VisionPanelCheck();
			else
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionPcReceiver, 0, FALSE);

			for (int ii = 0; ii < PanelMaxCount; ii++)
			{
				m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_VisionStart1, OffSet_0 + ii);

				if (m_bStartFlag == FALSE)
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_VisionResult1 + ii, &m_codeReset);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionGrabEnd1 + ii, OffSet_0, FALSE);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionEnd1 + ii, OffSet_0, FALSE);
				}

				if (m_bStartVision[ii] == !m_bStartFlag)
				{
					m_bStartVision[ii] = m_bStartFlag;
					theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("Vision Panel %d Start Flag [%s]"), ii + 1, m_bStartVision[ii] == FALSE ? _T("FALSE") : _T("TRUE")));

					if (m_bStartFlag == TRUE)
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_VisionResult1 + ii, &m_codeReset);
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionGrabEnd1 + ii, OffSet_0, FALSE);
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionEnd1 + ii, OffSet_0, FALSE);
						//Delay(100);
						m_iPcNum = ii <= PanelNum2 ? PC1 : PC2;
						VisionInspectionMethod(m_iPcNum, PanelNum1 + ii);
					}
				}
			}

			for (int ii = 0; ii < MaxCamCount; ii++)
			{
				m_bAutoFocusStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_AutoFocusEnd1 + ii, OffSet_0);

				if (m_bAutoFocusStartFlag == TRUE)
				{
					theApp.m_pEqIf->m_pMNetH->SetAutoFocusData(eWordType_AutoFocusMoter1, &pAutoFocusData);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AutoFocusSave1 + ii, OffSet_0, FALSE);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AutoFocusStart1 + ii, OffSet_0, FALSE);
				}

				if (m_bAutoFocusStart[ii] == !m_bAutoFocusStartFlag)
				{
					m_bAutoFocusStart[ii] = m_bAutoFocusStartFlag;
					theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("Auto Focus Cam_%d End Flag [%s]"), ii + 1, m_bAutoFocusStart[ii] == FALSE ? _T("FALSE") : _T("TRUE")));

					if (m_bAutoFocusStartFlag == TRUE)
					{
						if (theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_AutoFocusSave1 + ii, OffSet_0))
						{
							//save
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
								theApp.m_VisionSocketManager->SocketSendto(ii, _T("GOOD"), MC_Z_MOVE_DONE);
							else
								theApp.m_VisionSocketManager->SocketSendto(ii, _T("GOOD"), MC_FOCUS_MOVE_DONE);

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
					if (InspResult.time_check.IsTimeOver())
					{
						CString sendMsg;
						sendMsg.Format(_T("%d,%s"), MC_INSPECTION_END, InspResult.m_cellId);
						SocketSendto(InspResult.m_iPCNum, sendMsg, MC_INSPECTION_END);
						LogWrite(CStringSupport::FormatString(_T("Vision %d Inspection Last Request Start")), InspResult.m_iPCNum);
						InspResult.m_LastCheck = TRUE;
						if (theApp.m_iTimer[VisionLastGrabTimer] == 0)
							InspResult.time_check.SetCheckTime(1000);
						else
							InspResult.time_check.SetCheckTime(theApp.m_iTimer[VisionLastGrabTimer] * 1000);

						InspResult.time_check.StartTimer();
					}
				}

				if (InspResult.m_bResult == TRUE && theApp.m_bVisionDeleteFlag == TRUE)
					InspResult.Reset();

			}
			
		}
		else
		{
			theApp.m_VisionPCStatus[0] = FALSE;
			theApp.m_VisionPCStatus[1] = FALSE;
			m_bFirstStatus = TRUE;
		}
	}
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
			theApp.m_VisionSocketManager[Num].m_iVisionSocketCheckCount = 0;
			break;
		case VS_PCTIME_REQUEST:
			LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s"), Num, _T("RCV : VS_PCTIME_REQUEST")), Num);
			ParsingPcTimeRequest(Num, m_strContents);			
			sendMsg.Format(_T("%d,%d"), MC_STATE, !theApp.m_PlcThread->m_plcStart);
			for (int ii = 0; ii < PCMaxCount; ii++)
				theApp.m_VisionSocketManager[ii].SocketSendto(ii, sendMsg, MC_STATE);
			break;
		case VS_STATE:
			theApp.m_VisionPCStatus[Num] = m_strContents == _T("0") ? FALSE : TRUE;
			LogWrite(CStringSupport::FormatString(_T("[VS %d -> MC] %s->%s"), Num, _T("RCV : VS_STATE"), theApp.m_VisionPCStatus[Num] == TRUE ? _T("Start") : _T("Stop")), Num);
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

				if (theApp.m_ChangeModelVision1)
				{
					sendMsg.Format(_T("%d,%s"), MC_MODEL_CHANGE, theApp.m_CurrentModel.m_AlignPcCurrentModelName);
					theApp.m_VisionSocketManager[PC1].SocketSendto(PC1, sendMsg, MC_MODEL_CHANGE);
				}
			}
			else
			{
				theApp.m_CreateModelVision2 = FALSE;
				theApp.m_PlcThread->ModelCreateChangeModify(_T("ModelCreate"), _T("Vision2"), theApp.m_CreateModelVision2);

				if (theApp.m_ChangeModelVision2)
				{
					sendMsg.Format(_T("%d,%s"), MC_MODEL_CHANGE, theApp.m_CurrentModel.m_AlignPcCurrentModelName);
					theApp.m_VisionSocketManager[PC2].SocketSendto(PC2, sendMsg, MC_MODEL_CHANGE);
				}
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
void CVisionThread::SendICWStartMessage()
{
#if _SYSTEM_AMTAFT_
	// 最多4个治具，固定最大数量
	const int MAX_JIG = 4;
	CString strCurrentJigs = _T("");   // 当前检测的治具
	CString strMaxJigs = _T("");        // 最大治具数量

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
		}
		else
		{
			strCurrentJigs += _T("00");
		}

		// 最大治具固定为 01,02,03,04
		strMaxJigs += CStringSupport::FormatString(_T("%02d"), i + 1);
	}

	// 组装 Start$ 消息
	CString strStartMsg;
	strStartMsg.Format(_T("Start$%s$%s@"), strCurrentJigs, strMaxJigs);

	// 发送 ICW Start$ 消息
	theApp.m_ICWCommManager.SendMessage(strStartMsg);

	LogWrite(CStringSupport::FormatString(_T("[ICW] Send Start$: %s"), strStartMsg), 0);
#endif
}

void CVisionThread::VisionInspectionMethod(int Num, int panelNum)
{
	if (theApp.m_CurrentIndexZone < 0)
	{
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
		theApp.m_pEqIf->m_pMNetH->GetPanelData(eWordType_VisionPanel1 + panelNum, &pPanelData);
		strPanel = CStringSupport::ToWString(pPanelData.m_PanelData, sizeof(pPanelData.m_PanelData));

		theApp.m_pEqIf->m_pMNetH->GetFpcIdData(eWordType_VisionFpcID1 + panelNum, &pFpcData);
		strFpcID = CStringSupport::ToWString(pFpcData.m_FpcIDData, sizeof(pFpcData.m_FpcIDData));
	}

	if (strPanel.IsEmpty())
		strPanel = strFpcID;

	if (strFpcID.IsEmpty())
	{
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
	if (GetDBInterface().IsConnected())
	{
		if (!GetDBInterface().UpsertIDMapBeforeStart(strMarkID, panelNum, strUniqueID, strPanel, strMarkID))
			LogWrite(CStringSupport::FormatString(_T("Panel %d UpsertIDMap failed: %s"), panelNum, GetDBInterface().GetLastError()), Num);
	}

#if _SYSTEM_AMTAFT_
	// 发送 ICW Start$ 消息给点灯检系统（仅在第一个治具时发送一次）
	if (panelNum == 0 && !m_bICWStartSent[0])
	{
		SendICWStartMessage();
		m_bICWStartSent[0] = TRUE;
	}
#endif

	LogWrite(CStringSupport::FormatString(_T("%s Panel %d [%s][%s] Vision Grab Start"), PG_IndexName[iCurIndex], Num, strPanel, strFpcID), Num);

	BOOL bFlag = VisionVecAdd(strPanel, strFpcID, panelNum, indexPanelNum, Num, iCurIndex, strUniqueID);
	if (bFlag)
	{
		sendMsg.Format(_T("%d,%s,%d,%s,%s"), MC_INSPECTION_START, strPanel, indexPanelNum, strProcessID, strFpcID);
		SocketSendto(Num, sendMsg, MC_INSPECTION_START);
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

	sendMsg.Format(_T("%d,%s,%s"), MC_GRAB_END_RECEIVE, strPanelID, strFpcID);
	SocketSendto(Num, sendMsg, MC_GRAB_END_RECEIVE);
	
	for (auto &InspResult : theApp.m_lastInspResultVec)
	{
		if (!InspResult.m_cellId.CompareNoCase(strPanelID))
		{
			if (InspResult.m_bInspStart == TRUE)
			{
				LogWrite(CStringSupport::FormatString(_T("Panel [%s] Vision Grab End"), strPanelID), Num);
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

	sendMsg.Format(_T("%d,%s,%s"), MC_INSPECTION_RESULT_RECEIVE, strPanelID, strFpcID);
	SocketSendto(Num, sendMsg, MC_INSPECTION_RESULT_RECEIVE);

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

void CVisionThread::LogWrite(CString strContents,int Num)
{
	if (theApp.m_bExitFlag == FALSE)
		return;

	g_DlgMainView->m_VisionListBox[Num].InsertString(0, CStringSupport::FormatString(_T("[%s] %s"), GetNowSystemTimeMilliseconds(), strContents));
	theApp.m_VisionLog->LOG_INFO(strContents);
}

void CVisionThread::VisionPLCResult(int Num, int iPanelNum, CString ResultMsg, int ResultCode, CString strPanelID, int iSendNGBuffer)
{
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
		

	theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_SendNgBufferResult1 + iPanelNum, &iSendNGBuffer);
	theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_VisionResult1 + iPanelNum, &ResultCode);
	theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionGrabEnd1 + iPanelNum, OffSet_0, TRUE);
	theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionEnd1 + iPanelNum, OffSet_0, TRUE);

	// ?��???��? ICW (FN$...@)
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

// ICW????????????
// ICW ??? Start$...@ ?? {"Action":"Start",...} ????
void CVisionThread::OnICWStart(const ICW_StartInfo& startInfo)
{
	LogWrite(CStringSupport::FormatString(_T("[ICW] Received Start - JigNumber: %d, Products: %d"),
		startInfo.JigNumber, startInfo.Products.size()), 0);

	// ?????????????????????????????
	// JigNumber: 1=Panel1, 2=Panel2, 3=Panel3, 4=Panel4
	for (const auto& product : startInfo.Products)
	{
		int iPanelNum = product.Position - 1;  // ??? 0-based ????
		if (iPanelNum >= 0 && iPanelNum < 4)
		{
			int iPcNum = iPanelNum <= 1 ? PC1 : PC2;
			VisionInspectionMethod(iPcNum, iPanelNum);
		}
	}
}

// ICW ??? SnapFN@ ??????????????
void CVisionThread::OnICWSnapFN()
{
	LogWrite(_T("[ICW] Received SnapFN@ - Image Grab Complete"), 0);

#if _SYSTEM_AMTAFT_
	// 重置 ICW Start$ 发送标志，允许下次检测时重新发送
	for (int i = 0; i < 4; i++)
		m_bICWStartSent[i] = FALSE;
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

#endif