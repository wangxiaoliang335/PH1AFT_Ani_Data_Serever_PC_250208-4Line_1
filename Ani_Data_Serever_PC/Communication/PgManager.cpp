
#include "stdafx.h"
#if _SYSTEM_AMTAFT_
#include "DlgMainView.h"
#include "DlgMainLog.h"
#else
#include "DlgGammaMain.h"
#endif
#include "PgManager.h"

// ////////////////////////////////////////////////////////////////////////////
// CPgManager
//
// PG Socket 通信管理类：
//   - 作为 PG Server 端监听 PG 连接（`SocketServerOpen`）
//   - 发送控制命令到 PG（`SendPGMessage`）
//   - 解析 PG 的返回结果（`OnDataReceived`）
//
// 主要业务场景（AOI / ULD / Gamma）：
//   - AOI 自动生产：通过 PG 做 CONTACT ON/OFF、PreGamma、GET_RECIPE 等
//   - ULD 手动台：PG 用于手动接触/预 Gamma 等（通道 17~18）
//   - GAMMA 机种：使用 GAMMA / PID 等命令做亮度 / 均匀性 / PID 校正
//
// 收到 PG 返回帧的总入口为 `OnDataReceived`，根据系统宏拆分为：
//   - `_SYSTEM_AMTAFT_` 定义时：
//       AOIDataReceived()  : 生产 AOI 线 PG 结果
//       ULDDataReceived()  : 手动台 PG 结果
//   - 否则：
//       GammaDataReceived(): Gamma 设备 PG 结果
// ////////////////////////////////////////////////////////////////////////////

CPgManager::CPgManager()
{
#if _SYSTEM_AMTAFT_
	m_lastContent.resize(18);
	m_lastResult.resize(18);
	m_lastCommand.resize(18);
	m_lastRequest.resize(18);
	m_lastGammaL.resize(18);
	m_lastGammaX.resize(18);
	m_lastGammaY.resize(18);
#endif
}

CPgManager::~CPgManager()
{
}

void CPgManager::SendPGMessage(CString strMsg, int iChNum, int iStageNum)
{
	m_csSocketSend.Lock();

	CString strAMsg, strSendMsg;

	strAMsg = strMsg;

	if (theApp.m_iMachineType == SetAMT)
	{
		CStringArray responseTokens;
		CStringSupport::GetTokenArray(strAMsg, _T(','), responseTokens);
		int iSize = responseTokens.GetSize();
		CString strPacket[5];
	
		for (int ii = 0; ii < iSize; ii++)
			strPacket[ii] = responseTokens[ii];
	
		// PG 原始业务文本帧格式说明（逗号分隔）：
		//   Ch,<通道号>,PREGAMMA,START,<CELLID>           // 预 Gamma 开始
		//   Ch,<通道号>,PTRN,<PatternNumber>             // 切换 Pattern
		//   Ch,<通道号>,KEY,BACK                         // 按键：上一 Pattern
		//   Ch,<通道号>,KEY,NEXT                         // 按键：下一 Pattern
		//   Ch,<通道号>,CONTACTOFF,<CELLID>              // 接触 OFF
		//   Ch,<通道号>,CONTACT,<CELLID>                 // 接触 ON
		//   Ch,<通道号>,TURNOFF                          // PG 模块电源 OFF
		//   Ch,<通道号>,TURNON                           // PG 模块电源 ON
		if (!strPacket[1].Compare(_T("18")))
		{
			strPacket[1] = _T("21");
			if (!strPacket[2].CompareNoCase(_T("PREGAMMA")))
				strAMsg.Format(_T("%s,%s,%s,%s,%s"), strPacket[0], strPacket[1], strPacket[2], strPacket[3], strPacket[4]);
			else if (!strPacket[2].CompareNoCase(_T("KEY")) || !strPacket[2].CompareNoCase(_T("PTRN")) || 
				!strPacket[2].CompareNoCase(_T("CONTACTOFF")) || !strPacket[2].CompareNoCase(_T("CONTACT")))
				strAMsg.Format(_T("%s,%s,%s,%s"), strPacket[0], strPacket[1], strPacket[2], strPacket[3]);
			else
				strAMsg.Format(_T("%s,%s,%s"), strPacket[0], strPacket[1], strPacket[2]);
		}
	}

	// 组装最终发送报文：
	//   [STX][DEST][LEN(4位16进制)][内容(ASCII)][ETX]
	//   - LEN 为 strAMsg 长度，16 进制 4 位宽
	int iLen = strAMsg.GetLength();
	int iNum = iChNum - 1;
	strSendMsg.Format(_T("%c%c%04X%s%c"), _STX, _DEST, iLen, strAMsg, _ETX);

	char *lpCommand = StringToChar(strSendMsg);
	theApp.m_PgSocketManager[m_iPcNum].WriteComm((BYTE*)lpCommand, strlen(lpCommand), 100L);
#if _SYSTEM_AMTAFT_
	m_lastContent[iNum] = strMsg;
#else
	m_lastContent[iStageNum][iChNum] = strMsg;
#endif
	delete lpCommand;

	theApp.m_PgSendReceiverLog->Info(CStringSupport::FormatString(_T("[%s] [MC -> PG] %s"), GetNowSystemTimeMilliseconds(), strSendMsg));
	m_csSocketSend.Unlock();
}

void CPgManager::PgLogMessage(CString strContents)
{
	if (theApp.m_bExitFlag == FALSE)
		return;

	m_csSocketSend.Lock();

	g_MainLog->m_PgListBox.InsertString(0, CStringSupport::FormatString(_T("[%s] %s"), GetNowSystemTimeMilliseconds(), strContents));
	theApp.m_PgLog->Info(strContents);

	m_csSocketSend.Unlock();
}

