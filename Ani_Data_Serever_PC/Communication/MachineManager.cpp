//>> 210422
#include "stdafx.h"
#include "MachineManager.h"
#include "Ani_Data_Serever_PC.h"

CMachineManager::CMachineManager(int iPcNum)
{
	m_iPcNum = iPcNum;
}

CMachineManager::~CMachineManager()
{
}

bool CMachineManager::ConnectClient(bool bUpper, CString strServerIP, CString strServerPort)
{
	m_bMelsecSimulaion = true;
	return ConnectTo(strServerIP, strServerPort, AF_INET, SOCK_STREAM);
}

void CMachineManager::SendMachineMessage(CString strMsg, int iPcNum, int iCommand)
{
	int iNum = iPcNum;

	int iLength = strMsg.GetLength();
	CString strLength = CStringSupport::FormatString(_T("%d"), iLength);

	int iTest = String2Hex(strLength);
	CString strCommand = CStringSupport::FormatString(_T("%c%c0x%s%s%c"), _STX, _DEVICEID, Hex2String(String2Hex(strLength)), strMsg, _ETX);
	char *lpCommand = StringToChar(strCommand);
	
	
	//theApp.m_pComView->m_SocketClient[iNum]->WriteComm((BYTE*)lpCommand, strlen(lpCommand), 100L);
	delete lpCommand;

	theApp.m_pCodeLog[iPcNum]->LOG_INFO(CStringSupport::FormatString(_T("[Relay -> MesAdapter] ->%s"), strMsg));
}

void CMachineManager::LogMessage(CString strContents)
{
	//theApp.m_pComView->m_SEND_MSG_LIST.InsertString(0, CStringSupport::FormatString(_T("[%s] %s"), GetNowSystemTimeMilliseconds(), strContents));
	//theApp.m_pOpvLog->LOG_INFO(strContents);
}

void CMachineManager::OnDataReceived(const LPBYTE lpBuffer, DWORD dwCount)
{
	CString strData, strHeader, strCommand, strContents, strParsing, strSendMsg;
	int iFind, iFindSTX;
	MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<LPCSTR>(lpBuffer), dwCount, strData.GetBuffer(dwCount + 1), dwCount + 1);
	strData.ReleaseBuffer(dwCount);

	CStringArray responseTokens, responseTokens2;
	strData.Replace(_T("\x3"), _T(""));
	CStringSupport::GetTokenArray(strData, _T(','), responseTokens);
	theApp.m_pTestLog->LOG_DEBUG(_T("(MES Test) MES -> PC, %s, "), strData);
	int isize = responseTokens.GetSize();
	if (responseTokens.GetSize() == 1)
	{
		//OpvLogMessage(_T("ETX No Message!!!"));
		return;
	}

	theApp.m_PgLog->LOG_INFO(strData);


	if (responseTokens[0] == _T("ALIVE")) //VS_ARE_YOU_THERE
	{
		CString sendMsg = CStringSupport::FormatString(_T("ALIVE"));
		//theApp.m_pComView->m_SocketClient[0]->SendMachineMessage(sendMsg, FALSE, FALSE);
	}
	else
	{
		//>>210126 MesAdapter 결과 받아서 진행할 부분
		if (!responseTokens[MesASeq_PGCode].CompareNoCase(_T("PGCode")))
		{
			CStringSupport::GetTokenArray(responseTokens[MesASeq_PGCodeList], _T(';'), responseTokens2);
			for (auto &MesPgCodes : theApp.m_VecPGCode_Mes)
			{
				if (!MesPgCodes.m_cellId.CompareNoCase(responseTokens[MesASeq_PanelID]))
				{
					for (int i = 0; i < responseTokens2.GetSize(); i++)
					{
						MesPgCodes.m_PGCode[i] = responseTokens2[i];
						theApp.m_bMesReturn = TRUE;
						theApp.m_pTestLog->LOG_DEBUG(_T("(MES Test) MesPgCodes[%d] %s, "), i, MesPgCodes.m_PGCode[i]);
					}
				}
			}
		}
	}
}

void CMachineManager::OnEvent(UINT uEvent, LPVOID lpvData)
{
	SockAddrIn addrin;
	GetSockName(addrin);

	switch (uEvent)
	{
	case EVT_CONDROP:
		//OpvLogMessage(CStringSupport::FormatString(_T("PanelNum %d OPV Connect Drop"), m_iOpvNum + 1));
		//theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageA_OPVLoginOut + m_iOpvNum, OffSet_0, FALSE);
		break;
	case EVT_CONSUCCESS:
		//OpvLogMessage(CStringSupport::FormatString(_T("PanelNum %d OPV Connect Success"), Num + 1));
		break;
	case EVT_ZEROLENGTH:
		//OpvLogMessage(CStringSupport::FormatString(_T("PanelNum %d OPV EVT_ZEROLENGTH"), Num + 1));
		break;
	case EVT_CONFAILURE:
		//OpvLogMessage(CStringSupport::FormatString(_T("PanelNum %d OPV EVT_CONFAILURE"), Num + 1));
		break;
	default:
		//OpvLogMessage(CStringSupport::FormatString(_T("PanelNum %d Unknown Socket event"), Num + 1));
		break;
	}
}

//<<