////////////////////////////////////////////////////////////////////////////////
// PG 时序总览（培训用说明）：
//
//   PG(Module) <-> 本机(MC) <-> PLC <-> Flow 线程 <-> DFS
//
//   1) MC → PG：通过 `SendPGMessage` 发送业务命令
//      - Ch,<通道>,CONTACT,<CELLID>      // 接触 ON
//      - Ch,<通道>,PREGAMMA,START,<ID>   // 预 Gamma
//      - Ch,<通道>,KEY,RESET             // 接触 OFF
//   2) PG → MC：PG 完成后回传 DONE 帧（本函数 `OnDataReceived` 为统一入口）。
//   3) MC ：在 `OnDataReceived` 中拆 STX/ETX + 头信息，得到业务内容：
//         "Ch,Number,DONE,CONTACT,END,GOOD/NG,..."
//   4) AOI 线：调用 `AOIDataReceived`，
//      - Contact / PreGamma 结果写 PLC Word：
//          eWordType_AZoneContactOnResult / eWordType_PreGammaResultX / eWordType_PGCodeChXResult 等
//      - 完成信号写 PLC Bit：
//          eBitType_AZoneContactOnEnd / eBitType_PreGammaEnd 等
//   5) Flow 线程：轮询上述 Bit/Word，汇总为 Panel 级别结果，最终生成 DFS 区结构 `DfsData`。
//   6) DFS：`CPlcThread::SumDFSDataStart` 从 `DfsData` 读出 PG 相关字段（Contact/PreGamma/PGCode 等），
//           通过 `CDFSClient::DfsAddTransferFile` 交给 DFS 上传线程，生成 SUM/INDEX 文件。
//
//   简略 ASCII 流程（单片）：
//     MC::SendPGMessage()
//         │
//         ▼
//     PG 硬件执行 → DONE 帧
//         │
//         ▼
//     CPgManager::OnDataReceived()
//         ├─ AOIDataReceived() / ULDDataReceived() / GammaDataReceived()
//         │    ├─ 写 PG 结果到 PLC Word (Contact/PreGamma/PGCode...)
//         │    └─ 置 END Bit (ContactOnEnd/PreGammaEnd...)
//         └─ 更新内部结构 theApp.m_lastIndexPgVec 等
//
//     Flow 线程
//         ├─ 轮询 END Bit → 读取结果 Word
//         ├─ 生成 DFS 区 `DfsData`
//         └─ 调用 SumDFSDataStart() → DfsAddTransferFile() → DFS FTP 上传
////////////////////////////////////////////////////////////////////////////////
void CPgManager::OnDataReceived(const LPBYTE lpBuffer, DWORD dwCount)
{
	if (theApp.m_bExitFlag == FALSE)
		return;

	CString strData;
	MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<LPCSTR>(lpBuffer), dwCount, strData.GetBuffer(dwCount + 1), dwCount + 1);
	strData.ReleaseBuffer(dwCount);

	CStringArray responseTokens;
	CString m_strContents, m_strHeader, strParsing;
	// PG 返回数据可能一次粘在一起，使用 ETX 分隔后逐条解析
	CStringSupport::GetTokenArray(strData, _ETX, responseTokens);

	theApp.m_PgSendReceiverLog->Info(CStringSupport::FormatString(_T("[%s] [PG -> MC] %s"), GetNowSystemTimeMilliseconds(), strData));

	if (responseTokens.GetSize() == 1)
	{
		// 没有找到 ETX，认为报文不完整
		PgLogMessage(_T("ETX Message No !!!!"));
		return;
	}

	for (int ii = 0; ii < responseTokens.GetSize() - 1; ii++)
	{
		strParsing = responseTokens[ii];
		// 检查帧头 STX
		m_strHeader.Format(_T("%x"), strParsing.GetAt(0));
		UINT iHeader = (UINT)_ttoi(m_strHeader);
		if (iHeader != _STX || strParsing.GetAt(0) == ',')
		{
			PgLogMessage(_T("STX Message No !!!!"));
			continue;
		}

		// 去掉 STX、DEST、长度字段，只保留业务部分：
		//   [STX][DEST][LEN][Ch,Number,XXXX,....,RESULT]
		// 查找第一个逗号位置，减去前面 2Byte 头，截取后面的业务字符串
		int iFind = strParsing.Find(',') - 2;
		m_strContents = strParsing.Mid(iFind, strParsing.GetLength());

		m_csPgData.Lock();
#if _SYSTEM_AMTAFT_
		if (m_iPcNum == PgServer_1)
			AOIDataReceived(m_strContents);
		else
			ULDDataReceived(m_strContents);
#else
		GammaDataReceived(m_strContents);
#endif
		m_csPgData.Unlock();
	}

}

#if _SYSTEM_AMTAFT_
void CPgManager::AOIDataReceived(CString strContents)
{
	// AOI 自动线 PG 返回格式（典型）：
	//   Ch,<通道号>,DONE,CONTACT,END,<GOOD/NG>[,VBAT,VDDI,VCI,PGCODE]
	//   Ch,<通道号>,DONE,KEY,RESET,END,<GOOD/NG>
	//   Ch,<通道号>,DONE,PREGAMMA,END,<TEST名>,<GOOD/NG>
	//   Ch,<通道号>,DONE,CONTACTOFF,END,<GOOD/NG>
	//   Ch,<通道号>,DONE,GET_RECIPE,END,<RECIPENAME>
	// 其中：
	//   - responseTokens[1] : 实际 PG 通道号（1~16）
	//   - responseTokens[3] : 命令类型（CONTACT / KEY / PREGAMMA / CONTACTOFF / GET_RECIPE）
	//   - responseTokens[4] : 子命令或状态（END / RESET / NEXT / BACK）
	//   - responseTokens[5+] : 结果 GOOD/NG、PanelID、PG Code 等
	// 本函数负责将结果写入 PLC，并更新 `theApp.m_lastIndexPgVec` / Rank / DFS 等信息。
	CStringArray responseTokens;
	int iPanelNum, iIndexNum, iPanelCheal, iChNum, iPgOrderNum = 0;
	CStringSupport::GetTokenArray(strContents, _T(','), responseTokens);
	CString strPanelID;
	CString strPreGammaResult;

	if (responseTokens.GetSize() < 2)
	{
		PgLogMessage(CStringSupport::FormatString(_T("PG Response TokenCount=%d < 2, Raw=[%s]"),
			responseTokens.GetSize(), strContents));
		return;
	}

	if (responseTokens[0].CompareNoCase(_T("ch")))
	{
		PgLogMessage(_T("PG  Response Error !!!!"));
		return;
	}

	iPanelCheal = _ttoi(responseTokens[1]);	//ÆÐ³ÎÀÇ ¹øÈ£			//1ch ~ 16 ch
	iChNum = iPanelCheal - 1;				//Ã¤³ÎÀÇ º¯¼ö ¹øÈ£   ch0~ ch15
	iPanelNum = iChNum % 4;		//ÆÐ³ÎÀÇ Ã¤³Î¹øÈ£		//ex)AZone 0ch , 1ch, 2ch 3ch

	if (iPanelNum == 3)
		iIndexNum = (iPanelCheal / MaxZone) - 1;	//ÆÐ³ÎÀÇ index ¹øÈ£ Azone = 0 , Bzone = 1 , CZone = 2 , DZone = 3
	else
		iIndexNum = (iPanelCheal / MaxZone);

	if (responseTokens.GetSize() < 5)
	{
		PgLogMessage(CStringSupport::FormatString(_T("PG Response TokenCount=%d < 5, Raw=[%s]"),
			responseTokens.GetSize(), strContents));
		return;
	}

	if (responseTokens[3] == _T("CONTACT") || responseTokens[3] == _T("GET_RECIPE")) // Ch,Number,DONE,KEY,RESET
		iPgOrderNum = PG_CONTACT_ON;
	else if (responseTokens[3] == _T("PREGAMMA"))
		iPgOrderNum = PG_PREGAMMA;
	else if (responseTokens[3] == _T("KEY"))
	{
		if (responseTokens[4] == _T("RESET"))
			iPgOrderNum = PG_CONTACT_OFF;
		//else if (responseTokens[4] == _T("NEXT"))
		//	iPgOrderNum = PG_PATTERN_NEXT;
		//if (responseTokens[4] == _T("BACK"))
		//	iPgOrderNum = PG_PATTERN_BACK;
	}
	else if (responseTokens[3] == _T("CONTACTOFF"))
		iPgOrderNum = PG_CONTACT_OFF;

	//Ch,2,CONTACT,END,GOOD
	CString strIndexName = PG_IndexName[iIndexNum];
	m_lastRequest[iChNum] = strContents;
	int iCheckErr2(PGCode_OK); // 0: GOOD, 1: NGCode PG, Mes 안맞음, 2: TimeOver
	
	for (auto &InspResult : theApp.m_lastIndexPgVec[iIndexNum])
	{
		if (InspResult.m_iStatus == iPgOrderNum && InspResult.m_iIndexPanelNum == iPanelCheal) 
		{
			if (responseTokens[3] == _T("CONTACT"))
			{
				//Ch,Number,DONE,CONTACT,END,GOOD
				if (responseTokens[4] == _T("END"))
				{
					if (responseTokens[5] == _T("GOOD"))
					{
						theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_AZoneContactOnResult + iIndexNum, iPanelNum, &m_codeOk);
						PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Panel [%s] Contact On Success"), strIndexName, iPanelCheal, InspResult.m_cellId));
						InspResult.time_check.StopTimer();
						InspResult.m_bResult = TRUE;
						theApp.m_bContact[iChNum] = FALSE;
						theApp.m_strContactPanelID[iChNum] = _T("");
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AZoneContactOnEnd + iIndexNum, iPanelNum, TRUE);
					}
					else
					{
						if (theApp.m_bContact[iChNum] == TRUE && _ttoi(theApp.m_strPGName) == PG_MuhanZC)
						{
							InspResult.time_check.StopTimer();
							InspResult.m_bResult = TRUE;
							theApp.m_PgInexThread[iIndexNum]->PgVecAdd(theApp.m_strContactPanelID[iChNum], theApp.m_strContactPanelID[iChNum], iPanelNum, iIndexNum, PG_CONTACT_OFF, PGContactOffTimer, iPanelCheal);
							PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Panel [%s] Contact Off ReStart"), strIndexName, iPanelCheal, InspResult.m_cellId));
							CString strMsg = CStringSupport::FormatString(_T("Ch,%d,KEY,RESET"), iPanelCheal);
							theApp.m_PgSocketManager[PgServer_1].SendPGMessage(strMsg, iPanelCheal);
							theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_AZone1stContactResult + iIndexNum, iPanelNum, &m_codeFail);
						}
						else
						{
							if (theApp.m_PgPassMode)
								theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_AZoneContactOnResult + iIndexNum, iPanelNum, &m_codeOk);
							else
								theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_AZoneContactOnResult + iIndexNum, iPanelNum, &m_codeFail);

							PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Panel [%s] Contact On Fail"), strIndexName, iPanelCheal, InspResult.m_cellId));
							InspResult.time_check.StopTimer();
							InspResult.m_bResult = TRUE;
							theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AZoneContactOnEnd + iIndexNum, iPanelNum, TRUE);
						}
						//>> 210301 yjlim
						theApp.m_pRankTread->AddRankCodeList(InspResult.m_cellId, InspResult.m_FpcID, InspResult.m_iPanelNum, InspResult.m_iCurIndex, RankContact);
						//<<
					}
					m_lastCommand[iChNum] = responseTokens[3];
					m_lastResult[iChNum] = responseTokens[5];

					if (responseTokens.GetSize() == 10)
					{
						PGDfsList PgList;
						PgList.strPanelID = InspResult.m_cellId;
						PgList.strFpcID = InspResult.m_FpcID;
						PgList.m_strVBIT = responseTokens[6];
						PgList.m_strVDDI = responseTokens[7];
						PgList.m_strVCI = responseTokens[8];
						PgList.m_strProgramVersion = responseTokens[9];
						theApp.PGDfsInfoSave(PgList);
					}
				}
				theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_PGCodeCh1Result + iIndexNum, iPanelNum, &iCheckErr2);
			}
			else if (responseTokens[3] == _T("KEY"))
			{
				//Ch,Number,DONE,KEY,NEXT
				//Ch,Number,DONE,KEY,RESET,END,GOOD
				//Ch,Number,DONE,KEY,RESET,END,NG
				if (responseTokens[4] == _T("RESET") && _ttoi(theApp.m_strPGName) == PG_MuhanZC)
				{
					if (responseTokens[6] == _T("GOOD"))
					{
						if (theApp.m_bContact[iChNum] == TRUE)
						{
							InspResult.time_check.StopTimer();
							InspResult.m_bResult = TRUE;
							theApp.m_PgInexThread[iIndexNum]->PgVecAdd(theApp.m_strContactPanelID[iChNum], theApp.m_strContactPanelID[iChNum], iPanelNum, iIndexNum, PG_CONTACT_ON, PGContactOnTimer, iPanelCheal);
							CString strMsg = CStringSupport::FormatString(_T("Ch,%d,CONTACT,%s"), iPanelCheal, theApp.m_strContactPanelID[iChNum]);
							theApp.m_PgSocketManager[PgServer_1].SendPGMessage(strMsg, iPanelCheal);
							PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Panel [%s] Contact On ReStart"), strIndexName, iPanelCheal, theApp.m_strContactPanelID[iChNum]));
							theApp.m_bContact[iChNum] = FALSE;
							theApp.m_strContactPanelID[iChNum] = _T("");
						}
						else
						{
							theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_AZoneContactOffResult + iIndexNum, iPanelNum, &m_codeOk);
							PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Contact Off Success"), strIndexName, iPanelCheal));
							InspResult.time_check.StopTimer();
							InspResult.m_bResult = TRUE;
							theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AZoneContactOffEnd + iIndexNum, iPanelNum, TRUE);
						}
					}
					else
					{
						theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_AZoneContactOffResult + iIndexNum, iPanelNum, &m_codeFail);
						PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Contact Off Fail"), strIndexName, iPanelCheal));
						InspResult.time_check.StopTimer();
						InspResult.m_bResult = TRUE;
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AZoneContactOffEnd + iIndexNum, iPanelNum, TRUE);
					}
				
				}
				/*else if (responseTokens[4] == _T("NEXT"))
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AZonePatternNextEnd + iIndexNum, IndexZone, TRUE);
					PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Pattern Next End"), strIndexName, iPanelCheal));
					InspResult.time_check.StopTimer();
					InspResult.m_bResult = TRUE;

				}
				else if (responseTokens[4] == _T("BACK"))
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AZonePatternBackEnd + iIndexNum, IndexZone, TRUE);
					PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Pattern Back End"), strIndexName, iPanelCheal));
					InspResult.time_check.StopTimer();
					InspResult.m_bResult = TRUE;
				}*/

				m_lastCommand[iChNum] = responseTokens[3];
				m_lastResult[iChNum] = responseTokens[4];
			}
			else if (responseTokens[3] == _T("PREGAMMA"))
			{
				if(responseTokens[4] == _T("END"))
				{
					if (responseTokens.GetCount() == 7) // 진짜 씨부랄...
					{
						strPanelID = responseTokens[5];
						strPreGammaResult = responseTokens[6];
					}
					else
						strPreGammaResult = responseTokens[5];

					//Ch,Number,DONE,PREGAMMA,END,TEST0,GOOD
					//Ch,Number,DONE,PREGAMMA,END,TEST0,NG
					if (strPreGammaResult == _T("GOOD"))
					{                                    
						theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_PreGammaResult1, iPanelNum, &m_codeOk);
						PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Panel [%s] Gamma Success"), strIndexName, iPanelCheal, InspResult.m_cellId));
						InspResult.time_check.StopTimer();
						InspResult.m_bResult = TRUE;
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_PreGammaEnd1, iPanelNum, TRUE);
					}
					else
					{
						if (theApp.m_PgPassMode)
							theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_PreGammaResult1, iPanelNum, &m_codeOk);
						else
							theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_PreGammaResult1, iPanelNum, &m_codeFail);

						PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Panel [%s] Gamma Fail"), strIndexName, iPanelCheal, InspResult.m_cellId));
						InspResult.time_check.StopTimer();
						InspResult.m_bResult = TRUE;
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_PreGammaEnd1, iPanelNum, TRUE);
						if (!theApp.m_PgPassMode){
							CString strMsg;
							strMsg = CStringSupport::FormatString(_T("%d,%d"), MC_NG_PANEL, _PREGAMMA);
							theApp.m_OpvSocketManager[0].SendOpvMessage(strMsg, 0, MC_NG_PANEL); 
							theApp.m_OpvSocketManager[1].SendOpvMessage(strMsg, 1, MC_NG_PANEL);
						}
						
					}

					m_lastCommand[iChNum] = responseTokens[3];
					m_lastResult[iChNum] = strPreGammaResult;
				}
				CString strMsg = CStringSupport::FormatString(_T("Ch,%d,PTRN,3"), iChNum + 1);
				theApp.m_PgSocketManager[PgServer_1].SendPGMessage(strMsg, iChNum + 1);
			}
			else if (responseTokens[3] == _T("CONTACTOFF")) // Ch, Number, DONE, KEY, RESET, END, GOOD == > Ch, Number, DONE, CONTACT OFF, END, GOOD
			{
				if (responseTokens[4] == _T("END"))
				{
					theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_AZoneContactOffResult + iIndexNum, iPanelNum, &m_codeOk);
					PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Contact Off Success"), strIndexName, iPanelCheal));
					//>> 210301 yjlim 해당 부분에 Index내의 모든 Code Data 읽어들여서 분류 하는 부분 넣자..(이 위치가 아니더라도.. 물류방향 판정할 부분으로 나중에 옮기면 됌..)

					theApp.LoadResultIndexCode(InspResult.m_cellId, InspResult.m_FpcID);
					int iSendNGBuffer(Flow_AfterMachine);
					//theApp.m_pTestLog->Info(_T("m_FlowResultDatas.size() : %d, panel id : %s"), theApp.m_FlowResultDatas.size(), InspResult.m_cellId);
					//for (auto& flowData : theApp.m_FlowResultDatas)
					//{
						//theApp.m_pTestLog->Info(_T("[FlowResultDatas] Key(strGrade): [%s], Value(strCode): [%s]"), flowData.first, flowData.second);
					//}
					if (theApp.m_FlowResultDatas.size() > 0)
					{
						for (auto Grades : theApp.m_VecGradeFlow)
						{
							// m_FlowResultDatas  -> strGrade, strCode;
							// theApp.m_GradeFlow -> strGrade, iFlow; // iFlow -> 1 : Flow_AfterMachine, 2 : Flow_Operator
							map<CString, CString>::iterator iter;

							iter = theApp.m_FlowResultDatas.find(Grades.strGrade);
							if (iter != theApp.m_FlowResultDatas.end())
							{
								//
								//>> psh 0414
								//theApp.m_pTestLog->Info(_T("m_FlowResultDatas find panel id : %s"), InspResult.m_cellId);
								if (theApp.m_strEqpId == "MFGAP" || theApp.m_strMachineType == "AFT")
								{
									//theApp.m_pTestLog->Info(_T("eqpid %s machinetype %s panel id : %s"), theApp.m_strEqpId, theApp.m_strMachineType, InspResult.m_cellId);
									if (Grades.iFlow == Flow_Operator)
									{
										iSendNGBuffer = Grades.iFlow;
										//theApp.m_pTestLog->Info(_T("iSendNGBuffer %d panel id : %s"), iSendNGBuffer, InspResult.m_cellId);
									}
								}
								else
								{
									iSendNGBuffer = Flow_Operator;
								}
							}
						}
					}
					//for (auto saveLogs : theApp.m_FlowResultDatas)
					//{
						//theApp.m_pTestLog->Info(_T("Flows Data : %s, %s, Panel ID : %s,"), saveLogs.first, saveLogs.second, InspResult.m_cellId);
					//}

					//iSendNGBuffer = 2;   //test

					//theApp.m_pTestLog->Info(_T("Flows Data Final: %s, Panel ID : %s,"), iSendNGBuffer == Flow_AfterMachine ? _T("OK Flow") : _T("NG Flow"), InspResult.m_cellId);
					theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_AllZonePos1DirectionResult + iPanelNum, &iSendNGBuffer); /* 1 : go OK, 2 : go NG Buff*/
					theApp.m_FlowResultDatas.clear();
					//<< 
					InspResult.time_check.StopTimer();
					InspResult.m_bResult = TRUE;
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AZoneContactOffEnd + iIndexNum, iPanelNum, TRUE);
				}
				m_lastCommand[iChNum] = responseTokens[3];
				m_lastResult[iChNum] = responseTokens[5];
			}
			//>>210422 
			else if (responseTokens[3] == _T("GET_RECIPE")) // Ch, Number, DONE, GET_RECIPE, END, RECIPENAME
			{
				if (responseTokens[4] == _T("END"))
				{
					//>>0126 yjlim
					if (theApp.m_bPGCodeUsable == TRUE)
					{
						int iCheckErr(PGCode_NG); // 0: GOOD, 1: NGCode PG, Mes 안맞음, 2: TimeOver

						theApp.m_VecPGCode_PG[iChNum].m_PGCode[0] = responseTokens[5]; //ex : Ch,1,DONE,GET_RECIPE,END,RECIPENAME
						while (1)
						{
							if (theApp.m_VecPGCode_Mes[iChNum].time_check.IsTimeOver())
							{
								iCheckErr = PGCode_AckTimeout;
								break;
							}

							if (theApp.m_VecPGCode_Mes[iChNum].m_PGCode[0] != _T(""))
							{

								for (auto CodeList : theApp.m_VecPGCode_Mes[iChNum].m_PGCode)
								{
									if (CodeList == theApp.m_VecPGCode_PG[iChNum].m_PGCode[0])
										iCheckErr = PGCode_OK;									
									
									PgLogMessage(CStringSupport::FormatString(_T("(MES TEST)[%s] Ch %d Panel [%s] MesPGCode : %s, PG PGCode: %s,"),
										PG_IndexName[iIndexNum], iChNum, InspResult.m_cellId,
										CodeList, theApp.m_VecPGCode_PG[iChNum].m_PGCode[0]));
									//theApp.m_pTestLog->Debug(CStringSupport::FormatString(_T("(MES TEST)[%s] Ch %d Panel [%s] MesPGCode : %s, PG PGCode: %s,"),
									//	PG_IndexName[iIndexNum], iChNum, InspResult.m_cellId,
									//	CodeList, theApp.m_VecPGCode_PG[iChNum].m_PGCode[0]));
									if (iCheckErr == PGCode_OK)
										break;

								}
								break;
							}
							else
							{
								
							}
						}
						//>>여기 알람칠 내역 추가하고,, 
						theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_PGCodeCh1Result + iIndexNum, iPanelNum, &iCheckErr);
					}
				}
			}
			//<<
		}
	}
}

void CPgManager::ULDDataReceived(CString strContents)
{
	// ULD 手动台 PG 返回格式（通道一般为 17~18）：
	//   Ch,<通道号>,DONE,CONTACT,END,<GOOD/NG>
	//   Ch,<通道号>,DONE,KEY,RESET/ NEXT/ BACK,END,<GOOD/NG>
	//   Ch,<通道号>,DONE,PREGAMMA,END,<TEST名>,<GOOD/NG>
	//   Ch,<通道号>,DONE,CONTACTOFF,END,<GOOD/NG>
	// 其中：
	//   - responseTokens[1] : ULD PG 通道号（17/18），内部会换算回 1/2 面板索引
	//   - responseTokens[3] : 命令类型（CONTACT / KEY / PREGAMMA / CONTACTOFF）
	//   - responseTokens[4] : 子命令（RESET/NEXT/BACK/END）
	//   - responseTokens[5] : 结果 GOOD/NG
	// 本函数根据返回结果刷新 PLC 位、手动台队列 `m_VecManualStage` 以及重新发送 PG 命令。
	CStringArray responseTokens;
	int iPanelCheal, iChNum = 0, iPgOrderNum = 0;
	CStringSupport::GetTokenArray(strContents, _T(','), responseTokens);
	CString strPanelID;
	CString strPreGammaResult;

	if (responseTokens.GetSize() < 2)
	{
		PgLogMessage(CStringSupport::FormatString(_T("[ULD] TokenCount=%d < 2, Raw=[%s]"),
			responseTokens.GetSize(), strContents));
		return;
	}

	if (responseTokens[0].CompareNoCase(_T("ch")))
	{
		PgLogMessage(_T("[ULD] PG Response Error !!!!"));
		return;
	}

	if (theApp.m_iMachineType == SetAMT)
	{
		if (_ttoi(responseTokens[1]) == 21)
			responseTokens[1] = _T("18");
	}

	iPanelCheal = _ttoi(responseTokens[1]);	// ch17~18
	iChNum = iPanelCheal - 1;				

	int iPanelNum = (iChNum) % 2;

	if (responseTokens[3] == _T("CONTACT")) // Ch,Number,DONE,KEY,RESET
		iPgOrderNum = ManualStageContactOn;
	else if (responseTokens[3] == _T("PREGAMMA"))
		iPgOrderNum = ManualStagePreGamma;
	else if (responseTokens[3] == _T("KEY"))
	{
		if (responseTokens[4] == _T("RESET"))
			iPgOrderNum = ManualStageContactOff;
		if (responseTokens[4] == _T("NEXT"))
			iPgOrderNum = ManualStageNext;
		if (responseTokens[4] == _T("BACK"))
			iPgOrderNum = ManualStageBack;
	}
	else if (responseTokens[3] == _T("CONTACTOFF"))
		iPgOrderNum = ManualStageContactOff;

	//Ch,2,CONTACT,END,GOOD
	m_lastRequest[iChNum] = strContents;

	if (responseTokens[3] == _T("CONTACT"))
	{
		//Ch,Number,DONE,CONTACT,END,GOOD
		if (responseTokens[4] == _T("END"))
		{
			if (responseTokens[5] == _T("GOOD"))
			{
				theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_MStageAContactOnResult + iPanelNum, &m_codeOk);
				PgLogMessage(CStringSupport::FormatString(_T("Ch %d Panel [%s] Contact On Success"), iPanelCheal, theApp.m_VecManualStage[iPgOrderNum][iPanelNum].m_cellId));
				theApp.m_VecManualStage[iPgOrderNum][iPanelNum].time_check.StopTimer();
				theApp.m_VecManualStage[iPgOrderNum][iPanelNum].m_bResult = TRUE;
				theApp.m_bContact[iChNum] = FALSE;
				theApp.m_strContactPanelID[iChNum] = _T("");
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageAContactOnEnd + iPanelNum, OffSet_0, TRUE);
			}
			else
			{
				if (theApp.m_bContact[iChNum] == TRUE && _ttoi(theApp.m_strPGName) == PG_MuhanZC)
				{
					theApp.m_VecManualStage[iPgOrderNum][iPanelNum].time_check.StopTimer();
					theApp.m_VecManualStage[iPgOrderNum][iPanelNum].m_bResult = TRUE;
					theApp.m_ManualThread->ManualStageVecAdd(theApp.m_VecManualStage[iPgOrderNum][iPanelNum].m_cellId, theApp.m_VecManualStage[iPgOrderNum][iPanelNum].m_cellId, iChNum, ManualStageContactOff, PGContactOffTimer, m_iPcNum - 1);
					PgLogMessage(CStringSupport::FormatString(_T("Ch %d Panel [%s] Contact Off ReStart"), iPanelCheal, theApp.m_VecManualStage[iPgOrderNum][iPanelNum].m_cellId));
					CString strMsg = CStringSupport::FormatString(_T("Ch,%d,KEY,RESET"), iPanelCheal);
					theApp.m_PgSocketManager[m_iPcNum].SendPGMessage(strMsg, iPanelCheal);
					theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_MStageAContactResetResult + iChNum, OffSet_0, &m_codeFail);
				}
				else
				{
					if (theApp.m_PgPassMode)
						theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_MStageAContactOnResult + iPanelNum, &m_codeOk);
					else
						theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_MStageAContactOnResult + iPanelNum, &m_codeFail);

					PgLogMessage(CStringSupport::FormatString(_T("Ch %d Panel [%s] Contact On Fail"), iPanelCheal, theApp.m_VecManualStage[iPgOrderNum][iPanelNum].m_cellId));

					theApp.m_VecManualStage[iPgOrderNum][iPanelNum].time_check.StopTimer();
					theApp.m_VecManualStage[iPgOrderNum][iPanelNum].m_bResult = TRUE;
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageAContactOnEnd + iPanelNum, OffSet_0, TRUE);
				}
			}
			m_lastCommand[iChNum] = responseTokens[3];
			m_lastResult[iChNum] = responseTokens[5];
		}
	}
	else if (responseTokens[3] == _T("KEY"))
	{
		//Ch,Number,DONE,KEY,NEXT,END
		if (responseTokens[4] == _T("RESET") && _ttoi(theApp.m_strPGName) == PG_MuhanZC)
		{
			if (responseTokens[6] == _T("GOOD"))
			{
				if (theApp.m_bContact[iChNum] == TRUE)
				{
					theApp.m_VecManualStage[iPgOrderNum][iPanelNum].time_check.StopTimer();
					theApp.m_VecManualStage[iPgOrderNum][iPanelNum].m_bResult = TRUE;
					theApp.m_ManualThread->ManualStageVecAdd(theApp.m_strContactPanelID[iChNum], theApp.m_strContactPanelID[iChNum], iChNum, ManualStageContactOn, PGContactOnTimer, m_iPcNum - 1);
					CString strMsg = CStringSupport::FormatString(_T("Ch,%d,CONTACT,%s"), iPanelCheal, theApp.m_strContactPanelID[iChNum]);
					theApp.m_PgSocketManager[m_iPcNum].SendPGMessage(strMsg, iPanelCheal);
					PgLogMessage(CStringSupport::FormatString(_T("Ch %d Panel [%s] Contact On ReStart"), iPanelCheal, theApp.m_strContactPanelID[iChNum]));
					theApp.m_bContact[iChNum] = FALSE;
					theApp.m_strContactPanelID[iChNum] = _T("");
				}
				else
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_MStageAContactOffResult + iChNum, &m_codeOk);
					PgLogMessage(CStringSupport::FormatString(_T("Ch %d Contact Off Success"), iPanelCheal));
					theApp.m_VecManualStage[iPgOrderNum][iPanelNum].time_check.StopTimer();
					theApp.m_VecManualStage[iPgOrderNum][iPanelNum].m_bResult = TRUE;
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageAContactOffEnd + iChNum, OffSet_0, TRUE);
				}
			}
			else
			{
				theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_MStageAContactOffResult + iChNum, &m_codeFail);
				PgLogMessage(CStringSupport::FormatString(_T("Ch %d Contact Off Fail"), iPanelCheal));
				theApp.m_VecManualStage[iPgOrderNum][iPanelNum].time_check.StopTimer();
				theApp.m_VecManualStage[iPgOrderNum][iPanelNum].m_bResult = TRUE;
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageAContactOffEnd + iChNum, OffSet_0, TRUE);
		
			}
		
		}
		else if (responseTokens[4] == _T("NEXT"))
		{
			theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageAContactNextEnd + iPanelNum, OffSet_0, TRUE);
			PgLogMessage(CStringSupport::FormatString(_T("Ch %d Pattern Next End"), iPanelCheal));
			theApp.m_VecManualStage[iPgOrderNum][iPanelNum].time_check.StopTimer();
			theApp.m_VecManualStage[iPgOrderNum][iPanelNum].m_bResult = TRUE;

		}
		else if (responseTokens[4] == _T("BACK"))
		{
			theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageAContactBackEnd + iPanelNum, OffSet_0, TRUE);
			PgLogMessage(CStringSupport::FormatString(_T("Ch %d Pattern Back End"), iPanelCheal));
			theApp.m_VecManualStage[iPgOrderNum][iPanelNum].time_check.StopTimer();
			theApp.m_VecManualStage[iPgOrderNum][iPanelNum].m_bResult = TRUE;
		}

		m_lastCommand[iChNum] = responseTokens[4];
		m_lastResult[iChNum] = responseTokens[5];
	}
	else if (responseTokens[3] == _T("PREGAMMA"))
	{
		if (responseTokens[4] == _T("END"))	
		{
			if (responseTokens.GetCount() == 7) // 진짜 씨부랄...
			{
				strPanelID = responseTokens[5];
				strPreGammaResult = responseTokens[6];
			}
			else
				strPreGammaResult = responseTokens[5];
			//Ch,Number,DONE,PREGAMMA,END,TEST0,GOOD
			//Ch,Number,DONE,PREGAMMA,END,TEST0,NG,
			//Ch,Number,DONE,PREGAMMA,START,END,TEST0,NG,NG	이건 어디 프로토콜이야
			//if (responseTokens[6] == _T("GOOD"))
			if (strPreGammaResult == _T("GOOD"))
			{
				theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_MStageAGammaResult + iPanelNum, &m_codeOk);
				PgLogMessage(CStringSupport::FormatString(_T("Ch %d Panel [%s] Gamma Success"), iPanelCheal, theApp.m_VecManualStage[iPgOrderNum][iPanelNum].m_cellId));
				theApp.m_VecManualStage[iPgOrderNum][iPanelNum].time_check.StopTimer();
				theApp.m_VecManualStage[iPgOrderNum][iPanelNum].m_bResult = TRUE;
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageAPreGammaEnd + iPanelNum, OffSet_0, TRUE);
			}
			else
			{
				if (theApp.m_PgPassMode)
					theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_MStageAGammaResult + iPanelNum, &m_codeOk);
				else
					theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_MStageAGammaResult + iPanelNum, &m_codeFail);

				PgLogMessage(CStringSupport::FormatString(_T("Ch %d Panel [%s] Gamma Fail"), iPanelCheal, theApp.m_VecManualStage[iPgOrderNum][iPanelNum].m_cellId));
				theApp.m_VecManualStage[iPgOrderNum][iPanelNum].time_check.StopTimer();
				theApp.m_VecManualStage[iPgOrderNum][iPanelNum].m_bResult = TRUE;
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageAPreGammaEnd + iPanelNum, OffSet_0, TRUE);
			}

			m_lastCommand[iChNum] = responseTokens[3];
			m_lastResult[iChNum] = strPreGammaResult;
		}
	}
	else if (responseTokens[3] == _T("CONTACTOFF"))
	{
		if (responseTokens[4] == _T("END"))
		{
			theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_MStageAContactOffResult + iPanelNum, &m_codeOk);

			PgLogMessage(CStringSupport::FormatString(_T("Ch %d Contact Off Success"), iPanelCheal));
			theApp.m_VecManualStage[iPgOrderNum][iPanelNum].time_check.StopTimer();
			theApp.m_VecManualStage[iPgOrderNum][iPanelNum].m_bResult = TRUE;
			theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageAContactOffEnd + iPanelNum, OffSet_0, TRUE);
		}
		m_lastCommand[iChNum] = responseTokens[3];
		m_lastResult[iChNum] = responseTokens[5];
	}
}
#else
void CPgManager::GammaDataReceived(CString strContents)
{
	// Gamma 机种 PG 返回格式（典型）：
	//   Ch,<通道号>,DONE,CONTACT,END,<GOOD/NG>[,VBAT,VDDI,VCI,PGCODE]
	//   Ch,<通道号>,DONE,GAMMA,END,<GOOD/NG>,<Code>,<Grade>
	//   Ch,<通道号>,DONE,KEY,RESET/NEXT/BACK,END,<GOOD/NG>
	//   Ch,<通道号>,DONE,CONTACTOFF,END,<GOOD/NG>
	//   Ch,<通道号>,DONE,PID,END,<GOOD/NG>
	// 其中：
	//   - responseTokens[1] : PG 通道号（1~24）
	//   - responseTokens[3] : 命令类型（CONTACT / GAMMA / KEY / CONTACTOFF / PID）
	//   - responseTokens[4] : 子命令或状态（END / RESET / NEXT / BACK）
	//   - responseTokens[5] : 结果 GOOD/NG
	//   - responseTokens[6]/[7] : Gamma NG 时的 Code/Grade
	// 本函数会根据 Stage/Panel 索引，把结果写入到对应 PLC Word/Bit，并保存 DFS/缺陷信息。
	CStringArray responseTokens;
	int iPanelNum, iChNum, iStageNum;
	int iPgOrderNum = 0;
	CStringSupport::GetTokenArray(strContents, _T(','), responseTokens);

	if (responseTokens.GetSize() < 2)
	{
		PgLogMessage(CStringSupport::FormatString(_T("[Gamma] TokenCount=%d < 2, Raw=[%s]"),
			responseTokens.GetSize(), strContents));
		return;
	}

	if (responseTokens[0].CompareNoCase(_T("ch")))
	{
		PgLogMessage(_T("[Gamma] PG Response Error !!!!"));
		return;
	}

	iChNum = _ttoi(responseTokens[1]); // Ch 1 ~ 24

	iPanelNum = (iChNum - 1) % 2; // 존 별 채널

	if (iPanelNum == PanelNum1)
		iStageNum = iChNum / 2;
	else
		iStageNum = (iChNum / 2) - 1;

	if (responseTokens[3] == _T("CONTACT"))
		iPgOrderNum = PG_CONTACT_ON;
	else if (responseTokens[3] == _T("GAMMA"))
		iPgOrderNum = PG_GAMMA;
	else if (responseTokens[3] == _T("KEY"))
	{
		if (responseTokens[4] == _T("RESET")) 
			iPgOrderNum = PG_CONTACT_OFF;
		if (responseTokens[4] == _T("NEXT"))
			iPgOrderNum = PG_PATTERN_NEXT;
		if (responseTokens[4] == _T("BACK"))
			iPgOrderNum = PG_PATTERN_BACK;
	}
	else if (responseTokens[3] == _T("CONTACTOFF")) 
		iPgOrderNum = PG_CONTACT_OFF;
	else if (responseTokens[3] == _T("PID"))
		iPgOrderNum = PG_PID_CHECK;

	m_lastRequest[iStageNum][iPanelNum] = strContents;

	if (responseTokens[3] == _T("CONTACT")) // CH,NUM,DONE,CONTACT,END,RESULT
	{
		if (responseTokens[4] == _T("END"))
		{
			if (responseTokens[5] == _T("GOOD"))
			{
				theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_GammaStage1ContactOnResult + iStageNum, iPanelNum, &m_codeOk);

				PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Panel [%s] Contact On Success"), PG_IndexName[iStageNum], iChNum, theApp.m_lastGammaVec[iStageNum][iPanelNum].m_cellId));
				theApp.m_lastGammaVec[iStageNum][iPanelNum].time_check.StopTimer();
				theApp.m_bContact[iChNum - 1] = FALSE;
				theApp.m_strContactPanelID[iChNum - 1] = _T("");
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaContactOnEnd1 + iStageNum, OffSet_0, TRUE);

				if (responseTokens.GetSize() == 10)
				{
					PGDfsList PgList;
					PgList.strPanelID = theApp.m_lastGammaVec[iStageNum][iPanelNum].m_cellId;
					PgList.strFpcID = theApp.m_lastGammaVec[iStageNum][iPanelNum].m_FpcID;
					PgList.m_strVBIT = responseTokens[6];
					PgList.m_strVDDI = responseTokens[7];
					PgList.m_strVCI = responseTokens[8];
					PgList.m_strProgramVersion = responseTokens[9];
					theApp.GammaDfsInfoSave(PgList);

					m_csPgData.Lock();
					m_lastGammaVBAT[iStageNum][iPanelNum] = responseTokens[6];
					m_lastGammaVDDI[iStageNum][iPanelNum] = responseTokens[7];
					m_lastGammaVCI[iStageNum][iPanelNum] = responseTokens[8];
					m_lastGammaPGCODE[iStageNum][iPanelNum] = responseTokens[9];
					m_csPgData.Unlock();
				}
				else
					theApp.m_pTraceLog->Info(_T("**************** PanelID [%s] Contact On Protocol Error ****************"), theApp.m_lastGammaVec[iStageNum][iPanelNum].m_cellId);

				theApp.m_lastGammaVec[iStageNum][iPanelNum].m_bResult = TRUE;
			}
			else
			{
				if (theApp.m_bContact[iChNum - 1] == TRUE && _ttoi(theApp.m_strPGName) == PG_MuhanZC)
				{
					theApp.m_lastGammaVec[iStageNum][iPanelNum].time_check.StopTimer();
					theApp.m_lastGammaVec[iStageNum][iPanelNum].m_bResult = TRUE;
					theApp.m_GammaThread[iStageNum]->GammaVecAdd(theApp.m_strContactPanelID[iChNum - 1], theApp.m_strContactPanelID[iChNum - 1], iPanelNum, iStageNum, iChNum, PG_CONTACT_OFF, PGContactOffTimer);
					PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Panel [%s] Contact Off ReStart"), PG_IndexName[iStageNum], iPanelNum, theApp.m_strContactPanelID[iChNum - 1]));
				
					CString strMsg = CStringSupport::FormatString(_T("Ch,%d,KEY,RESET"), iChNum);
					theApp.m_PgSocketManager[m_iPcNum].SendPGMessage(strMsg, iPanelNum, iStageNum);
					theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_GammaStage1ContactOn1stResult + iStageNum, iPanelNum, &m_codeFail);
				}
				else
				{
					if (theApp.m_PgPassMode)
						theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_GammaStage1ContactOnResult + iStageNum, iPanelNum, &m_codeOk);
					else
						theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_GammaStage1ContactOnResult + iStageNum, iPanelNum, &m_codeFail);


					PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Panel [%s] Contact On Fail"), PG_IndexName[iStageNum], iChNum, theApp.m_lastGammaVec[iStageNum][iPanelNum].m_cellId));
					theApp.m_lastGammaVec[iStageNum][iPanelNum].time_check.StopTimer();
					theApp.m_lastGammaVec[iStageNum][iPanelNum].m_bResult = TRUE;
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaContactOnEnd1 + iStageNum, OffSet_0, TRUE);

					CString strPanelID = theApp.m_lastGammaVec[iStageNum][iPanelNum].m_cellId;
					CString strFpcID = theApp.m_lastGammaVec[iStageNum][iPanelNum].m_FpcID;
					theApp.GammaDefectInfoSave(strPanelID, strFpcID, theApp.m_strContactNgCode, theApp.m_strContactNgGrade);
				}
			}

			m_lastCommand[iStageNum][iPanelNum] = responseTokens[3];
			m_lastResult[iStageNum][iPanelNum] = responseTokens[5];
		}
	}
	else if (responseTokens[3] == _T("KEY"))  
	{
		if (responseTokens[4] == _T("RESET") && theApp.m_GammaThread[iStageNum]->m_bOperatorModeFlag[iStageNum] == TRUE)
		{
			//if (responseTokens[6] == _T("GOOD"))
			//{
				if (theApp.m_bContact[iChNum - 1] == TRUE && _ttoi(theApp.m_strPGName) == PG_MuhanZC)
				{
					theApp.m_lastGammaVec[iStageNum][iPanelNum].time_check.StopTimer();
					theApp.m_lastGammaVec[iStageNum][iPanelNum].m_bResult = TRUE;
					theApp.m_GammaThread[iStageNum]->GammaVecAdd(theApp.m_strContactPanelID[iChNum - 1], theApp.m_strContactPanelID[iChNum - 1], iPanelNum, iStageNum, iChNum, PG_CONTACT_ON, PGContactOnTimer);
					PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Panel [%s] Contact On ReStart"), PG_IndexName[iStageNum], iPanelNum, theApp.m_strContactPanelID[iChNum - 1]));
			
					CString strMsg = CStringSupport::FormatString(_T("Ch,%d,CONTACT,%s"), iChNum, theApp.m_strContactPanelID[iChNum - 1]);
					theApp.m_PgSocketManager[m_iPcNum].SendPGMessage(strMsg, iPanelNum, iStageNum);
					theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_GammaStage1ContactOffResult + iStageNum, iPanelNum, &m_codeFail);
				}
				else
				{
					theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_GammaStage1ContactOffResult + iStageNum, iPanelNum, &m_codeOk);
			
					PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Contact Off Success"), PG_IndexName[iStageNum], iChNum));
					theApp.m_lastGammaVec[iStageNum][iPanelNum].time_check.StopTimer();
					theApp.m_lastGammaVec[iStageNum][iPanelNum].m_bResult = TRUE;
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaContactOffEnd1 + iStageNum, OffSet_0, TRUE);
				}
			//}
			//else
			//{
			//	theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_GammaStage1ContactOffResult + iStageNum, iPanelNum, &m_codeFail);
			//
			//	PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Contact Off Fail"), PG_IndexName[iStageNum], iChNum));
			//	theApp.m_lastGammaVec[iStageNum][iPanelNum].time_check.StopTimer();
			//	theApp.m_lastGammaVec[iStageNum][iPanelNum].m_bResult = TRUE;
			//	theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaContactOffEnd1 + iStageNum, OffSet_0, TRUE);
			//}
		}
		else if (responseTokens[4] == _T("NEXT"))
		{
			theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaContactNextEnd1 + iStageNum, OffSet_0, TRUE);

			PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Pattern Next End"), PG_IndexName[iStageNum], iChNum));
			theApp.m_lastGammaVec[iStageNum][iPanelNum].time_check.StopTimer();
			theApp.m_lastGammaVec[iStageNum][iPanelNum].m_bResult = TRUE;

		}
		else if (responseTokens[4] == _T("BACK"))
		{
			theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaContactBackEnd1 + iStageNum, OffSet_0, TRUE);
			PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Pattern Back End"), PG_IndexName[iStageNum], iChNum));
			theApp.m_lastGammaVec[iStageNum][iPanelNum].time_check.StopTimer();
			theApp.m_lastGammaVec[iStageNum][iPanelNum].m_bResult = TRUE;
		}

		m_lastCommand[iStageNum][iPanelNum] = responseTokens[3];
		m_lastResult[iStageNum][iPanelNum] = responseTokens[4];
	}
	else if (responseTokens[3] == _T("GAMMA")) // TWICE => CH,NUM,DONE,GAMMA,END,NG,CODE,GRADE
	{
		if (responseTokens[4] == _T("END"))
		{
			if (responseTokens[5] == _T("GOOD"))
			{
				theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_GammaStage1MTPResult1 + iStageNum, iPanelNum, &m_codeOk);

				PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Panel [%s] Gamma Success"), PG_IndexName[iStageNum], iChNum, theApp.m_lastGammaVec[iStageNum][iPanelNum].m_cellId));
				theApp.m_lastGammaVec[iStageNum][iPanelNum].time_check.StopTimer();
				theApp.m_lastGammaVec[iStageNum][iPanelNum].m_bResult = TRUE;
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaStage1MTPEnd1 + iStageNum, iPanelNum, TRUE);
			}			
			else
			{
				if (theApp.m_PgPassMode)
					theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_GammaStage1MTPResult1 + iStageNum, iPanelNum, &m_codeOk);
				else
					theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_GammaStage1MTPResult1 + iStageNum, iPanelNum, &m_codeFail);

				CString strPanelID = theApp.m_lastGammaVec[iStageNum][iPanelNum].m_cellId;
				CString strFpcID = theApp.m_lastGammaVec[iStageNum][iPanelNum].m_FpcID;
				CString strCode = responseTokens[6];
				CString strGrade = responseTokens[7];
				
				theApp.GammaDefectInfoSave(strPanelID, strFpcID, strCode, strGrade);

				PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Panel [%s] Gamma Fail"), PG_IndexName[iStageNum], iChNum, theApp.m_lastGammaVec[iStageNum][iPanelNum].m_cellId));
				theApp.m_lastGammaVec[iStageNum][iPanelNum].time_check.StopTimer();
				theApp.m_lastGammaVec[iStageNum][iPanelNum].m_bResult = TRUE;
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaStage1MTPEnd1 + iStageNum, iPanelNum, TRUE);
			}

			m_lastCommand[iStageNum][iPanelNum] = responseTokens[3];
			m_lastResult[iStageNum][iPanelNum] = responseTokens[5];
		}
	}
	else if (responseTokens[3] == _T("CONTACTOFF")) // Ch, Number, DONE, KEY, RESET, END, GOOD == > Ch, Number, DONE, CONTACT OFF, END, GOOD
	{
		if (responseTokens[4] == _T("END"))
		{
			theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_GammaStage1ContactOffResult + iStageNum, iPanelNum, &m_codeOk);

			PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Contact Off Success"), PG_IndexName[iStageNum], iChNum));
			theApp.m_lastGammaVec[iStageNum][iPanelNum].time_check.StopTimer();
			theApp.m_lastGammaVec[iStageNum][iPanelNum].m_bResult = TRUE;
			theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaContactOffEnd1 + iStageNum, OffSet_0, TRUE);
		}
		m_lastCommand[iStageNum][iPanelNum] = responseTokens[3];
		m_lastResult[iStageNum][iPanelNum] = responseTokens[5];
	}
	else if (responseTokens[3] == _T("PID"))
	{
		if (responseTokens[4] == _T("END"))
		{
			if (responseTokens[5] == _T("GOOD"))
			{
				theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_GammaStage1PIDCheckResult + iStageNum, iPanelNum, &m_codeOk);

				PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Panel [%s] PID Check Success"), PG_IndexName[iStageNum], iChNum, theApp.m_lastGammaVec[iStageNum][iPanelNum].m_cellId));
				theApp.m_lastGammaVec[iStageNum][iPanelNum].time_check.StopTimer();
				theApp.m_lastGammaVec[iStageNum][iPanelNum].m_bResult = TRUE;
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaStage1PIDCheckEnd + iStageNum, iPanelNum, TRUE);
			}
			else
			{
				if (theApp.m_PgPassMode)
					theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_GammaStage1PIDCheckResult + iStageNum, iPanelNum, &m_codeOk);
				else
					theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_GammaStage1PIDCheckResult + iStageNum, iPanelNum, &m_codeFail);

				PgLogMessage(CStringSupport::FormatString(_T("[%s] Ch %d Panel [%s] PID Check Fail"), PG_IndexName[iStageNum], iChNum, theApp.m_lastGammaVec[iStageNum][iPanelNum].m_cellId));
				theApp.m_lastGammaVec[iStageNum][iPanelNum].time_check.StopTimer();
				theApp.m_lastGammaVec[iStageNum][iPanelNum].m_bResult = TRUE;
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaStage1PIDCheckEnd + iStageNum, iPanelNum, TRUE);
			}
		}
	}
}
#endif

void CPgManager::OnEvent(UINT uEvent, LPVOID lpvData)
{
	if (theApp.m_bExitFlag == FALSE)
		return;

	// 先交给基类处理公共的断线重连逻辑
	if (OnEventReconnectBase(uEvent))
		return;

	// 以下为 PG 业务相关事件的处理
	switch (uEvent)
	{
	case EVT_ZEROLENGTH:
		PgLogMessage(CStringSupport::FormatString(_T("PG EVT_ZEROLENGTH")));
		break;
	case EVT_CONFAILURE:
		PgLogMessage(CStringSupport::FormatString(_T("PG EVT_CONFAILURE")));
		break;
	default:
		PgLogMessage(CStringSupport::FormatString(_T("Unknown Socket event")));
		break;
	}
}

BOOL CPgManager::getConectCheck()
{
	return getConectCheckBase();
}

bool CPgManager::SocketServerOpen(CString strServerPort, int iPcNum)
{
	SocketServerOpenBase(strServerPort);
	m_iPcNum = iPcNum;
	m_bMelsecSimulaion = true;
	SetSmartAddressing(false);
	SetServerState(true);
	bool ret = CreateSocket(strServerPort, AF_INET, SOCK_STREAM, 0);
	if (ret) return WatchComm();
	else return false;
}

void CPgManager::RemoveClient()
{
	ShutdownConnection((SOCKET)m_hComm);
}

void CPgManager::LogServerMsg(LPCTSTR szMsg)
{
	PgLogMessage(szMsg);
}
