
#include "stdafx.h"
#if _SYSTEM_AMTAFT_
#include "DlgMainView.h"
#include "DlgMainLog.h"
#include "DBInterface.h"
#include "DataModels.h"
#else
#include "DlgGammaMain.h"
#endif
#include "PlcThread.h"
#include "DataInfo.h"
#include "DFSInfo.h"
#include "AniUtil.h"
#include "FileSupport.h"

////////////////////////////////////////////////////////////////////////////////
// CPlcThread（PLC 主线程，总体流程说明，给不写代码的工程人员看）
//
// 1) 角色定位
//    - 整机的「中枢调度线程」，负责：
//        • 与 PLC 通讯：读写各种 Word/Bit 地址（通过 MNetH）
//        • 监控 TP / PG / OPV / Gamma / AOI 等子系统状态（各 Manager 已把结果写入 PLC / 内存）
//        • 进行班别切换、Daily Log 拷贝、Alarm/Interlock 管理
//        • 汇总每片 Panel 的最终检测结果，生成 DFS 报工结构并交给 DFSClient 上传
//
// 2) 主循环 `ThreadRun()` 的大概节奏
//    - 每 50 ms 循环一次：
//        • `CFTPClient()`：扫描待上传的 DFS 文件队列（SUM/INDEX 等）
//        • 检查 PLC 是否在线：`theApp.m_pEqIf->m_pMNetH->IsConnected()`
//        • 处理机种/班别切换时间，到点后做 Data Reset、CSV 日志拷贝到服务器
//        • 轮询各类 PLC Bit/Word：设备起停、Alarm、TP/PG/OPV/Gamma End Bit、ULD End Bit 等
//        • 当检测到某片 Panel 完成时，调用 `SumDFSDataStart()` 做 DFS 报工
//
// 3) DFS 报工主流程（以 AOI 线为例）
//    - AOI/TP/PG/OPV 等模块先把结果写入 PLC 的 DFS 区：
//        • TP/Contact/Gamma/OPV 结果 → 各 Manager → PLC Word/Bit（详见 TP/PG/OPV Manager 头部注释）
//        • Flow 线程在 PLC 检测「各工序 End Bit 全到位」后，打包为一条 `DfsData` 写入 DFS Word 区
//    - `CPlcThread::SumDFSDataStart(iNum, iOkNg, iType)`：
//        • 根据 Stage 编号 iNum 和 OK/NG，调用 `MNetH::GetDfsData()` 读回 `DfsData` 结构
//        • 把数值字段转成字符串、人可读的 "OK/NG/BYPASS" 文本，填进 `DfsDataValue`
//        • 调用 `theApp.m_pFTP->DfsAddTransferFile(&pDfsDateValue)` 加入 DFS 上传队列
//        • 置位 `eBitType_UnloadOK/NGDFSEnd1 + iNum` 告知 PLC：该 Stage 的 DFS 已处理完毕
//
// 4) 典型一片 Panel 的整体时序（只看主干，细节见各模块注释）
//    - TP/PG/OPV/Gamma：
//        • 各 Manager 通过 Socket 与外部设备交互，收到结果后：
//              → 计算 Zone / Panel / Stage
//              → 写 PLC 结果 Word（OK/NG/Rank/Code 等）
//              → 置相应 End Bit（InspectionEnd / ContactEnd / PreGammaEnd / VSResultEnd 等）
//    - Flow / PLC：
//        • 当同一片 Panel 的 TP / Contact / Gamma / OPV / AOI 必要 End Bit 全部 ON：
//              → 把这片 Panel 的所有结果打包写入 DFS 区（`DfsData` 结构，按 Stage 编号 iNum）
//    - PLC 线程 `CPlcThread`：
//        • 检测到需要报工的 Stage → 调用 `SumDFSDataStart()`
//        • DFSClient 线程收到 `DfsAddTransferFile` 请求后，通过 FTP 上传 SUM/INDEX/图片信息到 DFS Server
//
// 5) 查问题时的建议阅读顺序
//    - 先看：本文件中 `ThreadRun()` & `SumDFSDataStart()` 上方的大块注释（总时序 + DFS 字段说明）
//    - 再看：`TpManager.cpp` / `PgManager.cpp` / `OpvManager.cpp` 顶部的“时序总览”注释，理解各工序如何写 PLC
//    - 然后：在 `MNetH.cpp` 中对应的 SetPlcBitData / SetWordResultOffSet / GetDfsData 等函数了解地址实际读写
////////////////////////////////////////////////////////////////////////////////

CPlcThread::CPlcThread()
{
	ModelCreateChangeIniLoad();
	m_hQuit = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_hQuitTact = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_plcStart = TRUE;
	m_bAlarmStart = FALSE;
	m_bAlarmReset = FALSE;
	m_bFirstCheck = TRUE;
	m_bModelFirstCheck = TRUE;
	m_bCardReaderIDStart = FALSE;
	m_bCardReaderPassWordStart = FALSE;
	m_bBCDataExistCheck = FALSE;
	m_bAlarmCountFlag = FALSE;

	m_vecAlarmReset.clear();

	for (int ii = 0; ii < UnloaderMaxDataCount; ii++)
		for (int jj = 0; jj < ChMaxCount; jj++)
			m_bUnloaderDataFlag[ii][jj] = FALSE;

	for (int ii = 0; ii < PanelMaxCount; ii++)
		m_bDataFlag[ii] = FALSE;

	for (int ii = 0; ii < ChMaxCount; ii++)
	{
		m_bTrayDataFlag[ii] = FALSE;
		m_bDefectCodeStart[ii] = 0;
		m_bULD_OK_DefectCodeStart[ii] = 0;
		m_bULD_NG_DefectCodeStart[ii] = 0;
		m_bJobDataStart[ii] = 0;
	}
	
	for (int ii = 0; ii < 3; ii++)
		m_bProductDataSaveFlag[ii] = FALSE;
}

CPlcThread::~CPlcThread()
{
	theApp.m_bExitFlag = FALSE;
}

void CPlcThread::ThreadRun()
{
	ModelNameData pModelName;
	LoginUserData pLoginUser;
	CTime curTime, HalfMinute;
	
	BOOL bTpFlag = FALSE;
	// Track PLC connection transitions so operators can confirm link status from log.
	int prevPlcConnected = -1; // -1 = unknown (first loop), 0 = disconnected, 1 = connected
	

	while (::WaitForSingleObject(m_hQuit, 50) != WAIT_OBJECT_0)
	{
		CFTPClient();
		theApp.m_PlcConectStatus = theApp.m_pEqIf->m_pMNetH->IsConnected();

		// Log PLC connection state changes (no spam, only on transition).
		const int curPlcConnected = theApp.m_PlcConectStatus ? 1 : 0;
		if (prevPlcConnected != curPlcConnected)
		{
			prevPlcConnected = curPlcConnected;
			if (curPlcConnected)
				theApp.m_PlcLog->LOG_INFO(_T("[PLC] CONNECTED (IsConnected=TRUE)"));
			else
				theApp.m_PlcLog->LOG_WARN(_T("[PLC] DISCONNECTED (IsConnected=FALSE)"));
		}

		if (theApp.m_PlcConectStatus)
		{
			ProgramStartStopLog();
			
			curTime = CTime::GetCurrentTime();
			HalfMinute = CTime::CTime(curTime.GetTime());
			CString strTime = _T("");
			strTime = CStringSupport::FormatString(_T("%d%02d"), HalfMinute.GetHour(), HalfMinute.GetMinute());

			int shiftIndex = theApp.GetShift(_ttoi(strTime));
			theApp.GetShiftTime(_ttoi(strTime), shiftIndex);

			//220319 Result Log Copy to FTP
			//CString strCsvFilePath(_T("")), strLumiTopLogPath(_T("")), str_Current_day;
			//strCsvFilePath.Format(_T("\\\\172.18.3.110\\module\\%s\\DAILY"), theApp.m_strEqpId);
			////strCsvFilePath.Replace(_T("\\"), _T("/"));
			//CString strEQPID = CStringSupport::FormatString(_T("%s%s"), theApp.m_strEqpId, theApp.m_strEqpNum);


			//CString strLumiTopResult_Log_Path_1, strLumiTopResult_Log_Path_2, strFileNameCsv_1, strFileNameCsv_2, FileName_1, FileName_2;
			//strFileNameCsv_1.Format(_T("LUMITOP\\Result_Log\\%s_LUMITOP_Result_Log_1.csv"), GetDateStringChangeDay(-1));
			//strLumiTopResult_Log_Path_1 = DFS_SHARE_PATH + strFileNameCsv_1;
			//strFileNameCsv_2.Format(_T("LUMITOP\\Result_Log\\%s_LUMITOP_Result_Log_2.csv"), GetDateStringChangeDay(-1));
			//strLumiTopResult_Log_Path_2 = DFS_SHARE_PATH + strFileNameCsv_2;
			//BOOL bFIle_1_OK = PathFileExists(strLumiTopResult_Log_Path_1);
			//FileName_1.Format(_T("%s_%s_LUMITOP_Result_Log_1.csv"), GetDateStringChangeDay(-1), strEQPID);
			//strCsvFilePath = strCsvFilePath + _T("\\") + FileName_1;
			//BOOL bCopy_OK = PathFileExists(strCsvFilePath);
			//if (bFIle_1_OK == TRUE && bCopy_OK == FALSE)
			//{
			//	::CopyFile(strLumiTopResult_Log_Path_1, strCsvFilePath, FALSE);
			//}
			//BOOL bFIle_2_OK = PathFileExists(strLumiTopResult_Log_Path_2);
			//FileName_2.Format(_T("%s_%s_LUMITOP_Result_Log_2.csv"), GetDateStringChangeDay(-1), strEQPID);
			//strCsvFilePath.Format(_T("\\\\172.18.3.110\\module\\%s\\DAILY"), theApp.m_strEqpId);
			//strCsvFilePath = strCsvFilePath + _T("\\") + FileName_2;
			//bCopy_OK = PathFileExists(strCsvFilePath);
			//if (bFIle_2_OK == TRUE && bCopy_OK == FALSE)
			//{
			//	::CopyFile(strLumiTopResult_Log_Path_2, strCsvFilePath, FALSE);
			//}
			/////////////////


			// DY NT END 시간에 따라 유동적으로 변한다
			if (_ttoi(strTime) >= theApp.m_iDataResetTime[Shift_DY][Shift_Start] && _ttoi(strTime) < theApp.m_iDataResetTime[Shift_DY][Shift_End])
			{
				//>> 왕형 요청사항 210114
				CString strCsvFilePath(_T("")), strAOICellLogPath(_T("")), FileName, FileName2, strCsvFilePath2;
				if (theApp.m_bDFSTestMode == TRUE)
					strCsvFilePath = _T("D:\\TEST");
				else
					strCsvFilePath.Format(_T("\\\\172.18.3.110\\module\\%s\\DAILY"), theApp.m_strEqpId);
				//strCsvFilePath.Replace(_T("\\"), _T("/"));
				CString strEQPID = CStringSupport::FormatString(_T("%s%s"), theApp.m_strEqpId, theApp.m_strEqpNum);
				FileName.Format(_T("%s_%s_Cell_Log_NT.csv"), GetDateStringChangeDay(-1), strEQPID);
				strAOICellLogPath.Format(_T("%s%s"), AOI_DATA_INSPECT_CSV_PATH, FileName);
				strCsvFilePath2.Format(_T("%s\\%s"), strCsvFilePath, FileName);

				::CopyFile(strAOICellLogPath, strCsvFilePath2, FALSE);

				FileName.Format(_T("%s_%s_ResultLog_NT.csv"), GetDateStringChangeDay(-1), strEQPID);
				FileName2.Format(_T("%s_ResultLog_NT.csv"), GetDateStringChangeDay(-1));

				strAOICellLogPath.Format(_T("%s%s"), AOI_DATA_INSPECT_CSV_PATH, FileName2);
				strCsvFilePath2.Format(_T("%s\\%s"), strCsvFilePath, FileName);

				::CopyFile(strAOICellLogPath, strCsvFilePath2, FALSE);

				FileName.Format(_T("%s_%s_ISLog_NT.csv"), GetDateStringChangeDay(-1), strEQPID);
				FileName2.Format(_T("%s_ISLog_NT.csv"), GetDateStringChangeDay(-1));
				
				strAOICellLogPath.Format(_T("%s%s"), AOI_DATA_INSPECT_CSV_PATH, FileName2);
				strCsvFilePath2.Format(_T("%s\\%s"), strCsvFilePath, FileName);

				::CopyFile(strAOICellLogPath, strCsvFilePath2, FALSE);

				FileName.Format(_T("%s_%s_MURALog_NT.csv"), GetDateStringChangeDay(-1), strEQPID);
				FileName2.Format(_T("%s_MURALog_NT.csv"), GetDateStringChangeDay(-1));

				strAOICellLogPath.Format(_T("%s%s"), AOI_DATA_INSPECT_CSV_PATH, FileName2);				
				strCsvFilePath2.Format(_T("%s\\%s"), strCsvFilePath, FileName);

				::CopyFile(strAOICellLogPath, strCsvFilePath2, FALSE);


				//<<
				theApp.InspDataShiftReset(Shift_DY); // 0728 ~ 0729
			}
			else if (_ttoi(strTime) >= theApp.m_iDataResetTime[Shift_NT][Shift_Start] && _ttoi(strTime) < theApp.m_iDataResetTime[Shift_NT][Shift_End])
			{
				//>> 왕형 요청사항 210114
				CString strCsvFilePath(_T("")), strAOICellLogPath(_T("")), FileName, FileName2, strCsvFilePath2;
				if (theApp.m_bDFSTestMode == TRUE)
					strCsvFilePath = _T("D:\\TEST");
				else
					strCsvFilePath.Format(_T("\\\\172.18.3.110\\module\\%s\\DAILY"), theApp.m_strEqpId);
				//strCsvFilePath.Replace(_T("\\"), _T("/"));
				CString strEQPID = CStringSupport::FormatString(_T("%s%s"), theApp.m_strEqpId, theApp.m_strEqpNum);
				FileName.Format(_T("%s_%s_Cell_Log_DY.csv"), GetDateStringChangeDay(0), strEQPID);
				strAOICellLogPath.Format(_T("%s%s"), AOI_DATA_INSPECT_CSV_PATH, FileName);
				strCsvFilePath2.Format(_T("%s\\%s"), strCsvFilePath, FileName);

				::CopyFile(strAOICellLogPath, strCsvFilePath2, FALSE);

				FileName.Format(_T("%s_%s_ResultLog_DY.csv"), GetDateStringChangeDay(-1), strEQPID);
				FileName2.Format(_T("%s_ResultLog_DY.csv"), GetDateStringChangeDay(-1));

				strAOICellLogPath.Format(_T("%s%s"), AOI_DATA_INSPECT_CSV_PATH, FileName2);
				strCsvFilePath2.Format(_T("%s\\%s"), strCsvFilePath, FileName);

				::CopyFile(strAOICellLogPath, strCsvFilePath2, FALSE);

				FileName.Format(_T("%s_%s_ISLog_DY.csv"), GetDateStringChangeDay(-1), strEQPID);
				FileName2.Format(_T("%s_ISLog_DY.csv"), GetDateStringChangeDay(-1));

				strAOICellLogPath.Format(_T("%s%s"), AOI_DATA_INSPECT_CSV_PATH, FileName2);
				strCsvFilePath2.Format(_T("%s\\%s"), strCsvFilePath, FileName);

				::CopyFile(strAOICellLogPath, strCsvFilePath2, FALSE);

				FileName.Format(_T("%s_%s_MURALog_DY.csv"), GetDateStringChangeDay(-1), strEQPID);
				FileName2.Format(_T("%s_MURALog_DY.csv"), GetDateStringChangeDay(-1));

				strAOICellLogPath.Format(_T("%s%s"), AOI_DATA_INSPECT_CSV_PATH, FileName2);
				strCsvFilePath2.Format(_T("%s\\%s"), strCsvFilePath, FileName);

				::CopyFile(strAOICellLogPath, strCsvFilePath2, FALSE);
				//<<

				theApp.InspDataShiftReset(Shift_NT); // 1928 ~ 1929
			}

			if (_ttoi(strTime) == 1700) // CSOT 요청사항..나중에 안쓸거같음
			{
				if (m_bProductDataSaveFlag[0] == FALSE)
				{
					m_bProductDataSaveFlag[0] = TRUE;
					ProductDataSave(TRUE);
				}
			}
			else if (_ttoi(strTime) == theApp.m_iDataResetTime[Shift_DY][Shift_Start] - 1)
			{
				if (m_bProductDataSaveFlag[1] == FALSE)
				{
					m_bProductDataSaveFlag[1] = TRUE;
					ProductDataSave(FALSE);
				}
			}
			else if (_ttoi(strTime) == theApp.m_iDataResetTime[Shift_NT][Shift_Start] - 1)
			{
				if (m_bProductDataSaveFlag[2] == FALSE)
				{
					m_bProductDataSaveFlag[2] = TRUE;
					ProductDataSave(FALSE);
				}
			}

			if (theApp.m_lastShiftIndex != shiftIndex)
			{
				theApp.m_lastShiftIndex = shiftIndex;

				if (theApp.m_lastShiftIndex == Shift_DY)
				{
					if (m_bFirstCheck == FALSE)
					{
						for (int ii = 0; ii < theApp.m_vecTactName.size(); ii++)
						{
							theApp.TactTimeTotalDataSave(ii, theApp.m_pTactTimeList[ii].GetAvgTactTime(), TRUE);
						}
					}

					for (int ii = 0; ii < theApp.m_vecTactName.size(); ii++)
					{
						theApp.m_pTactTimeList[ii].m_iSumTimeValue = 0;
						theApp.m_pTactTimeList[ii].m_iTactTimeCount = 0;
					}

					EZIni ini(DATA_SYSTEM_DATA_PATH);
					CString strDataToday = ini[_T("DATA")][_T("CURRENT_TOADY")];
					theApp.m_strCurrentToday = GetDateString();
					if (theApp.m_strCurrentToday.CompareNoCase(strDataToday))
						ini[_T("DATA")][_T("CURRENT_TOADY")] = theApp.m_strCurrentToday;

				}

				for (int ii = 0; ii < 3; ii++)
					m_bProductDataSaveFlag[ii] = 0;

				theApp.m_AlarmRankCount[theApp.m_lastShiftIndex].clear();

#if _SYSTEM_AMTAFT_
				theApp.m_SumDefectCountData[theApp.m_lastShiftIndex].Reset();
				theApp.m_mapOpvDefectList[theApp.m_lastShiftIndex].clear();
				theApp.m_VecDefectHistory[theApp.m_lastShiftIndex].clear();
#endif
				if (m_bFirstCheck == TRUE)
				{
					m_bFirstCheck = FALSE;
					theApp.GetAlarmCount();
					theApp.TotalTactTimeLoad();
					theApp.IDCardReaderLoad();
					theApp.PmModeIDCardReaderLoad();
#if _SYSTEM_AMTAFT_
					theApp.LoadRank();
					theApp.OpvLoadTitleName();
					theApp.DefectCodeListLoad();
#endif

					for (int jj = 0; jj < eNumShift; jj++)
					{
#if _SYSTEM_AMTAFT_
						for (int ii = 0; ii < MaxZone; ii++)
						{
							theApp.m_shiftProduction[ii].Reset(jj);
							theApp.m_UiShiftProduction[ii].Reset(jj);

							if(ii < ChMaxCount)
							{
								theApp.m_ULDshiftProduction[ii].Reset(jj);
								theApp.m_ULDUiShiftProduction[ii].Reset(jj);
							}
						}
#else
						for (int ii = 0; ii < MaxGammaStage; ii++)
						{
							theApp.m_shiftProduction[ii].Reset(jj);
							theApp.m_UiShiftProduction[ii].Reset(jj);
						}
#endif
						for (int ii = 0; ii < InspectTimeTotalCount; ii++)
						{
							theApp.m_shift_TimeProduction[ii].Reset(jj);
							theApp.m_UiShift_TimeProduction[ii].Reset(jj);
#if _SYSTEM_AMTAFT_
							theApp.m_ULDshift_TimeProduction[ii].Reset(jj);
							theApp.m_ULDUiShift_TimeProduction[ii].Reset(jj);
#endif
						}
#if _SYSTEM_AMTAFT_

						theApp.AOIInspectionDataLoad(jj);
						theApp.AOIInspectionTimeDataLoad(jj);
						theApp.ULDInspectionDataLoad(jj);
						theApp.ULDInspectionTimeDataLoad(jj);

						theApp.AlignDataLoad(jj);
						theApp.ULDAlignDataLoad(jj);
						theApp.ContactDataLoad(jj);
						theApp.TpDataLoad(jj);
						theApp.PreGammaDataLoad(jj);
						theApp.SetLoadHistoryCode(jj);
						theApp.SetLoadRankCode(jj);

						theApp.m_SumDefectCountData[jj].Reset();
						//theApp.OpvDefectHistoryLosd();
						theApp.OpvDefectPanelHistoryLosd();
						theApp.OpvDefectSumCount();
#else
						theApp.InspectionDataLoad(jj);
						theApp.InspectionTimeDataLoad(jj);

						theApp.ContactDataLoad(jj);
						theApp.AlignDataLoad(jj);
						theApp.MtpDataLoad(jj);
#endif
					}
#if _SYSTEM_AMTAFT_
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionSameDefectAlarmStart, OffSet_0, FALSE);
#endif
				}
				else
					DefectRankClear();
			}

			m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_ModelStart, OffSet_0);

			if (m_bStartFlag == FALSE)
			{
				theApp.m_pEqIf->m_pMNetH->GetModelNameData(&pModelName);
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_ModelEnd, OffSet_0, FALSE);
				
			}

			if (m_bModelStart == !m_bStartFlag)
			{
				m_bModelStart = m_bStartFlag;
				theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("Model Start Flag [%s]"), m_bModelStart == FALSE ? _T("FALSE") : _T("TRUE")));
				if (m_bModelStart == TRUE)
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_ModelEnd, OffSet_0, FALSE);
					ModelCheckMethod();
				}
			}

			if (m_bModelFirstCheck == TRUE)
			{
				theApp.m_pEqIf->m_pMNetH->GetModelNameData(&pModelName);
				SetModelData(pModelName);
				m_bModelFirstCheck = FALSE;
			}

			m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_AlarmStart, OffSet_0);

			if (m_bStartFlag == FALSE)
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AlarmEnd, OffSet_0, FALSE);

			if (m_bAlarmStart == !m_bStartFlag)
			{
				m_bAlarmStart = m_bStartFlag;
				theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("Alarm Start Flag [%s]"), m_bAlarmStart == FALSE ? _T("FALSE") : _T("TRUE")));
				if (m_bAlarmStart == TRUE)
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AlarmEnd, OffSet_0, FALSE);
					AlarmDataParser();
				}
			}

			m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_AlarmReset, OffSet_0);

			if (m_bAlarmReset == !m_bStartFlag)
			{
				m_bAlarmReset = m_bStartFlag;
				theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("Alarm Reset Flag [%s]"), m_bAlarmStart == FALSE ? _T("FALSE") : _T("TRUE")));
				if (m_bAlarmReset == TRUE)
					AlarmResetInfo();
				for (int ii = 0; ii < 16; ii++)
				{
					//for (int jj = 0; jj < PanelMaxCount; jj++)
					{
						theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_TPCodeCh1Result + ii, OffSet_0, &m_codeReset);
						theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_PGCodeCh1Result + ii, OffSet_0, &m_codeReset);
					}
				}
			}

			m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_AxisStart, OffSet_0);

			if (m_bStartFlag == FALSE)
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AxisEnd, OffSet_0, FALSE);

			if (m_AxisStart == !m_bStartFlag)
			{
				m_AxisStart = m_bStartFlag;
				theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("Axis Start Flag [%s]"), m_AxisStart == FALSE ? _T("FALSE") : _T("TRUE")));
				if (m_AxisStart == TRUE)
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AxisEnd, OffSet_0, FALSE);
					AxisDataParser();
				}
			}

			m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_OperateStart, OffSet_0);

			if (m_bStartFlag == FALSE)
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_OperateEnd, OffSet_0, FALSE);

			if (m_OperateTimeStart == !m_bStartFlag)
			{
				m_OperateTimeStart = m_bStartFlag;
				theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("Operate Time Start Flag [%s]"), m_OperateTimeStart == FALSE ? _T("FALSE") : _T("TRUE")));
				if (m_OperateTimeStart == TRUE)
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_OperateEnd, OffSet_0, FALSE);
					OperateTimeParser();
				}
			}

			if (theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_IDSerarchSend, OffSet_0))
				CardReaderIdPasswordCheck(Card_Reader_ID);
			else
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_IDSerarchReceived, OffSet_0, FALSE);

			m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_IDSerarchStart, OffSet_0);

			if (m_bStartFlag == FALSE)
			{
				theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_IdResult, &m_codeReset);
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_IDSerarchEnd, OffSet_0, FALSE);
			}

			if (m_bCardReaderIDStart == !m_bStartFlag)
			{
				m_bCardReaderIDStart = m_bStartFlag;
				theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("Card Reader ID Serarch Start Flag [%s]"), m_bCardReaderIDStart == FALSE ? _T("FALSE") : _T("TRUE")));
				if (m_bCardReaderIDStart == TRUE)
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_IdResult, &m_codeReset);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_IDSerarchEnd, OffSet_0, FALSE);
					CardReaderIDSerarch();
				}
			}

			if (theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_PassWordSerarchSend, OffSet_0))
				CardReaderIdPasswordCheck(Card_Reader_PassWord);
			else
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_PassWordSerarchReceived, OffSet_0, FALSE);

			m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_PassWordSerarchStart, OffSet_0);

			if (m_bStartFlag == FALSE)
			{
				theApp.m_pEqIf->m_pMNetH->SetCardReaderUserData(eWordType_PassWordResult, &pLoginUser);
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_PassWordSerarchEnd, OffSet_0, FALSE);
			}

			if (m_bCardReaderPassWordStart == !m_bStartFlag)
			{
				m_bCardReaderPassWordStart = m_bStartFlag;
				theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("Card Reader PassWord Serarch Start Flag [%s]"), m_bCardReaderPassWordStart == FALSE ? _T("FALSE") : _T("TRUE")));
				if (m_bCardReaderPassWordStart == TRUE)
				{
					theApp.m_pEqIf->m_pMNetH->SetCardReaderUserData(eWordType_PassWordResult, &pLoginUser);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_PassWordSerarchEnd, OffSet_0, FALSE);
					CardReaderPassWordSerarch();
				}
			}
#if _SYSTEM_AMTAFT_
			for (int ii = 0; ii < ChMaxCount; ii++)
			{
				m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_DFSStart1 + ii, OffSet_0);

				if (m_bStartFlag == FALSE)
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_DFSEnd1 + ii, OffSet_0, FALSE);
				}

				if (m_bDfsStart[ii] == !m_bStartFlag)
				{
					m_bDfsStart[ii] = m_bStartFlag;
					theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("DFS %d Start Flag [%s]"), ii + 1, m_bDfsStart[ii] == FALSE ? _T("FALSE") : _T("TRUE")));
					if (m_bDfsStart[ii] == TRUE)
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_DFSEnd1 + ii, OffSet_0, FALSE);
						DFSDataStart(ii, OKPanel, Machine_AOI);
					}
				}
			}

			for (int ii = 0; ii < ChMaxCount; ii++)
			{
				m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_UnloadOKDFSStart1 + ii, OffSet_0);

				if (m_bStartFlag == FALSE)
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_UnloadOKDFSEnd1 + ii, OffSet_0, FALSE);
				}

				if (m_bDfsStartOK[ii] == !m_bStartFlag)
				{
					m_bDfsStartOK[ii] = m_bStartFlag;
					theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("Unload DFS OK Panel %d Start Flag [%s]"), ii + 1, m_bDfsStartNG[ii] == FALSE ? _T("FALSE") : _T("TRUE")));
					if (m_bDfsStartOK[ii] == TRUE)
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_UnloadOKDFSEnd1 + ii, OffSet_0, FALSE);
						SumDFSDataStart(ii, OKPanel, Machine_ULD);
					}
				}

				m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_UnloadNGDFSStart1 + ii, OffSet_0);

				if (m_bStartFlag == FALSE)
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_UnloadNGDFSEnd1 + ii, OffSet_0, FALSE);
				}

				if (m_bDfsStartNG[ii] == !m_bStartFlag)
				{
					m_bDfsStartNG[ii] = m_bStartFlag;
					theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("Sum DFS NG Panel %d Start Flag [%s]"), ii + 1, m_bDfsStartNG[ii] == FALSE ? _T("FALSE") : _T("TRUE")));
					if (m_bDfsStartNG[ii] == TRUE)
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_UnloadNGDFSEnd1 + ii, OffSet_0, FALSE);
						SumDFSDataStart(ii, NGPanel, Machine_ULD);
					}
				}
			}

			for (int ii = 0; ii < ChMaxCount; ii++)
			{
				m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_DefectCodeStart1 + ii, OffSet_0);

				if (m_bStartFlag == FALSE)
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_DefectCodeEnd1 + ii, OffSet_0, FALSE);
				}

				if (m_bDefectCodeStart[ii] == !m_bStartFlag)
				{
					m_bDefectCodeStart[ii] = m_bStartFlag;
					theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("AOI DefectCode Panel %d Start Flag [%s]"), ii + 1, m_bDefectCodeStart[ii] == FALSE ? _T("FALSE") : _T("TRUE")));
					if (m_bDefectCodeStart[ii] == TRUE)
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_DefectCodeEnd1 + ii, OffSet_0, FALSE);
						DefectCodeStart(ii);
					}
				}
			}

			for (int ii = 0; ii < ChMaxCount; ii++)
			{
				m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_ULD_OK_DefectCodeStart1 + ii, OffSet_0);

				if (m_bStartFlag == FALSE)
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_ULD_OK_DefectCodeEnd1 + ii, OffSet_0, FALSE);
				}

				if (m_bULD_OK_DefectCodeStart[ii] == !m_bStartFlag)
				{
					m_bULD_OK_DefectCodeStart[ii] = m_bStartFlag;
					theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("OK DefectCode Panel %d Start Flag [%s]"), ii + 1, m_bDefectCodeStart[ii] == FALSE ? _T("FALSE") : _T("TRUE")));
					if (m_bULD_OK_DefectCodeStart[ii] == TRUE)
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_ULD_OK_DefectCodeEnd1 + ii, OffSet_0, FALSE);
						SumDefectCodeStart(ii, Machine_ULD, OKPanel);
					}
				}
			}

			for (int ii = 0; ii < ChMaxCount; ii++)
			{
				m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_ULD_NG_DefectCodeStart1 + ii, OffSet_0);

				if (m_bStartFlag == FALSE)
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_ULD_NG_DefectCodeEnd1 + ii, OffSet_0, FALSE);
				}

				if (m_bULD_NG_DefectCodeStart[ii] == !m_bStartFlag)
				{
					m_bULD_NG_DefectCodeStart[ii] = m_bStartFlag;
					theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("NG DefectCode Panel %d Start Flag [%s]"), ii + 1, m_bDefectCodeStart[ii] == FALSE ? _T("FALSE") : _T("TRUE")));
					if (m_bULD_NG_DefectCodeStart[ii] == TRUE)
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_ULD_NG_DefectCodeEnd1 + ii, OffSet_0, FALSE);
						SumDefectCodeStart(ii, Machine_ULD, NGPanel);
					}
				}
			}
#else
			for (int ii = 0; ii < ChMaxCount; ii++)
			{
				m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_DFSStart1 + ii, OffSet_0);
			
				if (m_bStartFlag == FALSE)
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_DFSEnd1 + ii, OffSet_0, FALSE);
				}
			
				if (m_bDfsStart[ii] == !m_bStartFlag)
				{
					m_bDfsStart[ii] = m_bStartFlag;
					theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("DFS OK %d Start Flag [%s]"), ii + 1, m_bDfsStart[ii] == FALSE ? _T("FALSE") : _T("TRUE")));
					if (m_bDfsStart[ii] == TRUE)
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_DFSEnd1 + ii, OffSet_0, FALSE);
						SumDFSDataStart(ii, OKPanel, Machine_GAMMA);
			
					}
				}
			}

			for (int ii = 0; ii < ChMaxCount; ii++)
			{
				m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_GammaNGDFSStart1 + ii, OffSet_0);

				if (m_bStartFlag == FALSE)
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaNGDFSEnd1 + ii, OffSet_0, FALSE);
				}

				if (m_bDfsStartNG[ii] == !m_bStartFlag)
				{
					m_bDfsStartNG[ii] = m_bStartFlag;
					theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("DFS NG Panel %d Start Flag [%s]"), ii + 1, m_bDfsStartNG[ii] == FALSE ? _T("FALSE") : _T("TRUE")));
					if (m_bDfsStartNG[ii] == TRUE)
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaNGDFSEnd1 + ii, OffSet_0, FALSE);
						SumDFSDataStart(ii, NGPanel, Machine_GAMMA);
					}
				}
			}

			for (int ii = 0; ii < ChMaxCount; ii++)
			{
				m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_DefectCodeStart1 + ii, OffSet_0);

				if (m_bStartFlag == FALSE)
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_DefectCodeEnd1 + ii, OffSet_0, FALSE);
				}

				if (m_bDefectCodeStart[ii] == !m_bStartFlag)
				{
					m_bDefectCodeStart[ii] = m_bStartFlag;
					theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("OK DefectCode Panel %d Start Flag [%s]"), ii + 1, m_bDefectCodeStart[ii] == FALSE ? _T("FALSE") : _T("TRUE")));
					if (m_bDefectCodeStart[ii] == TRUE)
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_DefectCodeEnd1 + ii, OffSet_0, FALSE);
						SumDefectCodeStart(ii, Machine_GAMMA, OKPanel);
					}
				}
			}

			for (int ii = 0; ii < ChMaxCount; ii++)
			{
				m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_GammaNGDefectCodeStart1 + ii, OffSet_0);

				if (m_bStartFlag == FALSE)
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaNGDefectCodeEnd1 + ii, OffSet_0, FALSE);
				}

				if (m_bULD_OK_DefectCodeStart[ii] == !m_bStartFlag)
				{
					m_bULD_OK_DefectCodeStart[ii] = m_bStartFlag;
					theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("NG DefectCode Panel %d Start Flag [%s]"), ii + 1, m_bDefectCodeStart[ii] == FALSE ? _T("FALSE") : _T("TRUE")));
					if (m_bULD_OK_DefectCodeStart[ii] == TRUE)
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaNGDefectCodeEnd1 + ii, OffSet_0, FALSE);
						SumDefectCodeStart(ii, Machine_GAMMA, NGPanel);
					}
				}
			}
#endif
			//if (theApp.m_bAllPassMode)
			//	continue;

			int iNum = _ttoi(theApp.m_strAlignCount);
			int iAlignTypeNum[MaxAlignInspectType] = { 0, 0, 0, 0 };
			for (int ii = 0; ii < iNum; ii++)
			{
				if (theApp.m_iAlignInspectType[ii] == PatternAlign)
				{
					if (theApp.m_AlignPCStatus[ii])
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_Align1Ready1 + iAlignTypeNum[PatternAlign], OffSet_0, TRUE);
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_Align1Ready2 + iAlignTypeNum[PatternAlign], OffSet_0, TRUE);
					}
					else
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_Align1Ready1 + iAlignTypeNum[PatternAlign], OffSet_0, FALSE);
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_Align1Ready2 + iAlignTypeNum[PatternAlign], OffSet_0, FALSE);
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_Align1End1 + iAlignTypeNum[PatternAlign], OffSet_0, FALSE);
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_Align1End2 + iAlignTypeNum[PatternAlign], OffSet_0, FALSE);
						theApp.m_pEqIf->m_pMNetH->SetAlignResult(eWordType_Align1Result1 + iAlignTypeNum[PatternAlign], &m_AlignResultReset);
						theApp.m_pEqIf->m_pMNetH->SetAlignResult(eWordType_Align1Result2 + iAlignTypeNum[PatternAlign], &m_AlignResultReset);
					}
					iAlignTypeNum[PatternAlign]++;
				}
				else if (theApp.m_iAlignInspectType[ii] == TrayCheck)
				{
					if (theApp.m_AlignPCStatus[ii])
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_TrayCheckReady1 + iAlignTypeNum[TrayCheck], OffSet_0, TRUE);
					else
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_TrayCheckReady1 + iAlignTypeNum[TrayCheck], OffSet_0, FALSE);
						//theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_TrayCheckEnd1 + iAlignTypeNum[TrayCheck], OffSet_0, TRUE);
						//theApp.m_pEqIf->m_pMNetH->SetTrayCheckResult(eWordType_TrayCheckResult1 + iAlignTypeNum[TrayCheck], &m_trayCheckResultReset);
					}
					iAlignTypeNum[TrayCheck]++;
				}
				else if (theApp.m_iAlignInspectType[ii] == TrayAlign)
				{
					if (theApp.m_AlignPCStatus[ii])
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_TrayAlignReady1 + iAlignTypeNum[TrayAlign], OffSet_0, TRUE);
					else
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_TrayAlignReady1 + iAlignTypeNum[TrayAlign], OffSet_0, FALSE);
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_TrayAlignEnd1 + iAlignTypeNum[TrayAlign], OffSet_0, TRUE);
						theApp.m_pEqIf->m_pMNetH->SetAlignResult(eWordType_TrayAlignResult1 + iAlignTypeNum[TrayAlign], &m_AlignResultReset);
					}
					iAlignTypeNum[TrayAlign]++;
				}
				else if (theApp.m_iAlignInspectType[ii] == TrayLowerAlign)
				{
					if (theApp.m_AlignPCStatus[ii])
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_TrayLowerAlignReady1 + iAlignTypeNum[TrayLowerAlign], OffSet_0, TRUE);
					}
					else
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_TrayLowerAlignReady1 + iAlignTypeNum[TrayLowerAlign], OffSet_0, FALSE);
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_TrayLowerAlignEnd1 + iAlignTypeNum[TrayLowerAlign], OffSet_0, TRUE);
						theApp.m_pEqIf->m_pMNetH->SetTrayLowerAlignResult(eWordType_TrayLowerAlignResult1 + iAlignTypeNum[TrayLowerAlign], &m_trayLowerAlignReslutReset);
					}
					iAlignTypeNum[TrayLowerAlign]++;
				}

			}

			for (int ii = 0; ii < ChMaxCount; ii++)
			{
				m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_JobDataStart1 + ii, OffSet_0);

				if (m_bStartFlag == FALSE)
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_JobDataEnd1 + ii, OffSet_0, FALSE);

				if (m_bJobDataStart[ii] == !m_bStartFlag)
				{
					m_bJobDataStart[ii] = m_bStartFlag;
					theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("Job Data Start Flag [%s]"), m_bJobDataStart[ii] == FALSE ? _T("FALSE") : _T("TRUE")));
					if (m_bJobDataStart[ii] == TRUE)
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_JobDataEnd1 + ii, OffSet_0, FALSE);
						m_bBCDataExistCheck = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_BCDataExist1 + ii, OffSet_0);
#if _SYSTEM_AMTAFT_
						JobDataStart(ii, Machine_AOI);
#else
						JobDataStart(ii, Machine_GAMMA);
#endif
					}
				}
			}
#if _SYSTEM_AMTAFT_
			for (int ii = 0; ii < ChMaxCount; ii++)
			{
				m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_UnloadJobDataStart1 + ii, OffSet_0);

				if (m_bStartFlag == FALSE)
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_UnloadJobDataEnd1 + ii, OffSet_0, FALSE);

				if (m_bJobDataStart[ii] == !m_bStartFlag)
				{
					m_bJobDataStart[ii] = m_bStartFlag;
					theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("ULD Job Data Start Flag [%s]"), m_bJobDataStart[ii] == FALSE ? _T("FALSE") : _T("TRUE")));
					if (m_bJobDataStart[ii] == TRUE)
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_UnloadJobDataEnd1 + ii, OffSet_0, FALSE);
						m_bBCDataExistCheck = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_UnloadBCDataExist1 + ii, OffSet_0);
						JobDataStart(ii, Machine_ULD);
					}
				}
			}


			for (int ii = 0; ii < PanelMaxCount; ii++)
			{
				m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_DataReportStart1 + ii, OffSet_0);

				if (m_bStartFlag == FALSE)
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_DataReportEnd1 + ii, OffSet_0, FALSE);

				if (m_bDataFlag[ii] == !m_bStartFlag)
				{
					m_bDataFlag[ii] = m_bStartFlag;
					theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("AOI Lower DataReport Flag %d [%s]"), ii, m_bDataFlag[ii] == FALSE ? _T("FALSE") : _T("TRUE")));
					if (m_bDataFlag[ii] == TRUE)
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_DataReportEnd1 + ii, OffSet_0, FALSE);
						AOIInspectDataParser(ii, Data_LowerMachineOut);
					}
				}
			}

			for (int ii = 0; ii < ChMaxCount; ii++)
			{
				m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_TrayReportStart1 + ii, OffSet_0);

				if (m_bStartFlag == FALSE)
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_TrayReportEnd1 + ii, OffSet_0, FALSE);

				if (m_bTrayDataFlag[ii] == !m_bStartFlag)
				{
					m_bTrayDataFlag[ii] = m_bStartFlag;
					theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("AOI Tray DataReport Flag %d [%s]"), ii, m_bTrayDataFlag[ii] == FALSE ? _T("FALSE") : _T("TRUE")));
					if (m_bTrayDataFlag[ii] == TRUE)
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_TrayReportEnd1 + ii, OffSet_0, FALSE);
						AOIInspectDataParser(ii, Data_TrayOut);
					}
				}
			}

			for (int jj = 0; jj < UnloaderMaxDataCount; jj++)
			{
				for (int ii = 0; ii < ChMaxCount; ii++)
				{
					m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_GoodReportStart + jj, ii);

					if (m_bStartFlag == FALSE)
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GoodReportEnd + jj, ii, FALSE);

					if (m_bUnloaderDataFlag[jj][ii] == !m_bStartFlag)
					{
						m_bUnloaderDataFlag[jj][ii] = m_bStartFlag;
						theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("%s Data Start Flag %d [%s]"), UnloaderDataOutName[jj], ii, m_bUnloaderDataFlag[jj][ii] == FALSE ? _T("FALSE") : _T("TRUE")));
						if (m_bUnloaderDataFlag[jj][ii] == TRUE)
						{
							theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GoodReportEnd + jj, ii, FALSE);
							ULDInspectDataParser(ii, jj);
						}
					}
				}
			}

			//index °è¼Ó È®ÀÎÇÏ¸é¼­ 1(TRUE) ÀÎÀÚ¸® È®ÀÎ
			theApp.IndexCheck();

			if (theApp.m_PgPassMode)
			{
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_PreGammaReady, OffSet_0, TRUE);
			}
			else
			{
				if (theApp.m_PgConectStatus)
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_PreGammaReady, OffSet_0, TRUE);
					for (int ii = 0; ii < MaxZone; ii++)
					{
						for (int jj = 0; jj < PanelMaxCount; jj++)
						{
							theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_TPCodeCh1Result + ii, OffSet_0 + jj, &m_codeReset);
							theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_PGCodeCh1Result + ii, OffSet_0 + jj, &m_codeReset);
						}
					}
				}
				else
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_PreGammaReady, OffSet_0, FALSE);
					//¿¬°á Disconnect µÇ¸é End »óÅÂµµ ÀüºÎ 1(TRUE)Ã³¸®
					for (int ii = 0; ii < MaxZone; ii++)
					{
						for (int jj = 0; jj < PanelMaxCount; jj++)
						{
							theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AZoneContactOnEnd + ii, OffSet_0 + jj, FALSE);
							theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AZoneContactOffEnd + ii, OffSet_0 + jj, FALSE);
							theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_AZoneContactOnResult + ii, OffSet_0 + jj, &m_codeReset);
							theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_AZoneContactOffResult + ii, OffSet_0 + jj, &m_codeReset);
							theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_TPCodeCh1Result + ii, OffSet_0 + jj, &m_codeReset);
							theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_PGCodeCh1Result + ii, OffSet_0 + jj, &m_codeReset);
						}
					}
				}
			}


			if (theApp.m_TpPassMode)
			{
				for (int ii = 0; ii < MaxZone; ii++)
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AZoneTouchReady + ii, OffSet_0, TRUE);
			}
			else
			{
				for (int ii = 0; ii < MaxZone; ii++)
				{	
					if (theApp.m_TpConectStatus)
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AZoneTouchReady + ii, OffSet_0, TRUE);
					}
					else
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AZoneTouchReady + ii, OffSet_0, FALSE);
						for (int jj = 0; jj < PanelMaxCount; jj++)
						{
							theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AZoneTouchInspectionEnd + ii, OffSet_0 + jj, FALSE);
							theApp.m_pEqIf->m_pMNetH->SetWordResultOffSet(eWordType_AZoneTouchResult + ii, OffSet_0 + jj, &m_codeReset);
						}
					}
				}
			}

			if (theApp.m_AnglePassMode)
			{
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_ViewingAngleReady, OffSet_0, TRUE);
			}
			else
			{
				//½Ã¾ß°¢ PC ÁØºñ È®ÀÎ
				if (theApp.m_ViewingAnglePCStatus)
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_ViewingAngleReady, OffSet_0, TRUE);
				}
				else
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_ViewingAngleReady, OffSet_0, FALSE);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_ViewingAngleEnd1, OffSet_0, FALSE);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_ViewingAngleEnd2, OffSet_0, FALSE);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_ViewingAngleEnd3, OffSet_0, FALSE);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_ViewingAngleEnd4, OffSet_0, FALSE);
					theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_ViewingAngleResult1, &m_codeReset);
					theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_ViewingAngleResult2, &m_codeReset);
					theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_ViewingAngleResult3, &m_codeReset);
					theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_ViewingAngleResult4, &m_codeReset);
				}
			}

			if (theApp.m_AOIPassMode)
			{
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionReady, OffSet_0, TRUE);
			}
			else
			{
				// ICW 6501端口连接状态检查
				BOOL bICWConnected = theApp.m_ICWCommManager.IsConnected();
				LogWrite(CStringSupport::FormatString(_T("[VisionReady] AOIPassMode=%d, ICWConnected=%d"),
					theApp.m_AOIPassMode, bICWConnected));

				if (bICWConnected)
				{
					LogWrite(_T("[VisionReady] ICW Connected - Set VisionReady=TRUE"));
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionReady, OffSet_0, TRUE);
				}
				else
				{
					LogWrite(_T("[VisionReady] ICW Disconnected - Reset VisionReady and all Vision signals to FALSE"));
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionReady, 0, FALSE);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionEnd1, OffSet_0, FALSE);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionEnd2, OffSet_0, FALSE);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionEnd3, OffSet_0, FALSE);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionEnd4, OffSet_0, FALSE);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionGrabEnd1, OffSet_0, FALSE);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionGrabEnd2, OffSet_0, FALSE);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionGrabEnd3, OffSet_0, FALSE);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionGrabEnd4, OffSet_0, FALSE);
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionSameDefectAlarmStart, OffSet_0, FALSE);
					theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_VisionResult1, &m_codeReset);
					theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_VisionResult2, &m_codeReset);
					theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_VisionResult3, &m_codeReset);
					theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_VisionResult4, &m_codeReset);
				}
			}

			if (theApp.m_iMachineType == SetAFT)
			{
				if (theApp.m_LumitopPassMode)
				{
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_LumitopReady, OffSet_0, TRUE);
				}
				else
				{
					//°Ë»ç PC ÁØºñ È®ÀÎ	
					if (theApp.m_LumitopPCStatus[0] && theApp.m_LumitopPCStatus[1] /*&& theApp.m_LumitopPCStatus[2] && theApp.m_LumitopPCStatus[3]*/)
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_LumitopReady, OffSet_0, TRUE);
					}
					else
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_LumitopReady, 0, FALSE);
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_LumitopEnd1, OffSet_0, FALSE);
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_LumitopEnd2, OffSet_0, FALSE);
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_LumitopEnd3, OffSet_0, FALSE);
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_LumitopEnd4, OffSet_0, FALSE);
						theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_PreGammaResult1, &m_codeReset);
						theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_PreGammaResult2, &m_codeReset);
						theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_PreGammaResult3, &m_codeReset);
						theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_PreGammaResult4, &m_codeReset);
					}
				}
			}

			// >> Unlaoder
			if (theApp.m_PgPassMode)
			{
				for (int ii = 0; ii < ChMaxCount; ii++)
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageAPreGammaReady + ii, OffSet_0, TRUE);
			}
			else
			{
				for (int ii = 0; ii < ChMaxCount; ii++)
				{
					if (theApp.m_PgConectStatus)
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageAPreGammaReady + ii, OffSet_0, TRUE);
					}
					else
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageAPreGammaReady + ii, OffSet_0, FALSE);
						for (int jj = 0; jj < ChMaxCount; jj++)
						{
							theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageAContactOnEnd + ii, OffSet_0, FALSE);
							theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageAContactOffEnd + ii, OffSet_0, FALSE);
							theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageAContactNextEnd + ii, OffSet_0, FALSE);
							theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageAContactBackEnd + ii, OffSet_0, FALSE);
							theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageAPreGammaEnd + ii, OffSet_0, FALSE);
							theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_MStageAContactOnResult + ii, &m_codeReset);
							theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_MStageAContactOffResult + ii, &m_codeReset);
							theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_MStageAGammaResult + ii, &m_codeReset);
						}
					}
				}
			}

			if (theApp.m_TpPassMode)
			{
				for (int ii = 0; ii < ChMaxCount; ii++)
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageATouchReady + ii, OffSet_0, TRUE);
			}
			else
			{
				for (int ii = 0; ii < ChMaxCount; ii++)
				{
					if (theApp.m_TpConectStatus)
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageATouchReady + ii, OffSet_0, TRUE);
					}
					else
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageATouchReady + ii, OffSet_0, FALSE);
						for (int jj = 0; jj < ChMaxCount; jj++)
						{
							theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageATouchEnd + ii, OffSet_0, FALSE);
							theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_MStageATouchResult + ii, &m_codeReset);
						}
					}
				}
			}

			if (theApp.m_OpvPassMode)
			{
				for (int ii = 0; ii < ChMaxCount; ii++)
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageA_OperatorViewReady + ii, OffSet_0, TRUE);
			}
			else
			{
				for (int ii = 0; ii < ChMaxCount; ii++)
				{
					if (theApp.m_OpvConectStatus[ii])
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageA_OperatorViewReady + ii, OffSet_0, TRUE);
					}
					else
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageA_OperatorViewReady + ii, OffSet_0, FALSE);
						for (int jj = 0; jj < ChMaxCount; jj++)
						{
							theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_MStageATouchEnd + ii, OffSet_0, FALSE);
							theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_MStageATouchResult + ii, &m_codeReset);
						}
					}
				}
			}
#else
			if (theApp.m_PgPassMode)
			{
				for (int ii = 0; ii < MaxGammaStage; ii++)
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaStage1Ready + ii, OffSet_0, TRUE);
			}
			else
			{
				for (int ii = 0; ii < MaxGammaStage; ii++)
				{
					if (theApp.m_PgConectStatus)
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaStage1Ready + ii, OffSet_0, TRUE);
					else
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaStage1Ready + ii, OffSet_0, FALSE);

						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaStage1MTPEnd1 + ii, CH_1, FALSE);
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaStage1MTPEnd2 + ii, CH_2, FALSE);
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaContactOnEnd1 + ii, OffSet_0, FALSE);
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaContactOffEnd1 + ii, OffSet_0, FALSE);
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaContactNextEnd1 + ii, OffSet_0, FALSE);
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaContactBackEnd1 + ii, OffSet_0, FALSE);

						for (int jj = 0; jj < ChMaxCount; jj++)
						{
							theApp.m_pEqIf->m_pMNetH->GetWordResultOffSet(eWordType_GammaStage1MTPResult1 + ii, jj, &m_codeReset);
							theApp.m_pEqIf->m_pMNetH->GetWordResultOffSet(eWordType_GammaStage1MTPResult2 + ii, jj, &m_codeReset);
							theApp.m_pEqIf->m_pMNetH->GetWordResultOffSet(eWordType_GammaStage1ContactOnResult + ii, jj, &m_codeReset);
							theApp.m_pEqIf->m_pMNetH->GetWordResultOffSet(eWordType_GammaStage1ContactOffResult + ii, jj, &m_codeReset);
							theApp.m_pEqIf->m_pMNetH->GetWordResultOffSet(eWordType_GammaStage1ContactOn1stResult + ii, jj, &m_codeReset);
						}
					}
				}
			}

			for (int ii = 0; ii < ChMaxCount; ii++)
			{
				m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_DataReportStart1 + ii, OffSet_0);

				if (m_bStartFlag == FALSE)
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_DataReportEnd1 + ii, OffSet_0, FALSE);

				if (m_bDataFlag[ii] == !m_bStartFlag)
				{
					m_bDataFlag[ii] = m_bStartFlag;
					theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("OK Data Start Flag %d [%s]"), ii, m_bDataFlag[ii] == FALSE ? _T("FALSE") : _T("TRUE")));
					if (m_bDataFlag[ii] == TRUE)
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_DataReportEnd1 + ii, OffSet_0, FALSE);
						GAMMAInspectDataParser(ii, Data_LowerMachineOut);
					}
				}
			}

			for (int ii = 0; ii < ChMaxCount; ii++)
			{
				m_bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_NgReportStart1 + ii, OffSet_0);

				if (m_bStartFlag == FALSE)
					theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_NgReportEnd1 + ii, OffSet_0, FALSE);

				if (m_bTrayDataFlag[ii] == !m_bStartFlag)
				{
					m_bTrayDataFlag[ii] = m_bStartFlag;
					theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("NG Tray Data Start Flag %d [%s]"), ii, m_bTrayDataFlag[ii] == FALSE ? _T("FALSE") : _T("TRUE")));
					if (m_bTrayDataFlag[ii] == TRUE)
					{
						theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_NgReportEnd1 + ii, OffSet_0, FALSE);
						GAMMAInspectDataParser(ii, Data_TrayOut);
					}
				}
			}

#endif
			

		}
		else
		{
			m_bAlarmStart = FALSE;
			m_bAlarmReset = FALSE;
			m_AxisStart = FALSE;
			m_bModelStart = FALSE;
			m_bModelFirstCheck = FALSE;
			m_OperateTimeStart = FALSE;
			m_bCardReaderIDStart = FALSE;
			m_bCardReaderPassWordStart = FALSE;
			m_bJobDataStart[0] = FALSE;
			m_bJobDataStart[1] = FALSE;
			m_bDfsStart[0] = FALSE;
			m_bDfsStart[1] = FALSE;
			m_bDfsStartOK[0] = FALSE;
			m_bDfsStartOK[1] = FALSE;
			m_bDfsStartNG[0] = FALSE;
			m_bDfsStartNG[1] = FALSE;
			m_bPlcHeartBitFlag = FALSE;

			curTime = CTime::GetCurrentTime();
			HalfMinute = CTime::CTime(curTime.GetTime());
			CString strTime = _T("");
			strTime = CStringSupport::FormatString(_T("%d%02d"), HalfMinute.GetHour(), HalfMinute.GetMinute());

			int shiftIndex = theApp.GetShift(_ttoi(strTime));
			theApp.GetShiftTime(_ttoi(strTime), shiftIndex);
			if (theApp.m_lastShiftIndex != shiftIndex)
			{
				theApp.m_lastShiftIndex = shiftIndex;
			}
			for (int jj = 0; jj < eNumShift; jj++)
			{
				theApp.SetLoadRankCode(jj);
			}
		}
	}
}

void CPlcThread::TactThreadRun()
{
	while (::WaitForSingleObject(m_hQuit, 10) != WAIT_OBJECT_0)
	{
		if (theApp.m_PlcConectStatus)
		{
			for (int ii = 0; ii < theApp.m_vecTactName.size(); ii++)
			{
				theApp.m_pEqIf->m_pMNetH->GetWordResultOffSet(eWordType_TactTime, theApp.m_vecTactName[ii].m_iTactTimeNum, &m_TactTimeStart);
				if (m_TactTimeStartFlag[ii] != m_TactTimeStart)
				{
					m_TactTimeStartFlag[ii] = m_TactTimeStart;

					theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("[%s] Tact Time Flag [%d]"), theApp.m_vecTactName[ii].m_strTactTimeName, m_TactTimeStart));

					TactTimeStartEndReset(ii, m_TactTimeStart);
				}
			}
		}	
	}
}

void CPlcThread::CardReaderIdPasswordCheck(int iCommand)
{
	CardReaderID pCardReaderID;
	CardReaderPassWord pCardReaderPassWord;
	CString strPanel;
	switch (iCommand)
	{
	case Card_Reader_ID: 
		theApp.m_pEqIf->m_pMNetH->GetCardReaderIDData(eWordType_SearchID, &pCardReaderID);
		strPanel = CStringSupport::ToWString(pCardReaderID.m_UserID, sizeof(pCardReaderID.m_UserID));
		break;
	case Card_Reader_PassWord: 
		theApp.m_pEqIf->m_pMNetH->GetCardReaderPassWordData(eWordType_SearchPassWord, &pCardReaderPassWord);
		strPanel = CStringSupport::ToWString(pCardReaderPassWord.m_UserPassWord, sizeof(pCardReaderPassWord.m_UserPassWord));
		break;
	}

	if (strPanel.GetLength() > 0 || pCardReaderID.m_CardNo > 0)
	{
		switch (iCommand)
		{
		case Card_Reader_ID:theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_IDSerarchReceived, OffSet_0, TRUE); break;
		case Card_Reader_PassWord:theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_PassWordSerarchReceived, OffSet_0, TRUE); break;
		}
	}
}

void CPlcThread::ModelCheckMethod()
{
	CString strModelName;
	ModelNameData pModelName;
	EZIni ini(DATA_SYSTEM_PATH + _T("modelData.ini"));
	CString strModelData;
	BOOL bFlag = TRUE;
	
	theApp.m_pEqIf->m_pMNetH->GetModelNameData(&pModelName);
	strModelData.Format(_T("%d"), pModelName.m_PlcCurrentModelNum);
	
	strModelName = CStringSupport::ToWString(pModelName.m_PlcCurrentModelName, sizeof(pModelName.m_PlcCurrentModelName));
	theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("Model : [%s] , ModelNum : [%d]"), strModelName, pModelName.m_PlcCurrentModelNum));

	if (pModelName.m_PlcModelCopyorChange == ModelCreate)
	{
		strModelName = CStringSupport::ToWString(pModelName.m_PlcCurrentModelName, sizeof(pModelName.m_PlcCurrentModelName));
		std::vector<CString> listOfKeyNames;
		ini[_T("ModelName")].EnumKeyNames(listOfKeyNames);
		for (CString modelNum : listOfKeyNames)
		{
			CString modelName = ini[_T("ModelName")][modelNum];
			if (!modelName.CompareNoCase(strModelName.Trim()))
			{
				bFlag = FALSE;
				ini[_T("ModelName")][strModelData] = strModelName.Trim();
				break;
			}
		}

		if (bFlag)
			SetModelCreate(pModelName);

	}
	else if (pModelName.m_PlcModelCopyorChange == ModelChange)
	{
		strModelName = CStringSupport::ToWString(pModelName.m_PlcCurrentModelName, sizeof(pModelName.m_PlcCurrentModelName));
		std::vector<CString> listOfKeyNames;
		ini[_T("ModelName")].EnumKeyNames(listOfKeyNames);
		for (CString modelNum : listOfKeyNames)
		{
			CString modelName = ini[_T("ModelName")][modelNum];
			if (!modelName.CompareNoCase(strModelName.Trim()))
			{
				bFlag = FALSE;
				ini[_T("ModelName")][strModelData] = strModelName.Trim();
				if (strModelName.Trim().CompareNoCase(theApp.m_CurrentModel.m_AlignPcCurrentModelName))
				{
					SetModelChangeMethod(strModelName.Trim());
				}
				break;
			}
		}

		if (bFlag)
		{
			SetModelCreate(pModelName);
			ModelCreateChangeIniSave(_T("ModelChange"), MC_MODEL_CHANGE);
			theApp.m_CurrentModel.m_AlignPcPreviousModelName = theApp.m_CurrentModel.m_AlignPcCurrentModelName;
			theApp.m_CurrentModel.m_AlignPcCurrentModelName = strModelName.Trim();
			g_topCtrl->m_ctrlModelName.SetCaption(_T("Model Name : ") + theApp.m_CurrentModel.m_AlignPcCurrentModelName);
			LogWrite(CStringSupport::FormatString(_T("PLC Model Change : [%s] -> [%s]"), theApp.m_CurrentModel.m_AlignPcPreviousModelName, theApp.m_CurrentModel.m_AlignPcCurrentModelName));
		}

		EZIni ini(DATA_SYSTEM_PATH + _T("modelData.ini"));
		ini[_T("MODEL")][_T("LAST_MODEL")] = strModelName.Trim();
	}
	theApp.LoadRank();
	theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_ModelEnd, OffSet_0, TRUE);
}

void CPlcThread::SetModelData(ModelNameData pModelName)
{
	EZIni ini(DATA_SYSTEM_PATH + _T("modelData.ini"));
	CString strModelData, strModelName;
	strModelData.Format(_T("%d"), pModelName.m_PlcCurrentModelNum);

	if (ini[_T("ModelName")][strModelData].Exists())
	{
		strModelName = CStringSupport::ToWString(pModelName.m_PlcCurrentModelName, sizeof(pModelName.m_PlcCurrentModelName));
		theApp.m_CurrentModel.m_AlignPcCurrentModelName = strModelName.Trim();
		theApp.m_CurrentModel.m_AlignPcCurrentModelNum = pModelName.m_PlcCurrentModelNum;

		g_topCtrl->m_ctrlModelName.SetCaption(_T("Model Name : ") + theApp.m_CurrentModel.m_AlignPcCurrentModelName);
		LogWrite(CStringSupport::FormatString(_T("PLC Start Model Name : %s"), theApp.m_CurrentModel.m_AlignPcCurrentModelName));
	}
	else
	{
		SetModelCreate(pModelName);
		ModelCreateChangeIniSave(_T("ModelChange"), MC_MODEL_CHANGE);
	}

	ini[_T("MODEL")][_T("LAST_MODEL")] = strModelName.Trim();
}

void CPlcThread::SetModelCreate(ModelNameData pModelName)
{
	EZIni ini(DATA_SYSTEM_PATH + _T("modelData.ini"));
	CString strModelData, strModelName, sendMsg;

	ModelCreateChangeIniSave(_T("ModelCreate"), MC_MODEL_CREATE);
	strModelName = CStringSupport::ToWString(pModelName.m_PlcCurrentModelName, sizeof(pModelName.m_PlcCurrentModelName));
	strModelData.Format(_T("%d"), pModelName.m_PlcCurrentModelNum);
	ini[_T("ModelName")][strModelData] = strModelName.Trim();

	sendMsg.Format(_T("%d,%s"), MC_MODEL_CREATE, strModelName);
	ModelCreateChange(sendMsg, MC_MODEL_CREATE);

	LogWrite(CStringSupport::FormatString(_T("PLC Start Create Model Name : %s"), strModelName));
}

void CPlcThread::SetModelChangeMethod(CString strModelName)
{
	CString sendMsg;
	theApp.m_CurrentModel.m_AlignPcPreviousModelName = theApp.m_CurrentModel.m_AlignPcCurrentModelName;
	theApp.m_CurrentModel.m_AlignPcCurrentModelName = strModelName.Trim();

	ModelCreateChangeIniSave(_T("ModelChange"), MC_MODEL_CHANGE);

	sendMsg.Format(_T("%d,%s"), MC_MODEL_CHANGE, theApp.m_CurrentModel.m_AlignPcCurrentModelName);
	ModelCreateChange(sendMsg, MC_MODEL_CHANGE);

	g_topCtrl->m_ctrlModelName.SetCaption(_T("Model Name : ") + theApp.m_CurrentModel.m_AlignPcCurrentModelName);
	LogWrite(CStringSupport::FormatString(_T("PLC Model Change : [%s] -> [%s]"), theApp.m_CurrentModel.m_AlignPcPreviousModelName, theApp.m_CurrentModel.m_AlignPcCurrentModelName));
}

void CPlcThread::ModelCreateChange(CString sendMsg, int iCommand)
{
	for (int ii = 0; ii < _MODEL_CHECK_TOTAL_COUNT; ii++)
	{
		switch (ii)
		{
		case _MODEL_CHECK_ALIGN:
			for (int ii = 0; ii < _ttoi(theApp.m_strAlignCount); ii++)
				if (theApp.m_AlignConectStatus[ii] == TRUE)
					theApp.m_AlignSocketManager[ii]->SocketSendto(ii, sendMsg, iCommand);

			break;
#if _SYSTEM_AMTAFT_
		case _MODEL_CHECK_VIEWING_ANGLE:
			if (theApp.m_ViewingAngleConectStatus[PanelNum1] == TRUE)
				theApp.m_ViewingAngleSocketManager[PanelNum1].SocketSendto(PanelNum1, sendMsg, iCommand);

			if (theApp.m_ViewingAngleConectStatus[PanelNum2] == TRUE)
				theApp.m_ViewingAngleSocketManager[PanelNum2].SocketSendto(PanelNum2, sendMsg, iCommand);

			if (theApp.m_ViewingAngleConectStatus[PanelNum3] == TRUE)
				theApp.m_ViewingAngleSocketManager[PanelNum3].SocketSendto(PanelNum3, sendMsg, iCommand);

			if (theApp.m_ViewingAngleConectStatus[PanelNum4] == TRUE)
				theApp.m_ViewingAngleSocketManager[PanelNum4].SocketSendto(PanelNum4, sendMsg, iCommand);

			break;
		case _MODEL_CHECK_VISION1:
			// ICW 6501端口连接状态检查
			if (theApp.m_ICWCommManager.IsConnected())
			{
				LogWrite(_T("[ModelCheck] ICW Connected - Vision model check via ICW protocol"));
				// ICW模式下模型检查通过ICW协议处理（如果有对应的消息类型）
				// 目前ICW主要处理Start$/SnapFN$/FN$等检测相关消息
				theApp.m_PlcLog->LOG_INFO(_T("[ModelCheck] ICW mode - model check command not implemented, use Start$ for inspection"));
			}
			else
			{
				LogWrite(_T("[ModelCheck] ICW Disconnected - Cannot send model check command"));
			}
			break;
		case _MODEL_CHECK_OPERATOR_VIEW:
			if (theApp.m_OpvConectStatus[CH_1] == TRUE)
				theApp.m_OpvSocketManager[CH_1].SendOpvMessage(sendMsg, CH_1, iCommand);

			if (theApp.m_OpvConectStatus[CH_2] == TRUE)
				theApp.m_OpvSocketManager[CH_2].SendOpvMessage(sendMsg, CH_2, iCommand);

			break;

		case _MODEL_CHECK_LUMITOP:
			if (theApp.m_LumitopConectStatus[PC1] == TRUE)
				theApp.m_LumitopSocketManager[PC1].SocketSendto(PC1, sendMsg, iCommand);

			if (theApp.m_LumitopConectStatus[PC2] == TRUE)
				theApp.m_LumitopSocketManager[PC2].SocketSendto(PC2, sendMsg, iCommand);

			break;
#endif
		}
	}
}

UINT CPlcThread::PlcThreadProc(LPVOID pParam)
{
	CPlcThread* pThis = reinterpret_cast<CPlcThread*>(pParam);
	_ASSERTE(pThis != NULL);
	pThis->ThreadRun();
	return 1L;

}

BOOL CPlcThread::CreateTask(){
	BOOL bRet = TRUE;
	//參數1：綫程的入口函數，聲明一定要如下：PlcThreadProc(LPVOID pParam)；
	//參數2：綫程竄入的參數，類型為LPVOID，把this傳進去綫程就可以操作和使用類的成員，注意：綫程函數是靜態類的函數成員
	//參數3，4，5是默認的可以省略
	m_pThreadPlc = ::AfxBeginThread(PlcThreadProc, this, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
	if (!m_pThreadPlc)
		bRet = FALSE;
	m_pThreadPlc->m_bAutoDelete = FALSE;
	m_pThreadPlc->ResumeThread();
	return bRet;
}

void CPlcThread::CloseTask()
{
	theApp.m_PlcConectStatus = FALSE;
	if (m_pThreadPlc != NULL)
	{
		SetEvent(m_hQuit);
		Delay(100, TRUE);
		if (::WaitForSingleObject(m_pThreadPlc->m_hThread, 1000) == WAIT_TIMEOUT)
		{
			SetEvent(m_hQuit);
			Delay(100, TRUE);
			if (::WaitForSingleObject(m_pThreadPlc->m_hThread, 1000) == WAIT_TIMEOUT) {
				::TerminateThread(m_pThreadPlc->m_hThread, 1L);
				theApp.m_PlcLog->LOG_INFO(_T("Terminate PLC Thread"));
			}
		}
		delete m_pThreadPlc;
		m_pThreadPlc = NULL;

	}
	if (m_hQuit)
	{
		CloseHandle(m_hQuit);
		m_hQuit = NULL;
	}
}
void CPlcThread::HeartBitThreadRun()
{
	CTimerCheck Timer;
	Timer.SetCheckTime(5000);
	int heartBit = 0;
	BOOL bFlag = FALSE;
	while (::WaitForSingleObject(m_hQuit, 500) != WAIT_OBJECT_0)
	{
		if (theApp.m_PlcConectStatus)
		{
			heartBit = heartBit + 1 % 2;
			
			if (theApp.m_PlcConectStatus)
			{
				m_bPlcHeartBitFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_PlcHearbit, OffSet_0);
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_DataServerPcHeartBit, OffSet_0, m_bPlcHeartBitFlag);
				if (bFlag == !m_bPlcHeartBitFlag)
				{
					bFlag = m_bPlcHeartBitFlag;
					Timer.StartTimer();
				}
				if (Timer.IsTimeOver())
				{
					Timer.StopTimer();
					theApp.m_PlcHeartBitLog->LOG_INFO(_T("m_bPlcHeartBitFlag Error !!!!!"), m_bPlcHeartBitFlag);
				}
			}
		}
	}
}

BOOL CPlcThread::HeartBitCreateTask()
{
	BOOL bRet = TRUE;
	m_pThreadmHeartBit = ::AfxBeginThread(HeartBitThreadProc, this, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
	if (!m_pThreadmHeartBit)
		bRet = FALSE;
	m_pThreadmHeartBit->m_bAutoDelete = FALSE;
	m_pThreadmHeartBit->ResumeThread();
	return bRet;
}

void CPlcThread::HeartBitCloseTask()
{
	theApp.m_PlcConectStatus = FALSE;
	if (m_pThreadmHeartBit != NULL)
	{
		SetEvent(m_hQuitTact);
		Delay(100, TRUE);
		if (::WaitForSingleObject(m_pThreadmHeartBit->m_hThread, 1000) == WAIT_TIMEOUT)
		{
			SetEvent(m_hQuitTact);
			Delay(100, TRUE);
			if (::WaitForSingleObject(m_pThreadmHeartBit->m_hThread, 1000) == WAIT_TIMEOUT) {
				::TerminateThread(m_pThreadmHeartBit->m_hThread, 1L);
				theApp.m_PlcLog->LOG_INFO(_T("Terminate PLC Thread"));
			}
		}
		delete m_pThreadmHeartBit;
		m_pThreadmHeartBit = NULL;

	}
	if (m_hQuitTact)
	{
		CloseHandle(m_hQuitTact);
		m_hQuitTact = NULL;
	}
}
UINT CPlcThread::HeartBitThreadProc(LPVOID pParam)
{
	CPlcThread* pThis = reinterpret_cast<CPlcThread*>(pParam);
	_ASSERTE(pThis != NULL);
	pThis->HeartBitThreadRun();
	return 1L;

}
UINT CPlcThread::TactPlcThreadProc(LPVOID pParam)
{
	CPlcThread* pThis = reinterpret_cast<CPlcThread*>(pParam);
	_ASSERTE(pThis != NULL);
	pThis->TactThreadRun();
	return 1L;

}

BOOL CPlcThread::TactCreateTask(){
	BOOL bRet = TRUE;
	m_pTactThreadPlc = ::AfxBeginThread(TactPlcThreadProc, this, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
	if (!m_pTactThreadPlc)
		bRet = FALSE;
	m_pTactThreadPlc->m_bAutoDelete = FALSE;
	m_pTactThreadPlc->ResumeThread();
	return bRet;
}

void CPlcThread::TactCloseTask()
{
	theApp.m_PlcConectStatus = FALSE;
	if (m_pTactThreadPlc != NULL)
	{
		SetEvent(m_hQuitTact);
		Delay(100, TRUE);
		if (::WaitForSingleObject(m_pTactThreadPlc->m_hThread, 1000) == WAIT_TIMEOUT)
		{
			SetEvent(m_hQuitTact);
			Delay(100, TRUE);
			if (::WaitForSingleObject(m_pTactThreadPlc->m_hThread, 1000) == WAIT_TIMEOUT) {
				::TerminateThread(m_pTactThreadPlc->m_hThread, 1L);
				theApp.m_PlcLog->LOG_INFO(_T("Terminate PLC Thread"));
			}
		}
		delete m_pTactThreadPlc;
		m_pTactThreadPlc = NULL;

	}
	if (m_hQuitTact)
	{
		CloseHandle(m_hQuitTact);
		m_hQuitTact = NULL;
	}
}

void  CPlcThread::LogWrite(CString strContents)
{
	if (theApp.m_bExitFlag == FALSE)
		return;

	g_MainLog->m_PlcListBox.InsertString(0, CStringSupport::FormatString(_T("[%s] %s"), GetNowSystemTimeMilliseconds(), strContents));
	theApp.m_PlcLog->LOG_INFO(strContents);
}

void CPlcThread::ProgramStartStopLog()
{
	BOOL start = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_PlcStartStatus, OffSet_0);
	theApp.m_PLCStatus = start;
	if (m_plcStart == start)
	{
		LogWrite(CStringSupport::FormatString(_T("************************** Program %s **************************"), start == TRUE ? _T("START") : _T("STOP")));
		m_plcStart = !start;
		//theApp.m_bPLCStopNStart = TRUE;
		if (!start)
		{
			for (int ii = 0; ii < _ttoi(theApp.m_strAlignCount); ii++)
				theApp.m_AlignSocketManager[ii]->AlignLightOff(ii);
		}
		CString sendMsg;
		sendMsg.Format(_T("%d,%d"), MC_STATE, start);
		for (int ii = 0; ii < _ttoi(theApp.m_strAlignCount); ii++)
			theApp.m_AlignSocketManager[ii]->SocketSendto(ii, sendMsg, MC_STATE);

#if _SYSTEM_AMTAFT_
		sendMsg.Format(_T("%d,%d"), MC_STATE, start);
		for (int ii = 0; ii < PCMaxCount; ii++)
			theApp.m_VisionSocketManager[ii].SocketSendto(ii, sendMsg, MC_STATE);
#endif
	}
}

void CPlcThread::ModelCreateChangeIniSave(CString strTilteName, int iChangeCreate)
{
	EZIni ini(DATA_SYSTEM_PATH + _T("modelData.ini"));
	ini[strTilteName][_T("Align")] = TRUE;
	ini[strTilteName][_T("ViewingAngle1")] = TRUE;
	ini[strTilteName][_T("ViewingAngle2")] = TRUE;
	ini[strTilteName][_T("ViewingAngle3")] = TRUE;
	ini[strTilteName][_T("ViewingAngle4")] = TRUE;
	ini[strTilteName][_T("Vision1")] = TRUE;
	ini[strTilteName][_T("Vision2")] = TRUE;

	if (iChangeCreate == MC_MODEL_CREATE)
	{
		theApp.m_CreateModelAlign = TRUE;
		theApp.m_CreateModelVision1 = TRUE;
		theApp.m_CreateModelVision2 = TRUE;
		theApp.m_CreateModelViewingAngle1 = TRUE;
		theApp.m_CreateModelViewingAngle2 = TRUE;
		theApp.m_CreateModelViewingAngle3 = TRUE;
		theApp.m_CreateModelViewingAngle4 = TRUE;
		theApp.m_CreateModelLumitop1 = TRUE;
		theApp.m_CreateModelLumitop2 = TRUE;
	}
	else
	{
		theApp.m_ChangeModelAlign = TRUE;
		theApp.m_ChangeModelVision1 = TRUE;
		theApp.m_ChangeModelVision2 = TRUE;
		theApp.m_ChangeModelViewingAngle1 = TRUE;
		theApp.m_ChangeModelViewingAngle2 = TRUE;
		theApp.m_ChangeModelViewingAngle3 = TRUE;
		theApp.m_ChangeModelViewingAngle4 = TRUE;
		theApp.m_ChangeModelLumitop1 = TRUE;
		theApp.m_ChangeModelLumitop2 = TRUE;
	}
}

void CPlcThread::ModelCreateChangeModify(CString strTitleName, CString strValueName, BOOL bValue)
{
	EZIni ini(DATA_SYSTEM_PATH + _T("modelData.ini"));
	ini[strTitleName][strValueName] = bValue;
}

void CPlcThread::ModelCreateChangeIniLoad()
{
	EZIni ini(DATA_SYSTEM_PATH + _T("modelData.ini"));

	theApp.m_CreateModelAlign = ini[_T("ModelCreate")][_T("Align")];
	theApp.m_CreateModelVision1 = ini[_T("ModelCreate")][_T("Vision1")];
	theApp.m_CreateModelVision2 = ini[_T("ModelCreate")][_T("Vision2")];
	theApp.m_CreateModelViewingAngle1 = ini[_T("ModelCreate")][_T("ViewingAngle1")];
	theApp.m_CreateModelViewingAngle2 = ini[_T("ModelCreate")][_T("ViewingAngle2")];
	theApp.m_CreateModelViewingAngle3 = ini[_T("ModelCreate")][_T("ViewingAngle3")];
	theApp.m_CreateModelViewingAngle4 = ini[_T("ModelCreate")][_T("ViewingAngle4")];
	theApp.m_CreateModelLumitop1 = ini[_T("ModelCreate")][_T("Lumitop1")];
	theApp.m_CreateModelLumitop2 = ini[_T("ModelCreate")][_T("Lumitop2")];

	theApp.m_ChangeModelAlign = ini[_T("ModelChange")][_T("Align")];
	theApp.m_ChangeModelVision1 = ini[_T("ModelChange")][_T("Vision1")];
	theApp.m_ChangeModelVision2 = ini[_T("ModelChange")][_T("Vision2")];
	theApp.m_ChangeModelViewingAngle1 = ini[_T("ModelChange")][_T("ViewingAngle1")];
	theApp.m_ChangeModelViewingAngle2 = ini[_T("ModelChange")][_T("ViewingAngle2")];
	theApp.m_ChangeModelViewingAngle3 = ini[_T("ModelChange")][_T("ViewingAngle3")];
	theApp.m_ChangeModelViewingAngle4 = ini[_T("ModelChange")][_T("ViewingAngle4")];
	theApp.m_ChangeModelLumitop1 = ini[_T("ModelCreate")][_T("Lumitop1")];
	theApp.m_ChangeModelLumitop2 = ini[_T("ModelCreate")][_T("Lumitop2")];

	if (theApp.m_CreateModelVision1 || theApp.m_CreateModelVision2 || theApp.m_CreateModelViewingAngle1 || theApp.m_CreateModelViewingAngle2 || 
		theApp.m_CreateModelViewingAngle3 || theApp.m_CreateModelViewingAngle4 || theApp.m_CreateModelAlign || theApp.m_ChangeModelLumitop1 || theApp.m_ChangeModelLumitop2)
	{
		theApp.m_CurrentModel.m_bModelCreate = TRUE;
	}

	if (theApp.m_ChangeModelVision1 || theApp.m_ChangeModelVision2 || theApp.m_ChangeModelViewingAngle1 || theApp.m_ChangeModelViewingAngle2
		|| theApp.m_ChangeModelViewingAngle3 || theApp.m_ChangeModelViewingAngle4 || theApp.m_ChangeModelAlign || theApp.m_ChangeModelLumitop1 || theApp.m_ChangeModelLumitop2)
	{
		theApp.m_CurrentModel.m_bModelChange = TRUE;
	}
}

void CPlcThread::TactTimeStartEndReset(int TactTimeUnit, int TactTimeStatus)
{
	m_csTact.Lock();

	if (theApp.m_bExitFlag == FALSE)
		return;

	if (TactTimeStatus == TactEnd)
	{
		theApp.m_pTactTimeLog->LOG_INFO(CStringSupport::FormatString(_T("[%s] %s End"), theApp.m_vecTactName[TactTimeUnit].m_strTactTimeName, GetNowSystemTimeMilliseconds()));
		theApp.m_pTactTimeList[TactTimeUnit].EndTactTime(theApp.m_vecTactName[TactTimeUnit].m_iTactTimeNum, theApp.m_lastShiftIndex);

		if (theApp.m_pTactTimeList[TactTimeUnit].GetLastTactTime() > 0)
		{
			theApp.TactTimeDataSave(TactTimeUnit);
			theApp.TactTimeTotalDataSave(TactTimeUnit, theApp.m_pTactTimeList[TactTimeUnit].GetLastTactTime(),FALSE);
		}
#if _SYSTEM_AMTAFT_
		if (TactTimeUnit == UNLOADER_STAGE)
		{
			theApp.m_pTactTimeList[Panel_Tact].EndTactTime(Panel_Tact, theApp.m_lastShiftIndex);
			if (theApp.m_pTactTimeList[Panel_Tact].GetLastTactTime() > 0)
			{
				theApp.TactTimeDataSave(Panel_Tact);
				theApp.TactTimeTotalDataSave(Panel_Tact, theApp.m_pTactTimeList[Panel_Tact].GetLastTactTime(), FALSE);
			}
		}
#endif
	}
	else if (TactTimeStatus == TactStart)
	{
		CString strTemp;
		DWORD dwTime = theApp.m_pTactTimeList[TactTimeUnit].GetLastTactTime();
		strTemp.Format(_T("%02d:%02d.%02d"), dwTime / 60000, (dwTime / 1000) % 60, (dwTime / 10) % 100);
		theApp.m_pTactTimeLog->LOG_INFO(CStringSupport::FormatString(_T("[%s] Time : %s"), theApp.m_vecTactName[TactTimeUnit].m_strTactTimeName, strTemp));

		theApp.m_pTactTimeLog->LOG_INFO(CStringSupport::FormatString(_T("[%s] %s Start"), theApp.m_vecTactName[TactTimeUnit].m_strTactTimeName, GetNowSystemTimeMilliseconds()));
		theApp.m_pTactTimeList[TactTimeUnit].BeginTactTime();
	}
	else
	{
		//>>tact timeÀÌ 0 ÀÌ»óÀÎ°Íµé¸¸ ±â·ÏÇÑ´Ù 0ÃÊÀÎ°ÍµéÀº ¹«ÀÇ¹ÌÇÏ´Ï±ñ.
		theApp.m_pTactTimeLog->LOG_INFO(CStringSupport::FormatString(_T("[%s] %s End"), theApp.m_vecTactName[TactTimeUnit].m_strTactTimeName, GetNowSystemTimeMilliseconds()));
		theApp.m_pTactTimeList[TactTimeUnit].EndTactTime(theApp.m_vecTactName[TactTimeUnit].m_iTactTimeNum, theApp.m_lastShiftIndex);
		if (theApp.m_pTactTimeList[TactTimeUnit].GetLastTactTime() > 0)
		{
			theApp.TactTimeDataSave(TactTimeUnit);
			theApp.TactTimeTotalDataSave(TactTimeUnit, theApp.m_pTactTimeList[TactTimeUnit].GetLastTactTime(), FALSE);
		}

		CString strTemp;
		DWORD dwTime = theApp.m_pTactTimeList[TactTimeUnit].GetLastTactTime();
		strTemp.Format(_T("%02d:%02d.%02d"), dwTime / 60000, (dwTime / 1000) % 60, (dwTime / 10) % 100);
		theApp.m_pTactTimeLog->LOG_INFO(CStringSupport::FormatString(_T("[%s] Time : %s"), theApp.m_vecTactName[TactTimeUnit].m_strTactTimeName, strTemp));

		theApp.m_pTactTimeLog->LOG_INFO(CStringSupport::FormatString(_T("[%s] %s Start"), theApp.m_vecTactName[TactTimeUnit].m_strTactTimeName, GetNowSystemTimeMilliseconds()));
		theApp.m_pTactTimeList[TactTimeUnit].BeginTactTime();
	}

	m_csTact.Unlock();
}

void CPlcThread::AlarmDataParser()
{
	theApp.m_pEqIf->m_pMNetH->GetPlcWordData(eWordType_AlarmCode, &m_AlarmCodeResult);

	theApp.AlarmMaxCount++;
	
	if (theApp.AlarmMaxCount > 1000)
	{
		m_bAlarmCountFlag = TRUE;
		theApp.AlarmMaxCount = 0;
	}

	if (theApp.m_AlarmDataList.size() > 999)
		theApp.m_AlarmDataList.pop_back();

	AlarmDataItem alarmData;
	alarmData.m_alarmCode.Format(_T("%d"), m_AlarmCodeResult);

#if _SYSTEM_AMTAFT_
	EZIni ini(DATA_SYSTEM_ALARM_PATH + theApp.m_strCompanyLine + _T("\\AMT_ALARM.ini"));
#else
	EZIni ini(DATA_SYSTEM_ALARM_PATH + theApp.m_strCompanyLine + _T("\\GAMMA_ALARM.ini"));
#endif
	
	if (ini[_T("ALARM")][alarmData.m_alarmCode].Exists())
	{
		CString alarmMsg = ini[_T("ALARM")][alarmData.m_alarmCode];
		CStringArray responseTokens;
		AlarmTextData pAlarmTextData;
		CStringSupport::GetTokenArray(alarmMsg, _T('^'), responseTokens);

		alarmData.m_strTime = GetDateString7();
		alarmData.m_alarmStartTime = GetTimeString();
		alarmData.m_alarmCode = responseTokens[0];
		alarmData.m_alarmMsg = responseTokens[1];

		theApp.m_AlarmDataList.insert(theApp.m_AlarmDataList.begin(), alarmData);
		SetAlarmRankCount(alarmData, theApp.m_lastShiftIndex);

		CStringSupport::ToAString(responseTokens[1].Trim(), pAlarmTextData.m_AlarmText, sizeof(pAlarmTextData.m_AlarmText));
		pAlarmTextData.m_AlarmUnit = _ttoi(responseTokens[2]);
		pAlarmTextData.m_AlarmCode = _ttoi(responseTokens[3]);
		pAlarmTextData.m_AlarmLevel = _ttoi(responseTokens[4]);
		pAlarmTextData.m_AlarmUsingFlag = _ttoi(responseTokens[5]);
		theApp.m_pEqIf->m_pMNetH->SetAlarmTextData(eWordType_AlarmData, &pAlarmTextData);
	}

	theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AlarmEnd, OffSet_0, TRUE);

	m_vecAlarmReset.push_back(alarmData);
}

void CPlcThread::AlarmResetInfo()
{
	BOOL bFlag = FALSE;
	if (m_vecAlarmReset.size() > 0)
	{
		theApp.AlarmDataSave(m_vecAlarmReset, m_bAlarmCountFlag);
		bFlag = TRUE;
	}

	if (bFlag == TRUE)
		m_vecAlarmReset.clear();

	m_bAlarmCountFlag = FALSE;
}

void CPlcThread::SetAlarmRankCount(AlarmDataItem alarmData, int nShift)
{
	map<CString, AlarmDataItem>::iterator Codeiter;
	CString strTemp, strShift, strTemp2;

	SetStringReplace(&alarmData.m_alarmCode, _T("[\\]\\[]"));

	Codeiter = theApp.m_AlarmRankCount[nShift].find(alarmData.m_alarmCode);
	if (Codeiter != theApp.m_AlarmRankCount[nShift].end())
	{
		Codeiter->second.m_alarmCount++;
		strTemp2.Format(_T("%s^%d"), alarmData.m_alarmMsg, Codeiter->second.m_alarmCount);
	}
	else
	{
		alarmData.m_alarmCount = 1;
		theApp.m_AlarmRankCount[nShift].insert(make_pair(alarmData.m_alarmCode, alarmData));
		strTemp2.Format(_T("%s^%d"), alarmData.m_alarmMsg, alarmData.m_alarmCount);
	}

	strShift = nShift == 0 ? _T("DY") : _T("NT");
	strTemp.Format(_T("%s_%s_AlarmCount.ini"), theApp.m_strCurrentToday, strShift);
	EZIni ini(DATA_ALARM_COUNT_PATH + strTemp);
	ini[_T("ALARM_COUNT")][alarmData.m_alarmCode] = strTemp2;
}

void CPlcThread::AxisDataParser()
{
	AxisRecipeID pAxisRecipeID;
	AxisModifyPosition pAxisModifyPosition;
	AxisModifyBeforePosition pAxisModifyBeforePosition;

	theApp.m_pEqIf->m_pMNetH->GetAxisRecipeIDData(eWordType_AxisRecipeIdValue, &pAxisRecipeID);
	theApp.m_pEqIf->m_pMNetH->GetAxisModifyPositionData(eWordType_AxisPostionValue, &pAxisModifyPosition);
	theApp.m_pEqIf->m_pMNetH->GetAxisModifyBeforePositionData(eWordType_AxisPostionBeforeValue, &pAxisModifyBeforePosition);

#if _SYSTEM_AMTAFT_
	EZIni ini(DATA_SYSTEM_AXIS_PATH + theApp.m_strCompanyLine + _T("\\AMT_AXIS.ini"));
#else
	EZIni ini(DATA_SYSTEM_AXIS_PATH + theApp.m_strCompanyLine + _T("\\GAMMA_AXIS.ini"));
#endif

	CString msg, strAxisName, strRecipeId;
	msg.Format(_T("%d"), pAxisRecipeID.m_AxisNo);
	if (ini[_T("AXIS")][msg].Exists())
	{
		strAxisName = ini[_T("AXIS")][msg];
	}
	else
	{
		theApp.m_pAxisLog->LOG_INFO(_T("Axis No Command !!!"));
	}

	strRecipeId = CStringSupport::ToWString(pAxisRecipeID.m_RecipeID, sizeof(pAxisRecipeID.m_RecipeID));

	int nPositionData = pAxisModifyPosition.m_PositionData;
	int nBeforePositionData = pAxisModifyBeforePosition.m_PositionBeforeData;
	int nChangePointData = pAxisRecipeID.m_ChangePoint;
	if (nPositionData != 0 || nBeforePositionData != 0)
	{
		theApp.m_pAxisLog->LOG_INFO(
			CStringSupport::FormatString(_T("[ID : %s][Axis : %s][Point : %d] Before Data : %d -> Data : %d"),
			strRecipeId,
			strAxisName,
			nChangePointData,
			pAxisModifyBeforePosition.m_PositionBeforeData,
			pAxisModifyPosition.m_PositionData)
			);

	}

	int nSpeedData = pAxisModifyPosition.m_PositionSpeedData;
	int nBeforeSpeedData = pAxisModifyBeforePosition.m_PositionBeforeSpeedData;
	if (nSpeedData != 0 || nBeforeSpeedData != 0)
	{
		theApp.m_pAxisLog->LOG_INFO(
			CStringSupport::FormatString(_T("[ID : %s][Axis : %s][Point : %d] Before Speed Data : %d -> Speed Data : %d"),
			strRecipeId,
			strAxisName,
			nChangePointData,
			pAxisModifyBeforePosition.m_PositionBeforeSpeedData,
			pAxisModifyPosition.m_PositionSpeedData)
			);

	}

	if (pAxisModifyPosition.m_AccData != 0 || pAxisModifyBeforePosition.m_AccBeforeData != 0)
	{
		theApp.m_pAxisLog->LOG_INFO(
			CStringSupport::FormatString(_T("[ID : %s][Axis : %s] Before Acc Data : %d -> Acc Data : %d "),
			strRecipeId,
			strAxisName,
			pAxisModifyBeforePosition.m_AccBeforeData,
			pAxisModifyPosition.m_AccData)
			);
	}


	if (pAxisModifyPosition.m_DecData != 0 || pAxisModifyBeforePosition.m_DecBeforeData != 0)
	{
		theApp.m_pAxisLog->LOG_INFO(
			CStringSupport::FormatString(_T("[ID : %s][Axis : %s] Before Dec Data : %d -> Dec Data : %d "),
			strRecipeId,
			strAxisName,
			pAxisModifyBeforePosition.m_DecBeforeData,
			pAxisModifyPosition.m_DecData)
			);
	}

	// -  Negetive + Positive
	if (pAxisModifyPosition.m_SoftData1 != 0 || pAxisModifyBeforePosition.m_SoftBeforeData1 != 0)
	{
		theApp.m_pAxisLog->LOG_INFO(
			CStringSupport::FormatString(_T("[ID : %s][Axis : %s] Before Positive Data : %d -> Acc Data : %d "),
			strRecipeId,
			strAxisName,
			pAxisModifyBeforePosition.m_SoftBeforeData1,
			pAxisModifyPosition.m_SoftData1)
			);
	}

	if (pAxisModifyPosition.m_SoftData2 != 0 || pAxisModifyBeforePosition.m_SoftBeforeData2 != 0)
	{
		theApp.m_pAxisLog->LOG_INFO(
			CStringSupport::FormatString(_T("[ID : %s][Axis : %s] Before Negetive Data : %d -> Acc Data : %d "),
			strRecipeId,
			strAxisName,
			pAxisModifyBeforePosition.m_SoftBeforeData2,
			pAxisModifyPosition.m_SoftData2)
			);
	}

	theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_AxisEnd, OffSet_0, TRUE);
}

void CPlcThread::OperateTimeParser()
{
	CString strTemp;
	OperateTime pOperateTime;

	theApp.m_pEqIf->m_pMNetH->GetOperateTimeData(eWordType_OperateTimeValue, &pOperateTime);

	theApp.m_pOperateTimeLog->LOG_INFO(_T("***************************** Operate Time Start **********************************"));
	theApp.m_pOperateTimeLog->LOG_INFO(CStringSupport::FormatString(_T("The Rate Of Operate : %f"), pOperateTime.m_iRateOfOperate * 0.1));

	strTemp.Format(_T("%02d:%02d:%02d"), (pOperateTime.m_iOperateTime / 3600) % 12, (pOperateTime.m_iOperateTime / 60) % 60, pOperateTime.m_iOperateTime % 60);
	theApp.m_pOperateTimeLog->LOG_INFO(CStringSupport::FormatString(_T("OperateTime : %s"), strTemp));
	strTemp.Format(_T("%02d:%02d:%02d"), (pOperateTime.m_iIdleTime / 3600) % 12, (pOperateTime.m_iIdleTime / 60) % 60, pOperateTime.m_iIdleTime % 60);
	theApp.m_pOperateTimeLog->LOG_INFO(CStringSupport::FormatString(_T("IdleTime : %s"), strTemp));
	strTemp.Format(_T("%02d:%02d:%02d"), (pOperateTime.m_iStopTime / 3600) % 12, (pOperateTime.m_iStopTime / 60) % 60, pOperateTime.m_iStopTime % 60);
	theApp.m_pOperateTimeLog->LOG_INFO(CStringSupport::FormatString(_T("StopTime : %s"), strTemp));

	theApp.m_pOperateTimeLog->LOG_INFO(_T("***************************** Operate Time End ***********************************"));

	theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_OperateEnd, OffSet_0, TRUE);
}


void CPlcThread::CardReaderIDSerarch()
{
	//PM Mode Clear 할경우에는 clear 만 처리하고 나머지는 처리하지않습니다.
	theApp.m_pEqIf->m_pMNetH->GetPlcWordData(eWordType_PM_Mode_LoginClear, &m_lPmModeLoginClear);
	if (m_lPmModeLoginClear == 1)
	{
		theApp.m_pUserLog->LOG_INFO(_T("********************** PM Mode User List Clear **********************"));
		for (auto &PmModeLogin : theApp.m_PmModeLoginUser)
			PmModeLogin.Reset();

		theApp.PmModeIDCardReaderSave();
		theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_IdResult, &m_codeOk);
		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_IDSerarchEnd, OffSet_0, TRUE);
		return;
	}

	BOOL bFlag = FALSE, bPmModeSerarch = FALSE;
	CardReaderID pCardReaderID;
	CString strUserID;
	BOOL bPmMode;

	bPmMode = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_IDSerarchPmMode, OffSet_0);
	theApp.m_pEqIf->m_pMNetH->GetCardReaderIDData(eWordType_SearchID, &pCardReaderID);
	strUserID = CStringSupport::ToWString(pCardReaderID.m_UserID, sizeof(pCardReaderID.m_UserID));

	if (strUserID.Trim().IsEmpty() && pCardReaderID.m_CardNo == 0)
	{
		theApp.m_pUserLog->LOG_INFO(_T("User ID / Card No Error !!!!!"));
		theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_IdResult, &m_codeResponseError);
		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_IDSerarchEnd, OffSet_0, TRUE);
		return;
	}

	if (bPmMode == TRUE)
	{
		//PM Mode
		for (auto PmModeLogin : theApp.m_PmModeLoginUser)
		{
			if (PmModeLogin.m_bLoginFlag == FALSE)
			{
				bPmModeSerarch = TRUE;
				break;
			}
		}

		if (bPmModeSerarch == FALSE)
		{
			theApp.m_pUserLog->LOG_INFO(_T("User Login Full Count !!!!!"));
			theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_IdResult, &m_codeResponseError);
			theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_IDSerarchEnd, OffSet_0, TRUE);
			return;
		}

		for (IDCardReader cardReader : theApp.m_VecIDCardReader)
		{
			for (auto &PmModeLogin : theApp.m_PmModeLoginUser) // ini 에 우리가 저장한거
			{
				if (!cardReader.m_strUserID.CompareNoCase(strUserID.Trim()) && PmModeLogin.m_bLoginFlag == FALSE)
				{
					PmModeLogin.m_strUserID = strUserID.Trim();
					PmModeLogin.m_bIdSerarchFlag = TRUE;
					theApp.m_pUserLog->LOG_INFO(CStringSupport::FormatString(_T("User ID [%s] Login Serarch Success"), strUserID.Trim()));
					bFlag = TRUE;
					break;
				}
				else if (_ttoi(cardReader.m_strIDCardNo) == pCardReaderID.m_CardNo && PmModeLogin.m_bLoginFlag == FALSE)
				{
					PmModeLogin.m_strIDCardNo = cardReader.m_strIDCardNo;
					PmModeLogin.m_bIdSerarchFlag = TRUE;
					theApp.m_pUserLog->LOG_INFO(CStringSupport::FormatString(_T("User ID [%s] Login Serarch Success"), strUserID.Trim()));
					bFlag = TRUE;
					break;
				}
			}

			if (bFlag == TRUE)
				break;

		}

		if (bFlag == TRUE)
		{
			theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_IdResult, &m_codeOk);
			theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_IDSerarchEnd, OffSet_0, TRUE);
			theApp.PmModeIDCardReaderSave();
		}
		else
		{
			theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_IdResult, &m_codeFail);
			theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_IDSerarchEnd, OffSet_0, TRUE);
			theApp.m_pUserLog->LOG_INFO(CStringSupport::FormatString(_T("User ID : [%s] Card No :[%d] Login Serarch FALSE"), strUserID.Trim(), pCardReaderID.m_CardNo));
		}
	}
	else
	{
		for (IDCardReader cardReader : theApp.m_VecIDCardReader)
		{
			if (!cardReader.m_strUserID.CompareNoCase(strUserID.Trim()))
			{
				theApp.m_CurrentLoginUser.m_strUserID = strUserID.Trim();
				theApp.m_pUserLog->LOG_INFO(CStringSupport::FormatString(_T("User ID [%s] Login Serarch Success"), strUserID.Trim()));
				bFlag = TRUE;
				break;
			}
			else if (_ttoi(cardReader.m_strIDCardNo) == pCardReaderID.m_CardNo)
			{
				theApp.m_CurrentLoginUser.m_strIDCardNo = cardReader.m_strIDCardNo;
				theApp.m_pUserLog->LOG_INFO(CStringSupport::FormatString(_T("User ID [%s] Login Serarch Success"), strUserID.Trim()));
				bFlag = TRUE;
				break;
			}
		}

		if (bFlag == TRUE)
		{
			theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_IdResult, &m_codeOk);
			theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_IDSerarchEnd, OffSet_0, TRUE);
			theApp.CurrenrUserSave();
		}
		else
		{
			theApp.m_pEqIf->m_pMNetH->SetPlcWordData(eWordType_IdResult, &m_codeFail);
			theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_IDSerarchEnd, OffSet_0, TRUE);
			theApp.m_pUserLog->LOG_INFO(CStringSupport::FormatString(_T("User ID : [%s] Card No :[%d] Login Serarch FALSE"), strUserID.Trim(), pCardReaderID.m_CardNo));
		}
	}
}

void CPlcThread::CardReaderPassWordSerarch()
{
	LoginUserData pLoginUser;
	BOOL bFlag = FALSE, bPmModeSerarch = FALSE;
	CardReaderPassWord pCardReaderPassWord;
	CardReaderID pCardReaderID;
	CString strUserPassWord, strUserID, strCardID;
	BOOL bPmMode;

	bPmMode = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_PassWordSerarchPmMode, OffSet_0);
	theApp.m_pEqIf->m_pMNetH->GetCardReaderPassWordData(eWordType_SearchPassWord, &pCardReaderPassWord);
	strUserPassWord = CStringSupport::ToWString(pCardReaderPassWord.m_UserPassWord, sizeof(pCardReaderPassWord.m_UserPassWord));
	theApp.m_pEqIf->m_pMNetH->GetCardReaderIDData(eWordType_SearchID, &pCardReaderID);
	strCardID = CStringSupport::FormatString(_T("%d"), pCardReaderID.m_CardNo);
	strUserID = CStringSupport::ToWString(pCardReaderID.m_UserID, sizeof(pCardReaderID.m_UserID));

	if (bPmMode == TRUE)
	{
		IDCardReader currentUser;
		//PM Mode
		for (auto &PmModeLogin : theApp.m_PmModeLoginUser)
		{
			if (PmModeLogin.m_bIdSerarchFlag == TRUE)
			{
				bPmModeSerarch = TRUE;
				break;
			}
		}

		if (bPmModeSerarch == FALSE)
		{
			pLoginUser.m_ResultValue = m_codeResponseError;
			theApp.m_pEqIf->m_pMNetH->SetCardReaderUserData(eWordType_PassWordResult, &pLoginUser);
			theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_PassWordSerarchEnd, OffSet_0, TRUE);
			return;
		}

		if (pCardReaderPassWord.m_IDReaderInOut == 0)
		{
			pLoginUser.m_ResultValue = m_codeResponseError;
			theApp.m_PlcThread->LogWrite(_T("Login/Out Error!!!!"));
			theApp.m_pEqIf->m_pMNetH->SetCardReaderUserData(eWordType_PassWordResult, &pLoginUser);
			theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_PassWordSerarchEnd, OffSet_0, TRUE);
			return;
		}

		if (strUserPassWord.Trim().IsEmpty())
		{
			pLoginUser.m_ResultValue = m_codeResponseError;
			theApp.m_PlcThread->LogWrite(_T("User PassWord Error !!!!!"));
			theApp.m_pEqIf->m_pMNetH->SetCardReaderUserData(eWordType_PassWordResult, &pLoginUser);
			theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_PassWordSerarchEnd, OffSet_0, TRUE);
			return;
		}

		if (pCardReaderPassWord.m_IDReaderInOut == 1)
		{
			for (IDCardReader cardReader : theApp.m_VecIDCardReader)
			{
				for (auto &PmModeLogin : theApp.m_PmModeLoginUser)
				{
					if ((!cardReader.m_strIDCardNo.CompareNoCase(PmModeLogin.m_strIDCardNo)
						|| !cardReader.m_strUserID.CompareNoCase(PmModeLogin.m_strUserID)) && PmModeLogin.m_bLoginFlag == FALSE)
					{
						if (!cardReader.m_strUserPassWord.CompareNoCase(strUserPassWord.Trim()))
						{
							pLoginUser.m_ResultValue = m_codeOk;
							PmModeLogin = currentUser = cardReader;
							PmModeLogin.m_bLoginFlag = TRUE;
							bFlag = TRUE;
							break;
						}
					}
				}

				if (bFlag == TRUE)
					break;
			}

			//ID만 검색한것들 전부 초기화 시켜버림
			for (auto &PmModeLogin : theApp.m_PmModeLoginUser)
				if (PmModeLogin.m_bLoginFlag == FALSE)
					PmModeLogin.Reset();

			if (bFlag == TRUE)
			{
				pLoginUser.m_UserLevel = _ttoi(currentUser.m_strLevel);
				CStringSupport::ToAString(currentUser.m_strUserID, pLoginUser.m_UserID, sizeof(pLoginUser.m_UserID));
				CStringSupport::ToAString(currentUser.m_strUserPassWord, pLoginUser.m_UserPassWord, sizeof(pLoginUser.m_UserPassWord));
				CStringSupport::ToAString(currentUser.m_strIDCardNo, pLoginUser.m_UserIdCardNo, sizeof(pLoginUser.m_UserIdCardNo));
				CStringSupport::ToAString(currentUser.m_strDivision, pLoginUser.m_UserDivision, sizeof(pLoginUser.m_UserDivision));
				CStringSupport::ToAString(currentUser.m_strUserName, pLoginUser.m_UserName, sizeof(pLoginUser.m_UserName));
				currentUser.m_strLoginOut = _T("Login");
				currentUser.m_strLogintTime = GetDateString6();
				CardReaderCsvFileSave(currentUser);
				theApp.PmModeIDCardReaderSave();

				theApp.m_pUserLog->LOG_INFO(CStringSupport::FormatString(_T("User ID : [%s] Login Success"), currentUser.m_strUserID));
				theApp.m_pUserLoginOutLog->LOG_INFO(CStringSupport::FormatString(_T("User ID : [%s] Login Success"), currentUser.m_strUserID));

				theApp.m_pEqIf->m_pMNetH->SetCardReaderUserData(eWordType_PassWordResult, &pLoginUser);
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_PassWordSerarchEnd, OffSet_0, TRUE);
			}
			else
			{
				pLoginUser.m_ResultValue = m_codeFail;
				theApp.m_PlcThread->LogWrite(_T("User PassWord Error !!!!!"));
				theApp.m_pEqIf->m_pMNetH->SetCardReaderUserData(eWordType_PassWordResult, &pLoginUser);
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_PassWordSerarchEnd, OffSet_0, TRUE);
				return;
			}

		}
		else
		{
			//ID만 검색한것들 전부 초기화 시켜버림
			for (auto &PmModeLogin : theApp.m_PmModeLoginUser)
				if (PmModeLogin.m_bLoginFlag == FALSE)
					PmModeLogin.Reset();


			for (auto &PmModeLogin : theApp.m_PmModeLoginUser)
			{
				if (!PmModeLogin.m_strIDCardNo.CompareNoCase(strCardID)
					|| !PmModeLogin.m_strUserID.CompareNoCase(strUserID))
				{
					if (!PmModeLogin.m_strUserPassWord.CompareNoCase(strUserPassWord.Trim()))
					{
						pLoginUser.m_ResultValue = m_codeOk;
						currentUser = PmModeLogin;
						PmModeLogin.Reset();
						bFlag = TRUE;
						break;
					}
				}
			}

			if (bFlag == TRUE)
			{
				pLoginUser.m_UserLevel = _ttoi(currentUser.m_strLevel);
				CStringSupport::ToAString(currentUser.m_strUserID, pLoginUser.m_UserID, sizeof(pLoginUser.m_UserID));
				CStringSupport::ToAString(currentUser.m_strUserPassWord, pLoginUser.m_UserPassWord, sizeof(pLoginUser.m_UserPassWord));
				CStringSupport::ToAString(currentUser.m_strIDCardNo, pLoginUser.m_UserIdCardNo, sizeof(pLoginUser.m_UserIdCardNo));
				CStringSupport::ToAString(currentUser.m_strDivision, pLoginUser.m_UserDivision, sizeof(pLoginUser.m_UserDivision));
				CStringSupport::ToAString(currentUser.m_strUserName, pLoginUser.m_UserName, sizeof(pLoginUser.m_UserName));
				currentUser.m_strLoginOut = _T("LogOut");
				currentUser.m_strLogintTime = GetDateString6();
				CardReaderCsvFileSave(currentUser);
				theApp.PmModeIDCardReaderSave();

				theApp.m_pUserLog->LOG_INFO(CStringSupport::FormatString(_T("User ID : [%s] CardNo : [%s] LogOut Success"), currentUser.m_strUserID, currentUser.m_strIDCardNo));
				theApp.m_pUserLoginOutLog->LOG_INFO(CStringSupport::FormatString(_T("User ID : [%s] CardNo : [%s] LogOut Success"), currentUser.m_strUserID, currentUser.m_strIDCardNo));

				theApp.m_pEqIf->m_pMNetH->SetCardReaderUserData(eWordType_PassWordResult, &pLoginUser);
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_PassWordSerarchEnd, OffSet_0, TRUE);
			}
			else
			{
				pLoginUser.m_ResultValue = m_codeFail;
				theApp.m_PlcThread->LogWrite(_T("User PassWord Error !!!!!"));
				theApp.m_pEqIf->m_pMNetH->SetCardReaderUserData(eWordType_PassWordResult, &pLoginUser);
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_PassWordSerarchEnd, OffSet_0, TRUE);
				return;
			}
		}
	}
	else
	{
		if (!theApp.m_CurrentLoginUser.m_strIDCardNo.CompareNoCase(_T(""))
			&& !theApp.m_CurrentLoginUser.m_strUserID.CompareNoCase(_T("")))
		{
			pLoginUser.m_ResultValue = m_codeResponseError;
			theApp.m_pEqIf->m_pMNetH->SetCardReaderUserData(eWordType_PassWordResult, &pLoginUser);
			theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_PassWordSerarchEnd, OffSet_0, TRUE);
			return;
		}

		//0 = Error , 1 = Login , 2 = LogOut 
		if (pCardReaderPassWord.m_IDReaderInOut == 0)
		{
			pLoginUser.m_ResultValue = m_codeResponseError;
			theApp.m_pUserLog->LOG_INFO(CStringSupport::FormatString(_T("User ID : [%s] Card No :[%d] Login/Out Error!!!!"), theApp.m_CurrentLoginUser.m_strUserID, theApp.m_CurrentLoginUser.m_strIDCardNo));
			theApp.m_pEqIf->m_pMNetH->SetCardReaderUserData(eWordType_PassWordResult, &pLoginUser);
			theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_PassWordSerarchEnd, OffSet_0, TRUE);
			return;
		}

		if (pCardReaderPassWord.m_IDReaderInOut == 1)
		{
			if (strUserPassWord.Trim().IsEmpty())
			{
				pLoginUser.m_ResultValue = m_codeResponseError;
				theApp.m_PlcThread->LogWrite(_T("User PassWord Error !!!!!"));
				theApp.m_pEqIf->m_pMNetH->SetCardReaderUserData(eWordType_PassWordResult, &pLoginUser);
				theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_PassWordSerarchEnd, OffSet_0, TRUE);
				return;
			}

			for (IDCardReader cardReader : theApp.m_VecIDCardReader)
			{
				if (!cardReader.m_strIDCardNo.CompareNoCase(theApp.m_CurrentLoginUser.m_strIDCardNo)
					|| !cardReader.m_strUserID.CompareNoCase(theApp.m_CurrentLoginUser.m_strUserID))
				{
					if (!cardReader.m_strUserPassWord.CompareNoCase(strUserPassWord.Trim()))
					{
						pLoginUser.m_ResultValue = m_codeOk;
						theApp.m_CurrentLoginUser = cardReader;
						bFlag = TRUE;
						break;
					}
					else
					{
						pLoginUser.m_ResultValue = m_codeFail;
						theApp.m_pUserLog->LOG_INFO(CStringSupport::FormatString(_T("User ID : [%s] CardNo : [%s] PassWord [%s] Login Fail"), theApp.m_CurrentLoginUser.m_strUserID, theApp.m_CurrentLoginUser.m_strIDCardNo, strUserPassWord.Trim()));
					}
				}
			}

			if (bFlag == TRUE)
			{
				pLoginUser.m_UserLevel = _ttoi(theApp.m_CurrentLoginUser.m_strLevel);
				CStringSupport::ToAString(theApp.m_CurrentLoginUser.m_strUserID, pLoginUser.m_UserID, sizeof(pLoginUser.m_UserID));
				CStringSupport::ToAString(theApp.m_CurrentLoginUser.m_strUserPassWord, pLoginUser.m_UserPassWord, sizeof(pLoginUser.m_UserPassWord));
				CStringSupport::ToAString(theApp.m_CurrentLoginUser.m_strIDCardNo, pLoginUser.m_UserIdCardNo, sizeof(pLoginUser.m_UserIdCardNo));
				CStringSupport::ToAString(theApp.m_CurrentLoginUser.m_strDivision, pLoginUser.m_UserDivision, sizeof(pLoginUser.m_UserDivision));
				CStringSupport::ToAString(theApp.m_CurrentLoginUser.m_strUserName, pLoginUser.m_UserName, sizeof(pLoginUser.m_UserName));
				theApp.m_CurrentLoginUser.m_strLoginOut = _T("Login");
				theApp.m_CurrentLoginUser.m_strLogintTime = GetDateString6();
				CardReaderCsvFileSave(theApp.m_CurrentLoginUser);
				theApp.CurrenrUserSave();

				theApp.m_pUserLog->LOG_INFO(CStringSupport::FormatString(_T("User ID : [%s] Login Success"), theApp.m_CurrentLoginUser.m_strUserID));
				theApp.m_pUserLoginOutLog->LOG_INFO(CStringSupport::FormatString(_T("User ID : [%s] Login Success"), theApp.m_CurrentLoginUser.m_strUserID));
			}

			theApp.m_pEqIf->m_pMNetH->SetCardReaderUserData(eWordType_PassWordResult, &pLoginUser);
			theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_PassWordSerarchEnd, OffSet_0, TRUE);
		}
		else
		{
			pLoginUser.m_ResultValue = m_codeOk;
			theApp.m_CurrentLoginUser.m_strLoginOut = _T("LogOut");
			theApp.m_CurrentLoginUser.m_strLogintTime = GetDateString6();
			CardReaderCsvFileSave(theApp.m_CurrentLoginUser);

			theApp.m_pUserLog->LOG_INFO(CStringSupport::FormatString(_T("User ID : [%s] CardNo : [%s] LogOut Success"), theApp.m_CurrentLoginUser.m_strUserID, theApp.m_CurrentLoginUser.m_strIDCardNo));
			theApp.m_pUserLoginOutLog->LOG_INFO(CStringSupport::FormatString(_T("User ID : [%s] CardNo : [%s] LogOut Success"), theApp.m_CurrentLoginUser.m_strUserID, theApp.m_CurrentLoginUser.m_strIDCardNo));

			theApp.m_pEqIf->m_pMNetH->SetCardReaderUserData(eWordType_PassWordResult, &pLoginUser);
			theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_PassWordSerarchEnd, OffSet_0, TRUE);

			theApp.m_CurrentLoginUser.Reset();
			theApp.CurrenrUserSave();
		}
	}
}

void CPlcThread::CardReaderCsvFileSave(IDCardReader cardReader)
{
	//1000개 이상이면 뒤에서부터 하나씩 삭제처리

	BOOL bFlag = FALSE;
	if (theApp.m_LoginOutData.size() > 1000)
	{
		bFlag = TRUE;
	}

	if (theApp.m_LoginOutData.size() > 999)
		theApp.m_LoginOutData.pop_back();

	CStdioFile sFile;

	if (!FileExists(LOG_USER_HISTORY_PATH + _T("History")))
		bFlag = TRUE;

	if (bFlag == TRUE)
	{
		if (sFile.Open(LOG_USER_HISTORY_PATH + _T("History"), CFile::modeCreate | CFile::modeWrite) == FALSE)
			return;
	}
	else
	{
		if (sFile.Open(LOG_USER_HISTORY_PATH + _T("History"), CFile::modeWrite) == FALSE)
			return;
	}

	sFile.SeekToEnd();

	CString msg;
	msg.Format(_T("%s,%s,%s,%s,%s,%s,%s"), 
		cardReader.m_strLogintTime, 
		cardReader.m_strLevel,
		cardReader.m_strIDCardNo,
		cardReader.m_strUserID,
		cardReader.m_strUserName,
		cardReader.m_strDivision,
		cardReader.m_strLoginOut);
	sFile.WriteString(msg);

	msg = _T("\n");
	sFile.WriteString(msg);

	sFile.Close();

	theApp.m_LoginOutData.insert(theApp.m_LoginOutData.begin(), cardReader);

	CFile   File;
	CString FileName, strString;
	FileName.Format(_T("%s\\%s_LoginOut.csv"), DATA_INSPECT_LOGIN_OUT_PATH, theApp.m_strCurrentToday);

	BOOL bOpen = FALSE;
	if (!File.Open(FileName, CFile::modeReadWrite | CFile::shareDenyNone))
	{
		if (File.Open(FileName, CFile::modeCreate | CFile::modeWrite))
		{
			bOpen = TRUE;

			strString.Format(_T("TIME,Login/Out,Level,User ID,ID Card No,Division,User Name"));
			strString += "\r\n";
			File.Write(strString.GetBuffer(), strString.GetLength() * 2);
			strString.ReleaseBuffer();
		}

	}
	else bOpen = TRUE;

	if (bOpen){
		File.SeekToEnd();

		strString.Format(_T("%s,%s,%s,%s,%s,%s,%s"),
			GetDateString6(),
			cardReader.m_strLoginOut,
			cardReader.m_strLevel,
			cardReader.m_strUserID,
			cardReader.m_strIDCardNo,
			cardReader.m_strDivision,
			cardReader.m_strUserName);

		strString += "\r\n";

		int iLen = strString.GetLength();
		File.Write(strString.GetBuffer(), iLen * 2);
		strString.ReleaseBuffer();
		File.Close();
	}
}

void CPlcThread::DefectRankClear()
{
	theApp.m_mapRankTotalList[theApp.m_lastShiftIndex].clear();
	theApp.m_mapRankCodeCount[theApp.m_lastShiftIndex].clear();
	map<CString, int> mapRankData;
	vector<pair<CString, ResultCodeRank>> vecRankData;

	for (int ii = 0; ii < NameCount; ii++)
	{
		theApp.m_mapRankTotalList[theApp.m_lastShiftIndex].insert(make_pair(InspectName[ii], vecRankData));
		theApp.m_mapRankCodeCount[theApp.m_lastShiftIndex].insert(make_pair(InspectName[ii], mapRankData));
	}
}

CString CPlcThread::GammaTuningTimeParser(CString strTime)
{
	int iLength = strTime.GetLength();
	CString strTime1 = strTime.Left(iLength - 1);
	CString strTime2 = strTime.Right(1);
	CString strTime3 = strTime1 + _T(".") + strTime2;

	return strTime3;
}

void CPlcThread::JobDataStart(int iNum, int iType)
{
	SBITJobData pBITJobData;
	JobData pJobData;
	CDataInfo dataInfo;

	if (iType == Machine_AOI || iType == Machine_GAMMA)
		theApp.m_pEqIf->m_pMNetH->GetJobData(eWordType_JobData1 + iNum, &pJobData);
	else
		theApp.m_pEqIf->m_pMNetH->GetJobData(eWordType_UnloadJobData1 + iNum, &pJobData);

	dataInfo.m_JobData.Cassette_Sequence_No = Int2String(pJobData.Casssette_Sequence_No);
	dataInfo.m_JobData.Job_Sequence_No = Int2String(pJobData.Job_Sequence_No);
	dataInfo.m_JobData.Group_Index = Int2String(pJobData.Group_Index);
	dataInfo.m_JobData.Product_Type = Int2String(pJobData.Product_Type);

	// PLC Address BIT형으로 받았기에
	// 1. 10진수 -> 2진수
	// 2. 2진수 -> 자릿수 파싱(2진수)
	// 3. 자릿수 파싱(2진수) -> 10진수
	_itoa(pJobData.CST_Operation_Mode, pBITJobData.strCST_Operation_Mode, 2);
	MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<LPCSTR>(pBITJobData.strCST_Operation_Mode), sizeof(pBITJobData.strCST_Operation_Mode), pBITJobData.m_strBitJodata[0].GetBuffer(sizeof(pBITJobData.strCST_Operation_Mode) + 1), sizeof(pBITJobData.strCST_Operation_Mode) + 1);
	pBITJobData.m_strBitJodata[0].ReleaseBuffer(sizeof(pBITJobData.strCST_Operation_Mode));

	pBITJobData.iCST_Operation_Mode = StringBinaryToInt(pBITJobData.m_strBitJodata[0].Left(2));
	pBITJobData.iSubStrate_Type = StringBinaryToInt(pBITJobData.m_strBitJodata[0].Mid(2, 2));
	pBITJobData.iCIM_Mode = StringBinaryToInt(pBITJobData.m_strBitJodata[0].Mid(4, 1));
	pBITJobData.iJob_Type = StringBinaryToInt(pBITJobData.m_strBitJodata[0].Mid(5, 5));
	pBITJobData.iJob_Judge = StringBinaryToInt(pBITJobData.m_strBitJodata[0].Mid(10, 3));
	pBITJobData.iSampling_Slot_Flag = StringBinaryToInt(pBITJobData.m_strBitJodata[0].Mid(13, 1));
	pBITJobData.iFirst_Run = StringBinaryToInt(pBITJobData.m_strBitJodata[0].Mid(14, 1));

	dataInfo.m_JobData.CST_Operation_Mode = Int2String(pBITJobData.iCST_Operation_Mode);
	dataInfo.m_JobData.SubStrate_Type = Int2String(pBITJobData.iSubStrate_Type);
	dataInfo.m_JobData.CIM_Mode = Int2String(pBITJobData.iCIM_Mode);
	dataInfo.m_JobData.Job_Type = Int2String(pBITJobData.iJob_Type);
	dataInfo.m_JobData.Job_Judge = Int2String(pBITJobData.iJob_Judge);
	dataInfo.m_JobData.Sampling_Slot_Flag = Int2String(pBITJobData.iSampling_Slot_Flag);
	dataInfo.m_JobData.First_Run = Int2String(pBITJobData.iFirst_Run);

	dataInfo.m_JobData.Job_Grade = Int2String(pJobData.Job_Grade);
	dataInfo.m_JobData.Job_ID = CStringSupport::ToWString(pJobData.Job_ID, sizeof(pJobData.Job_ID));

	_itoa(pJobData.INSP_Reservation, pBITJobData.strINSP_Reservation, 2);
	MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<LPCSTR>(pBITJobData.strINSP_Reservation), sizeof(pBITJobData.strINSP_Reservation), pBITJobData.m_strBitJodata[1].GetBuffer(sizeof(pBITJobData.strINSP_Reservation) + 1), sizeof(pBITJobData.strINSP_Reservation) + 1);
	pBITJobData.m_strBitJodata[1].ReleaseBuffer(sizeof(pBITJobData.strINSP_Reservation));

	pBITJobData.iINSP_Reservation = StringBinaryToInt(pBITJobData.m_strBitJodata[1].Left(1));
	pBITJobData.iEQP_Reservation = StringBinaryToInt(pBITJobData.m_strBitJodata[1].Mid(1, 1));
	pBITJobData.iLastGlass_Flag = StringBinaryToInt(pBITJobData.m_strBitJodata[1].Mid(2, 1));

	dataInfo.m_JobData.INSP_Reservation = Int2String(pBITJobData.iINSP_Reservation);
	dataInfo.m_JobData.EQP_Reservation = Int2String(pBITJobData.iEQP_Reservation);
	dataInfo.m_JobData.LastGlass_Flag = Int2String(pBITJobData.iLastGlass_Flag);

	dataInfo.m_JobData.InspJudge_Data = Int2String(*pJobData.InspJudge_Data);
	dataInfo.m_JobData.Tracking_Data = Int2String(*pJobData.Tracking_Data);
	dataInfo.m_JobData.EQP_Flag = Int2String(*pJobData.EQP_Flag);
	dataInfo.m_JobData.Chip_Count = Int2String(pJobData.Chip_Count);
	dataInfo.m_JobData.PP_ID = CStringSupport::ToWString(pJobData.PP_ID, sizeof(pJobData.PP_ID));
	dataInfo.m_JobData.FPC_ID = CStringSupport::ToWString(pJobData.FPC_ID, sizeof(pJobData.FPC_ID));
	dataInfo.m_JobData.Cassette_Setting_Code = CStringSupport::ToWString(pJobData.Cassette_Setting_Code, sizeof(pJobData.Cassette_Setting_Code));

	if (theApp.m_bBCTestMode == TRUE)
		dataInfo.m_JobData.BCDataFileExist = TRUE;
	else
		dataInfo.m_JobData.BCDataFileExist = m_bBCDataExistCheck;

	dataInfo.m_JobData.BCDataFileExist = TRUE; // TWICE 일단 무조건 있는걸로 하기

	dataInfo.m_JobData.TypeNum = iType;
	if (iType == Machine_AOI || iType == Machine_ULD)
	{
		CString strName = iType == Machine_AOI ? _T("AOI") : _T("UNLOADER");
		theApp.m_PlcLog->LOG_INFO(_T("[%s] PanelID [%s] FpcID [%s] Data Exists %d JobData Start"), strName, dataInfo.m_JobData.Job_ID, dataInfo.m_JobData.FPC_ID, m_bBCDataExistCheck);
	}
	else
		theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] FpcID [%s] Data Exists %d JobData Start"), dataInfo.m_JobData.Job_ID, dataInfo.m_JobData.FPC_ID, m_bBCDataExistCheck);

	theApp.m_pFS->AddTransferFile(dataInfo.m_JobData);

	if (iType == Machine_AOI || iType == Machine_GAMMA)
		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_JobDataEnd1 + iNum, OffSet_0, TRUE);
	else
		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_UnloadJobDataEnd1 + iNum, OffSet_0, TRUE);
}

void CPlcThread::SumDefectCodeStart(int iNum, int iType, int iOkNg)
{
	m_csSumDefectCode.Lock();

	PanelData pPanelData;
	FpcIDData pFpcData;
	DefectCodeRank pDefectCodeRank;
	DefectGradeRank pDefectGradeRank;

	CString strPanelID, strFpcID;
	CString strCode, strGrade;

#if _SYSTEM_AMTAFT_
	if (iOkNg == OKPanel)
	{
		theApp.m_pEqIf->m_pMNetH->GetPanelData(eWordType_ULD_OK_DefectCodePanelID1 + iNum, &pPanelData);
		strPanelID = CStringSupport::ToWString(pPanelData.m_PanelData, sizeof(pPanelData.m_PanelData));

		theApp.m_pEqIf->m_pMNetH->GetFpcIdData(eWordType_ULD_OK_DefectCodeFpcID1 + iNum, &pFpcData);
		strFpcID = CStringSupport::ToWString(pFpcData.m_FpcIDData, sizeof(pFpcData.m_FpcIDData));
	}
	else
	{
		theApp.m_pEqIf->m_pMNetH->GetPanelData(eWordType_ULD_NG_DefectCodePanelID1 + iNum, &pPanelData);
		strPanelID = CStringSupport::ToWString(pPanelData.m_PanelData, sizeof(pPanelData.m_PanelData));

		theApp.m_pEqIf->m_pMNetH->GetFpcIdData(eWordType_ULD_NG_DefectCodeFpcID1 + iNum, &pFpcData);
		strFpcID = CStringSupport::ToWString(pFpcData.m_FpcIDData, sizeof(pFpcData.m_FpcIDData));
	}
#else
	if (iOkNg == OKPanel)
	{
		theApp.m_pEqIf->m_pMNetH->GetPanelData(eWordType_DefectCodePanelID1 + iNum, &pPanelData);
		strPanelID = CStringSupport::ToWString(pPanelData.m_PanelData, sizeof(pPanelData.m_PanelData));

		theApp.m_pEqIf->m_pMNetH->GetFpcIdData(eWordType_DefectCodeFpcID1 + iNum, &pFpcData);
		strFpcID = CStringSupport::ToWString(pFpcData.m_FpcIDData, sizeof(pFpcData.m_FpcIDData));
	}
	else
	{
		theApp.m_pEqIf->m_pMNetH->GetPanelData(eWordType_GammaNGDefectCodePanelID1 + iNum, &pPanelData);
		strPanelID = CStringSupport::ToWString(pPanelData.m_PanelData, sizeof(pPanelData.m_PanelData));

		theApp.m_pEqIf->m_pMNetH->GetFpcIdData(eWordType_GammaNGDefectCodeFpcID1 + iNum, &pFpcData);
		strFpcID = CStringSupport::ToWString(pFpcData.m_FpcIDData, sizeof(pFpcData.m_FpcIDData));
	}
#endif

	if (strPanelID.IsEmpty())
		strPanelID = strFpcID;

	if (iOkNg == OKPanel)
		theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] FpcID [%s] Sum DefectCode Start OKPanel"), strPanelID, strFpcID);
	else
		theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] FpcID [%s] Sum DefectCode Start NGPanel"), strPanelID, strFpcID);

#if _SYSTEM_AMTAFT_
	CString strCodeGrade = theApp.SetTotalLoadResultCode(strPanelID, strFpcID, Machine_ULD);
#else
	CString strCodeGrade = theApp.GammaDefectInfoLoad(strPanelID, strFpcID);
#endif
	strGrade = _T("OK");
	if (strCodeGrade.IsEmpty() == FALSE)
	{
		CStringArray responseTokens;
		CStringSupport::GetTokenArray(strCodeGrade, _T('^'), responseTokens);

		strCode = responseTokens[0];
		strGrade = responseTokens[1];
		

		CStringSupport::ToAString(strCode, pDefectCodeRank.m_DefectCode, sizeof(pDefectCodeRank.m_DefectCode));
		CStringSupport::ToAString(strGrade, pDefectGradeRank.m_DefectGrade, sizeof(pDefectGradeRank.m_DefectGrade));
	}

	theApp.m_pSendDefectCodeLog->LOG_INFO(_T("[SUM] Panel : [%s][%s] SendPlcDefectCode : [%s][%s]"), strPanelID, strFpcID, strCode, strGrade);

#if _SYSTEM_AMTAFT_
	if (iOkNg == OKPanel)
	{
		theApp.m_pEqIf->m_pMNetH->SetDefectRankData(eWordType_UnloadOKDefectCodeResult1 + iNum, &pDefectCodeRank);
		theApp.m_pEqIf->m_pMNetH->SetDefectGradeRankData(eWordType_UnloadOKDefectGradeResult1 + iNum, &pDefectGradeRank);
		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_ULD_OK_DefectCodeEnd1 + iNum, OffSet_0, TRUE);
	}
	else
	{
		theApp.m_pEqIf->m_pMNetH->SetDefectRankData(eWordType_UnloadNGDefectCodeResult1 + iNum, &pDefectCodeRank);
		theApp.m_pEqIf->m_pMNetH->SetDefectGradeRankData(eWordType_UnloadNGDefectGradeResult1 + iNum, &pDefectGradeRank);
		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_ULD_NG_DefectCodeEnd1 + iNum, OffSet_0, TRUE);
	}
#else
	theApp.m_pEqIf->m_pMNetH->SetDefectRankData(eWordType_DefectCodeResult1 + iNum, &pDefectCodeRank);
	theApp.m_pEqIf->m_pMNetH->SetDefectGradeRankData(eWordType_DefectGradeResult1 + iNum, &pDefectGradeRank);
	if (iOkNg == OKPanel) // 결과말고..END비트만 나눠져있다 왜그랬지? 문제있음 추가하도록하자.
		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_DefectCodeEnd1 + iNum, OffSet_0, TRUE);
	else
		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaNGDefectCodeEnd1 + iNum, OffSet_0, TRUE);
#endif
	
	m_csSumDefectCode.Unlock();

}

void CPlcThread::SumDFSDataStart(int iNum, int iOkNg, int iType)
{
	////////////////////////////////////////////////////////////////////////////////
	// DFS 汇总时序总览（培训用说明）：
	//
	//   PG / TP / OPV / Contact / Gamma 结果
	//         │
	//         ▼
	//     PLC Bit / Word (由 PgManager / TpManager / OpvManager / 其它模块写入)
	//         │
	//         ▼
	//     Flow/Plc 线程
	//         ├─ 轮询各类 END Bit（ContactOnEnd / TouchInspectionEnd / VisionEnd / DFSEnd 等）
	//         ├─ 读取对应结果 Word（ContactResult / TouchResult / VisionResult / DefectCode/Grade 等）
	//         ├─ 生成/更新 DFS 区结构 `DfsData`（写入 PLC Word：eWordType_DFSValue1/UnloadOKDFSValue1/...）
	//         └─ 在适当时机调用本函数 `SumDFSDataStart` 进行 DFS 报工。
	//
	//   本函数职责：
	//     1) 从 PLC DFS 区读取 `DfsData` 结构：
	//          - OK Panel : eWordType_UnloadOKDFSValue1 + iNum (或 eWordType_DFSValue1 + iNum)
	//          - NG Panel : eWordType_UnloadNGDFSValue1 + iNum (或 eWordType_GammaNGDFSValue1 + iNum)
	//     2) 将结构体字段转换成 `DfsDataValue`（字符串形式），包括：
	//          - PanelID / FpcID / ModelID / IndexNum / ChNum
	//          - Start/End/Load/Unload 时间、TactTime、TP/PreGamma 时间
	//          - Contact / PreGamma / TP / AOI/OPV / Lumitop 结果 (OK/NG/BYPASS)
	//          - ContactCount / HandlerNUM 等。
	//     3) 调用 `theApp.m_pFTP->DfsAddTransferFile(pDfsDateValue)`：
	//          将该 Panel 加入 DFS 上传队列，交由 `CDFSClient::RunDfsUploadThread` 异步生成 SUM/INDEX/Image 等。
	//     4) 根据 iOkNg 及机型，在 PLC 中写 DFSEnd Bit：
	//          - AOI 线：eBitType_UnloadOKDFSEnd1/eBitType_UnloadNGDFSEnd1
	//          - Gamma 线：eBitType_DFSEnd1/eBitType_GammaNGDFSEnd1
	//
	//   简略 ASCII 流程（单片）：
	//       (PG/TP/OPV 结果 → PLC → Flow → 写入 DfsData)
	//           │
	//           ▼
	//       CPlcThread::SumDFSDataStart()
	//           ├─ MNetH::GetDfsData(...)  读取 DfsData
	//           ├─ 填充 DfsDataValue      (转字符串、映射 OK/NG 文本)
	//           ├─ theApp.m_pFTP->DfsAddTransferFile(...)
	//           └─ SetPlcBitData(eBitType_UnloadOK/NGDFSEnd1 + iNum, TRUE)
	//
	// ─────────────────────────────────────────────────────────────────────────────
	//   A-Zone 具体时序 & 地址偏移示意（以 TP 为例，单片 Panel）
	//
	//   1) TP 结果写 PLC（CTpManager::OnDataReceived）
	//      - 逻辑：TP → Ani → PLC（MNetH）
	//      - 代码：
	//          OK 时：
	//              SetWordResultOffSet(
	//                  eWordType_AZoneTouchResult + iZoneNum,   // Zone 基地址：A-Zone=0
	//                  iPanelNum,                               // Panel 偏移：0~N（A-Zone 内第几片）
	//                  &m_codeOk);                              // 写入 TP OK Code
	//              SetPlcBitData(
	//                  eBitType_AZoneTouchInspectionEnd + iZoneNum, // A-Zone TP End Bit 基地址
	//                  iPanelNum,                                   // 同样用作 Panel 偏移
	//                  TRUE);                                       // 置位 END Bit
	//
	//          NG 时：
	//              SetWordResultOffSet(eWordType_AZoneTouchResult + iZoneNum, iPanelNum, &m_codeFail);
	//              SetPlcBitData(eBitType_AZoneTouchInspectionEnd + iZoneNum, iPanelNum, TRUE);
	//
	//      - 地址偏移理解：
	//          • Zone 偏移：eWordType_AZoneTouchResult      + iZoneNum     → A/B/C/D-Zone
	//          • Panel 偏移：SetWordResultOffSet(..., iPanelNum, ...)      → 该 Zone 内第几片
	//          • Bit 同理： eBitType_AZoneTouchInspectionEnd + iZoneNum, iPanelNum
	//
	//   2) Flow/Plc 线程汇总到 DFS 结构（MNetH::SetDfsData* 系列，非本函数内部）
	//      - 条件：
	//          • A-ZoneTouchInspectionEnd(Zone, Panel) == ON
	//          • 对应 PG/OPV/Contact/Gamma 等 END Bit 也到位
	//      - 动作：
	//          • 根据 Zone/Panel 计算当前 Stage（iNum）和 DFS 区起始 Word：
	//                eWordType_DFSValue1 + iNum          // Gamma 线（OK）
	//                eWordType_GammaNGDFSValue1 + iNum   // Gamma 线（NG）
	//                eWordType_UnloadOKDFSValue1 + iNum  // AOI 线（OK）
	//                eWordType_UnloadNGDFSValue1 + iNum  // AOI 线（NG）
	//          • 将 A-Zone 的 TP Result 映射到 DfsData：
	//                pDfsData.m_TpResult / m_TpResult2 / m_Contact / m_AOIInpsect / m_OPView ...
	//          • 按固定字段顺序写入 PLC DFS 区（结构体式连写，多 Word 连续区，内部再有字段偏移）。
	//
	//   3) 本函数 SumDFSDataStart 读 DFS 区 → 生成 DfsDataValue → 报工
	//      - 读 DFS 区：
	//            GetDfsData(
	//                eWordType_DFSValue1       + iNum   // 或 UnloadOK/NGDFSValue1 / GammaNGDFSValue1
	//                , &pDfsData);
	//      - 解析时：
	//            pDfsDateValue.m_TpResult  = Int2String(pDfsData.m_TpResult);
	//            pDfsDateValue.m_TpResult2 = pDfsData.m_TpResult2 == 1 ? "OK" : ...;
	//            // Contact / PreGamma / AOI / OPView 等字段同理
	//      - 报工：
	//            theApp.m_pFTP->DfsAddTransferFile(&pDfsDateValue);   // 交给 DFS 线程生成 SUM/INDEX
	//            SetPlcBitData(eBitType_UnloadOK/NGDFSEnd1 + iNum, TRUE); // 通知 PLC：该 Stage DFS 已完成
	//
	//   4) A-Zone 整体信号链路总结（文字版时序，一片 Panel）
	//      TP #RT → CTpManager::OnDataReceived
	//          → SetWordResultOffSet(eWordType_AZoneTouchResult + Zone, Panel, OK/NG)
	//          → SetPlcBitData(eBitType_AZoneTouchInspectionEnd + Zone, Panel, ON)
	//          → Flow/Plc 线程检测到 A-ZoneTouchInspectionEnd ON
	//          → 汇总 PG/OPV/Contact/Gamma 结果，写入 eWordType_*DFSValue1 + Stage
	//          → CPlcThread::SumDFSDataStart(Stage, OK/NG, Type)
	//          → FTP DfsAddTransferFile() → DFS Server 生成 SUM/INDEX/Image → MES/上位。
	//
	// ─────────────────────────────────────────────────────────────────────────────
	//   B-Zone & 其它 Zone（结构 & 偏移的复用关系说明）
	//
	//   1) Word/Bit 基址
	//      - A-Zone 使用：
	//          eWordType_AZoneTouchResult
	//          eBitType_AZoneTouchInspectionEnd
	//      - B/C/D-Zone 只是在此基础上加 Zone 偏移：
	//          eWordType_AZoneTouchResult      + iZoneNum   // iZoneNum = 0(A),1(B),2(C),3(D)...
	//          eBitType_AZoneTouchInspectionEnd + iZoneNum
	//      - 因此：**所有 Zone 的 TP 结果/End Bit 的编码方式完全一致，仅 Zone 偏移不同。**
	//
	//   2) Panel 维度
	//      - 同一 Zone 下的多片 Panel，通过第二参数 iPanelNum 区分：
	//          SetWordResultOffSet(BaseWord + iZoneNum, iPanelNum, ...);
	//          SetPlcBitData(BaseBit + iZoneNum, iPanelNum, TRUE);
	//      - DFS 区写入时，Stage(iNum) 已结合 Zone+Panel 计算好；本函数只关心 iNum，不区分是哪一片 Panel。
	//
	//   3) 对应到 DfsData 结构
	//      - 不论 A/B/C/D-Zone，最终都会汇总到统一的 DfsData 结构：
	//          m_TpResult, m_TpResult2, m_Contact, m_PreGamma, m_OPView, m_AOIInpsect...
	//      - SumDFSDataStart 解读的也是这同一套字段，所以：**Zone 维度已经在 PLC 端折算为 Stage(iNum)**。
	//
	// ─────────────────────────────────────────────────────────────────────────────
	//   Contact（接触测试）信号链路示意
	//
	//   1) Contact 结果写 PLC（接触测试 Handler 模块 → MNetH）
	//      - 逻辑：Contact → Ani → PLC（MNetH）
	//      - 典型代码风格（伪代码，仅说明地址关系）：
	//          OK/NG/BYPASS 时：
	//              SetWordResultOffSet(
	//                  eWordType_ContactResult + iZoneNum,   // 每个 Zone 一组 Contact Result Word
	//                  iPanelNum,                            // Zone 内 Panel 偏移
	//                  &ContactCode);                        // 例如 1=OK, 2=NG, 3=BYPASS
	//              SetPlcBitData(
	//                  eBitType_ContactInspectionEnd + iZoneNum, // Contact End Bit 基址
	//                  iPanelNum,
	//                  TRUE);
	//
	//   2) Flow/Plc 汇总写入 DFS
	//      - 条件：ContactInspectionEnd(Zone, Panel) == ON，且 TP/PG/OPV/Gamma 对应 End Bit 均满足策略。
	//      - 动作：
	//          • 选取对应 Stage(iNum) 的 DFS 区（eWordType_*DFSValue1 + iNum）
	//          • 填写：pDfsData.m_Contact / m_PreGammaContactStatus 等字段
	//
	//   3) SumDFSDataStart 中的解析
	//      - 字段对应关系：
	//            pDfsDateValue.m_Contact = pDfsData.m_Contact == 1 ? _T("OK")
	//                                         : pDfsData.m_Contact == 2 ? _T("NG")
	//                                         : _T("BYPASS");
	//            pDfsDateValue.m_PreGammaContactStatus = Int2String(pDfsData.m_PreGammaContactStatus);
	//      - 对上位来说：Contact 信息只需关心 DfsData 解读结果，不关心具体 Zone/Panel 细节。
	//
	// ─────────────────────────────────────────────────────────────────────────────
	//   OPV（Optical View）信号链路示意
	//
	//   1) OPV 结果写 PLC（COpvManager::OnDataReceived 或类似回调）
	//      - 逻辑：OPV → Ani → PLC（MNetH）
	//      - 地址关系：
	//          • Word：
	//                eWordType_OpvResult + iZoneNum, iPanelNum, &OpvCode
	//                // 与 TP/Contact 一致：Zone 偏移 + Panel 偏移
	//          • Bit：
	//                eBitType_OpvInspectionEnd + iZoneNum, iPanelNum, TRUE
	//
	//   2) 写入 DFS 区
	//      - Flow/Plc 检测到 OpvInspectionEnd ON 后：
	//          • 合并当前 Stage 的 PG/TP/Contact/Gamma/AOI 结果
	//          • 将 OPV 结果写入：pDfsData.m_OPView，对应 DFS 区连续 Word 内的某一字段
	//
	//   3) SumDFSDataStart 中的解析
	//      - 字段对应关系：
	//            pDfsDateValue.m_opViewResult =
	//                    pDfsData.m_OPView == 1 ? _T("OK")
	//                  : pDfsData.m_OPView == 2 ? _T("NG")
	//                  : _T("BYPASS");
	//      - 这样，多个 Panel/Zone 的 OPV 状态最后都折算到按 Stage 汇总的 DFS 记录里。
	//
	// ─────────────────────────────────────────────────────────────────────────────
	//   Gamma / PreGamma（含 GammaNG DFS）的信号链路示意
	//
	//   1) Gamma & PreGamma 测试写 PLC（CPgManager / Gamma Handler）
	//      - 逻辑：Gamma → Ani → PLC（MNetH）
	//      - 地址关系示意：
	//          • PreGamma Result：
	//                eWordType_PreGammaResult + iZoneNum, iPanelNum, &PreGammaCode
	//                eBitType_PreGammaInspectionEnd + iZoneNum, iPanelNum, TRUE
	//          • Gamma Result（若与 PreGamma 分开编码）：
	//                eWordType_GammaResult + iZoneNum, iPanelNum, &GammaCode
	//                eBitType_GammaInspectionEnd + iZoneNum, iPanelNum, TRUE
	//
	//   2) DFS 区分 OK / NG 的地址
	//      - OK Panel：
	//            eWordType_DFSValue1      + iNum   // Gamma 线 OK DFS 区
	//      - NG Panel：
	//            eWordType_GammaNGDFSValue1 + iNum // Gamma 线 NG DFS 区
	//      - AOI 线 OK/NG DFS 同理使用：
	//            eWordType_UnloadOKDFSValue1 + iNum
	//            eWordType_UnloadNGDFSValue1 + iNum
	//
	//   3) SumDFSDataStart 中的解析
	//      - Gamma/PreGamma 相关字段：
	//            pDfsDateValue.m_PreGamma  = pDfsData.m_PreGamma == 1 ? _T("OK")
	//                                         : pDfsData.m_PreGamma == 2 ? _T("NG")
	//                                         : _T("BYPASS");
	//            pDfsDateValue.m_PreGammaTime = Int2String(pDfsData.m_PreGammaTime);
	//            pDfsDateValue.m_Lumitop      = pDfsData.m_Lumitop == 1 ? _T("OK")
	//                                              : pDfsData.m_Lumitop == 2 ? _T("NG")
	//                                              : _T("BYPASS");
	//      - 对比 SumDFSDataStart 开头的 GetDfsData：
	//            • OKPanel → eWordType_DFSValue1       + iNum
	//            • NGPanel → eWordType_GammaNGDFSValue1 + iNum
	//        即：**Gamma 线 OK/NG 使用不同 DFS 段，但内部字段布局一致，由 iOkNg 控制起始基址。**
	////////////////////////////////////////////////////////////////////////////////
	m_csSumDFS.Lock();

	DfsData pDfsData;
	DfsDataValue pDfsDateValue;

#if _SYSTEM_AMTAFT_
	if (iOkNg == OKPanel)
		theApp.m_pEqIf->m_pMNetH->GetDfsData(eWordType_UnloadOKDFSValue1 + iNum, &pDfsData);
	else
		theApp.m_pEqIf->m_pMNetH->GetDfsData(eWordType_UnloadNGDFSValue1 + iNum, &pDfsData);
#else
	if (iOkNg == OKPanel)
		theApp.m_pEqIf->m_pMNetH->GetDfsData(eWordType_DFSValue1 + iNum, &pDfsData);
	else
		theApp.m_pEqIf->m_pMNetH->GetDfsData(eWordType_GammaNGDFSValue1 + iNum, &pDfsData);
#endif

	pDfsDateValue.m_FpcID = CStringSupport::ToWString(pDfsData.m_FpcID, sizeof(pDfsData.m_FpcID));
	pDfsDateValue.m_PanelID = CStringSupport::ToWString(pDfsData.m_PanelID, sizeof(pDfsData.m_PanelID));
	pDfsDateValue.m_StartTime = DFSDataTimeParser(pDfsData.m_StartTime1, pDfsData.m_StartTime2, pDfsData.m_StartTime3);
	pDfsDateValue.m_LoadHandlerTime = DFSDataTimeParser(pDfsData.m_LoadHandlerTime1, pDfsData.m_LoadHandlerTime2, pDfsData.m_LoadHandlerTime3);
	pDfsDateValue.m_UnloadHandlerTime = DFSDataTimeParser(pDfsData.m_UnLoadHandlerTime1, pDfsData.m_UnLoadHandlerTime2, pDfsData.m_UnLoadHandlerTime3);
	pDfsDateValue.m_TpTime = Int2String((pDfsData.m_TPTime));
	pDfsDateValue.m_PreGammaTime = Int2String(pDfsData.m_PreGammaTime);
	pDfsDateValue.m_EndTime = DFSDataTimeParser(pDfsData.m_EndTime1, pDfsData.m_EndTime2, pDfsData.m_EndTime3);
	pDfsDateValue.m_TactTime = DFSDataTactTimeParser(pDfsData.m_StartTime2, pDfsData.m_StartTime3, pDfsData.m_EndTime2, pDfsData.m_EndTime3);
	pDfsDateValue.m_PreGammaContactStatus = Int2String(pDfsData.m_PreGammaContactStatus);
	pDfsDateValue.m_ModelID = CStringSupport::ToWString(pDfsData.m_ModelID, sizeof(pDfsData.m_ModelID));
	pDfsDateValue.m_IndexNum = Int2String(pDfsData.m_IndexNum);
	pDfsDateValue.m_ChNum = Int2String(pDfsData.m_ChNum);
	pDfsDateValue.m_TpResult = Int2String(pDfsData.m_TpResult);
	pDfsDateValue.m_Contact = pDfsData.m_Contact == 1 ? _T("OK") : pDfsData.m_Contact == 2 ? _T("NG") : _T("BYPASS");
	pDfsDateValue.m_PreGamma = pDfsData.m_PreGamma == 1 ? _T("OK") : pDfsData.m_PreGamma == 2 ? _T("NG") : _T("BYPASS");

#if _SYSTEM_AMTAFT_
	// 从 MySQL 查询 AOI 结果（点灯检数据）
	CString strPanelID = CStringSupport::ToWString(pDfsData.m_PanelID, sizeof(pDfsData.m_PanelID));
	CString strFpcID = CStringSupport::ToWString(pDfsData.m_FpcID, sizeof(pDfsData.m_FpcID));
	CString strQueryID = strPanelID.IsEmpty() ? strFpcID : strPanelID;

	CString strUniqueID = _T("");
	CIDMapInfo idMapInfo;
	if (GetDBInterface().QueryIDMapByPanelID(strQueryID, idMapInfo))
	{
		strUniqueID = idMapInfo.UniqueID;
	}

	if (!strUniqueID.IsEmpty())
	{
		CInspectionResult aoiResult;
		if (GetDBInterface().QueryByUniqueID(strUniqueID, aoiResult))
		{
			if (aoiResult.AOIResult.CompareNoCase(_T("OK")) == 0)
				pDfsDateValue.m_AOIInpsect = _T("OK");
			else
				pDfsDateValue.m_AOIInpsect = _T("NG");
			LogWrite(CStringSupport::FormatString(_T("Panel [%s] AOI Result from MySQL: %s, UniqueID: %s"),
				strQueryID, aoiResult.AOIResult, strUniqueID));
		}
		else
		{
			pDfsDateValue.m_AOIInpsect = pDfsData.m_AOIInpsect == 1 ? _T("OK") : pDfsData.m_AOIInpsect == 2 ? _T("NG") : _T("BYPASS");
			LogWrite(CStringSupport::FormatString(_T("Panel [%s] AOI Result from PLC: %s, MySQL Error: %s"),
				strQueryID, pDfsDateValue.m_AOIInpsect, GetDBInterface().GetLastError()));
		}
	}
	else
	{
		pDfsDateValue.m_AOIInpsect = pDfsData.m_AOIInpsect == 1 ? _T("OK") : pDfsData.m_AOIInpsect == 2 ? _T("NG") : _T("BYPASS");
	}
#else
	pDfsDateValue.m_AOIInpsect = pDfsData.m_AOIInpsect == 1 ? _T("OK") : pDfsData.m_AOIInpsect == 2 ? _T("NG") : _T("BYPASS");
#endif

	pDfsDateValue.m_TpResult2 = pDfsData.m_TpResult2 == 1 ? _T("OK") : pDfsData.m_TpResult2 == 2 ? _T("NG") : _T("BYPASS");
	pDfsDateValue.m_Lumitop = pDfsData.m_Lumitop == 1 ? _T("OK") : pDfsData.m_Lumitop == 2 ? _T("NG") : _T("BYPASS");
	pDfsDateValue.m_ContactCount = Int2String(pDfsData.m_ContactCount);
	pDfsDateValue.m_LoadeHandlerNUM = Int2String(pDfsData.m_LoadeHandlerNUM);
	pDfsDateValue.m_UnLoadeHandlerNUM = Int2String(pDfsData.m_UnLoadeHandlerNUM);
	pDfsDateValue.m_opViewResult = pDfsData.m_OPView == 1 ? _T("OK") : pDfsData.m_OPView == 2 ? _T("NG") : _T("BYPASS");;
	pDfsDateValue.m_TypeNum = iType;
	pDfsDateValue.m_StageNum = iNum + 1;

	theApp.m_pDataStatusLog->LOG_INFO(CStringSupport::FormatString(_T(" pDfsDateValue GetEQPDataInfo()_DFS Start time : %s,%d,%d,%d"), pDfsDateValue.m_StartTime, pDfsData.m_StartTime1, pDfsData.m_StartTime2, pDfsData.m_StartTime3));

	if (pDfsDateValue.m_PanelID.IsEmpty())
		pDfsDateValue.m_PanelID = pDfsDateValue.m_FpcID;

	if (iOkNg == OKPanel)
		theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] FpcID [%s] SUM DFS Start OK"), pDfsDateValue.m_PanelID, pDfsDateValue.m_FpcID);
	else
		theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] FpcID [%s] SUM DFS Start NG"), pDfsDateValue.m_PanelID, pDfsDateValue.m_FpcID);
	if (iType == Machine_AOI)
		theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] FpcID [%s] SUM DFS FTP Start iType : Machine_AOI"), pDfsDateValue.m_PanelID, pDfsDateValue.m_FpcID);
	else
		theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] FpcID [%s] SUM DFS FTP Start iType : Machine_ULD"), pDfsDateValue.m_PanelID, pDfsDateValue.m_FpcID);
	theApp.m_pFTP->DfsAddTransferFile(pDfsDateValue);
#if _SYSTEM_AMTAFT_
	if(iType == Machine_AOI)
		theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] FpcID [%s] SUM DFS FTP END iType : Machine_AOI"), pDfsDateValue.m_PanelID, pDfsDateValue.m_FpcID);
	else
		theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] FpcID [%s] SUM DFS FTP END iType : Machine_ULD"), pDfsDateValue.m_PanelID, pDfsDateValue.m_FpcID);

	theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] FpcID [%s] SUM DFS_SendPlcDefectCode Start"), pDfsDateValue.m_PanelID, pDfsDateValue.m_FpcID);
	SendPlcDefectCode(iNum, pDfsDateValue, iType);

	theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] FpcID [%s] SUM DFS_SendPlcDefectCode End"), pDfsDateValue.m_PanelID, pDfsDateValue.m_FpcID);
	//>> 0617
	/*CString strPath, strFilePath, strShift;
	strShift = theApp.m_lastShiftIndex == 0 ? _T("DY") : _T("NT");
	strPath.Format(_T("%s\\%s\\%s_%s"), DATA_DEFECT_CODE_PATH, _T("AOI"), theApp.m_strCurrentToday, strShift);
	strFilePath.Format(_T("%s\\%s.ini"), strPath, pDfsDateValue.m_FpcID);
	DeleteFile(strFilePath);*/


	/*strPath.Format(_T("%s\\%s\\%s_%s"), DATA_DEFECT_CODE_PATH, _T("ULD"), theApp.m_strCurrentToday, strShift);
	strFilePath.Format(_T("%s\\%s.ini"), strPath, pDfsDateValue.m_FpcID);
	DeleteFile(strFilePath);*/
		
	
	//<<

	if (iOkNg == OKPanel)
	{
		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_UnloadOKDFSEnd1 + iNum, OffSet_0, TRUE);
		theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] FpcID [%s] SUM DFS Start OK2"), pDfsDateValue.m_PanelID, pDfsDateValue.m_FpcID);
	}

	else
	{
		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_UnloadNGDFSEnd1 + iNum, OffSet_0, TRUE);
		theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] FpcID [%s] SUM DFS Start NG2"), pDfsDateValue.m_PanelID, pDfsDateValue.m_FpcID);
	}
#else
	if (iOkNg == OKPanel)
		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_DFSEnd1 + iNum, OffSet_0, TRUE);
	else
		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GammaNGDFSEnd1 + iNum, OffSet_0, TRUE);
#endif

	m_csSumDFS.Unlock();
}

void CPlcThread::ProductDataSave(BOOL bFlag)
{
	CFile   File;
	CString strPath, FileName, strString, strTemp, strShift;
	CString strSrc, strDest;
	strShift = theApp.m_lastShiftIndex == 0 ? _T("DY") : _T("NT");

	CString strLineName, str;

	if (!theApp.m_strEqpId.CompareNoCase(_T("MFBAP")))
		strLineName.Format(_T("%s%s"), _T("AMT_"), theApp.m_strEqpNum.Left(1));
	else if (!theApp.m_strEqpId.CompareNoCase(_T("MFGAP")))
		strLineName.Format(_T("%s%s"), _T("AFT_"), theApp.m_strEqpNum.Left(1));
	else if (!theApp.m_strEqpId.CompareNoCase(_T("MFGGA")))
		strLineName.Format(_T("%s%s"), _T("GAMMA_"), theApp.m_strEqpNum.Left(1));

	str = CStringSupport::FormatString(_T("%s"), DATA_TOTAL_CHECK_PATH + strLineName + _T("_HongikData\\"));

	strPath.Format(_T("%s%s_%s"), str, theApp.m_strCurrentToday, strLineName);
	CreateFolders(strPath);

	if (!bFlag)
	{
		strSrc = CStringSupport::FormatString(_T("%s%s_%s_AlarmCount.ini"), DATA_ALARM_COUNT_PATH, theApp.m_strCurrentToday, strShift);
		strDest = CStringSupport::FormatString(_T("%s\\%s_AlarmCount.ini"), strPath, strShift);
		::CopyFile(strSrc, strDest, FALSE);

		strSrc = CStringSupport::FormatString(_T("%s%s_AlarmLog_%s.csv"), DATA_ALARM_PATH, theApp.m_strCurrentToday, strShift);
		strDest = CStringSupport::FormatString(_T("%s\\%s_AlarmLog_%s.csv"), strPath, theApp.m_strCurrentToday, strShift);
		::CopyFile(strSrc, strDest, FALSE);
	}

	if (bFlag)
		FileName.Format(_T("%s\\PM1700_Data_Log_%s.csv"), strPath, strShift);
	else
		FileName.Format(_T("%s\\TotalProductData_Log_%s.csv"), strPath, strShift);

	int iTotal = 0;
	int iTotalGood = 0;
	int iTotalNg = 0;
	int iTotalContact = 0;
	int iTotalGamma = 0;
	int iTotalVision = 0;
	int iTotalViewingAngle = 0;
	int iTotalTp = 0;
	int iTotalAlign = 0;
	int iTotalManulContact = 0;

#if _SYSTEM_AMTAFT_
	for (int ii = 0; ii < MaxZone; ii++)
	{
		iTotal += theApp.m_UiShiftProduction[ii].m_InspectionTotal[theApp.m_lastShiftIndex];
		iTotalGood += theApp.m_UiShiftProduction[ii].m_GoodResult[theApp.m_lastShiftIndex];
		iTotalNg += theApp.m_UiShiftProduction[ii].m_BadResult[theApp.m_lastShiftIndex];
		iTotalContact += theApp.m_UiShiftProduction[ii].m_ContactResult[theApp.m_lastShiftIndex];
		iTotalGamma += theApp.m_UiShiftProduction[ii].m_PreGammaResult[theApp.m_lastShiftIndex];
		iTotalVision += theApp.m_UiShiftProduction[ii].m_VisionResult[theApp.m_lastShiftIndex];
		iTotalViewingAngle += theApp.m_UiShiftProduction[ii].m_ViewingResult[theApp.m_lastShiftIndex];
		iTotalTp += theApp.m_UiShiftProduction[ii].m_TpResult[theApp.m_lastShiftIndex];
		iTotalAlign += theApp.m_UiShiftProduction[ii].m_AlignResult[theApp.m_lastShiftIndex];
	}
#else
	for (int ii = 0; ii < MaxGammaStage; ii++)
	{
		iTotal += theApp.m_UiShiftProduction[ii].m_InspectionTotal[theApp.m_lastShiftIndex];
		iTotalGood += theApp.m_UiShiftProduction[ii].m_GoodResult[theApp.m_lastShiftIndex];
		iTotalNg += theApp.m_UiShiftProduction[ii].m_BadResult[theApp.m_lastShiftIndex];
		iTotalContact += theApp.m_UiShiftProduction[ii].m_ContactResult[theApp.m_lastShiftIndex];
		iTotalGamma += theApp.m_UiShiftProduction[ii].m_MtpResult[theApp.m_lastShiftIndex];
		iTotalManulContact += theApp.m_UiShiftProduction[ii].m_ManualContactResult[theApp.m_lastShiftIndex];
		iTotalAlign += theApp.m_UiShiftProduction[ii].m_AlignResult[theApp.m_lastShiftIndex];
	}
#endif
	BOOL bOpen = FALSE;
	if (!File.Open(FileName, CFile::modeReadWrite | CFile::shareDenyNone))
	{
		if (File.Open(FileName, CFile::modeCreate | CFile::modeWrite))
		{
			bOpen = TRUE;
#if _SYSTEM_AMTAFT_
			if (theApp.m_iMachineType == SetAMT)
				strString.Format(_T("TIME,TOTAL,GOOD,NG,CONTACT NG,GAMMA NG, VISION NG, VIEWING NG, TP NG, ALIGN NG"));
			else
				strString.Format(_T("TIME,TOTAL,GOOD,NG,CONTACT NG,LUMITOP NG, VISION NG, VIEWING NG, TP NG, ALIGN NG"));
#else
			strString.Format(_T("TIME,TOTAL,GOOD,NG,CONTACT NG,GAMMA NG, MANULCONTACT NG, ALIGN NG "));
#endif
			strString += "\r\n";
			File.Write(strString.GetBuffer(), strString.GetLength() * 2);
			strString.ReleaseBuffer();
		}
	}
	else bOpen = TRUE;

	if (bOpen){
		CTime cTime1;

		cTime1 = CTime::GetCurrentTime();
		File.SeekToEnd();
#if _SYSTEM_AMTAFT_
		strString.Format(_T("%s,%d,%d,%d,%d,%d,%d,%d,%d,%d"),
			GetDateString6(), iTotal, iTotalGood, iTotalNg, iTotalContact, iTotalGamma, iTotalVision, iTotalViewingAngle, iTotalTp, iTotalAlign);
#else
		strString.Format(_T("%s,%d,%d,%d,%d,%d,%d,%d"),
			GetDateString6(), iTotal, iTotalGood, iTotalNg, iTotalContact, iTotalGamma, iTotalManulContact, iTotalAlign);
#endif

		strString += "\r\n";

		int iLen = strString.GetLength();
		File.Write(strString.GetBuffer(), iLen * 2);
		strString.ReleaseBuffer();

		strString += "\r\n";

		strString.Format(_T("STAGE,CH,CONTACT NG,GAMMA NG"));
		strString += "\r\n";
		File.Write(strString.GetBuffer(), strString.GetLength() * 2);
		strString.ReleaseBuffer();

		CTime cTime2;

		cTime2 = CTime::GetCurrentTime();
		File.SeekToEnd();

#if _SYSTEM_AMTAFT_
		for (int ii = 0; ii < MaxZone; ii++)
		{
			for (int jj = 0; jj < PanelMaxCount; jj++)
			{
				strString.Format(_T("%s,%d,%d,%d"),
					PG_IndexName[ii],
					jj + 1,
					theApp.m_UiShiftProduction[ii].m_ContactNg[theApp.m_lastShiftIndex][jj],
					theApp.m_UiShiftProduction[ii].m_PreGammaNg[theApp.m_lastShiftIndex][jj]);

				strString += "\r\n";

				int iLen = strString.GetLength();
				File.Write(strString.GetBuffer(), iLen * 2);
				strString.ReleaseBuffer();
			}
		}
#else
		for (int ii = 0; ii < MaxGammaStage; ii++)
		{
			for (int jj = 0; jj < ChMaxCount; jj++)
			{
				strString.Format(_T("%s,%d,%d,%d"),
					PG_IndexName[ii],
					jj + 1,
					theApp.m_UiShiftProduction[ii].m_ContactNg[theApp.m_lastShiftIndex][jj],
					theApp.m_UiShiftProduction[ii].m_MtpNg[theApp.m_lastShiftIndex][jj]);

				strString += "\r\n";

				int iLen = strString.GetLength();
				File.Write(strString.GetBuffer(), iLen * 2);
				strString.ReleaseBuffer();
			}
		}
#endif

		File.Close();
	}
}

CString CPlcThread::DFSDataTimeParser(USHORT Time1, USHORT Time2, USHORT Time3)
{
	char charTemp1[10];
	char charTemp2[10];
	char charTemp3[10];
	CString strTemp1;
	CString strTemp2;
	CString strTemp3;
	CString strTime1;
	CString strTime2;
	CString strTime3;
	CString strResult;
	CString strlog;
	strlog.Format(_T("%04d,%04d,%04d"), Time1, Time2, Time3);
	theApp.m_PlcLog->LOG_INFO(strlog);

	_itoa(Time1, charTemp1, 16);//MMYY
	_itoa(Time2, charTemp2, 16);//HH24DD
	_itoa(Time3, charTemp3, 16);//SSMI

	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<LPCSTR>(charTemp1), 10, strTemp1.GetBuffer(10 + 1), 10 + 1);
	strTemp1.ReleaseBuffer(sizeof(charTemp1));
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<LPCSTR>(charTemp2), 10, strTemp2.GetBuffer(10 + 1), 10 + 1);
	strTemp2.ReleaseBuffer(sizeof(charTemp2));
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<LPCSTR>(charTemp3), 10, strTemp3.GetBuffer(10 + 1), 10 + 1);
	strTemp3.ReleaseBuffer(sizeof(charTemp3));

	strTime1.Format(_T("%s"), strTemp1);
	strTime2.Format(_T("%s"), strTemp2);
	strTime3.Format(_T("%s"), strTemp3);

	if (strTime1.GetLength() == 3)
		strTime1 = _T("0") + strTime1;

	if (strTime2.GetLength() == 3)
		strTime2 = _T("0") + strTime2;

	if (strTime3.GetLength() == 3)
		strTime3 = _T("0") + strTime3;

	strResult = CStringSupport::FormatString(_T("20%s%s%s%s%s%s"), strTime1.Mid(2, 3), strTime1.Left(2),
		strTime2.Mid(2, 3), strTime2.Left(2), strTime3.Mid(2, 3), strTime3.Left(2));

	return strResult;
}


CString CPlcThread::DFSDataTactTimeParser(USHORT Time1, USHORT Time2, USHORT Time3, USHORT Time4)
{
	char charTemp1[10];
	char charTemp2[10];
	char charTemp3[10];
	char charTemp4[10];

	CString strTemp1;
	CString strTemp2;
	CString strTemp3;
	CString strTemp4;

	CString strTime1;
	CString strTime2;
	CString strTime3;
	CString strTime4;
	CString strResult;

	int iStartIme, iEndTime, iResult;
	int iStartTime1, iStartTime2, iStartTime3;
	int iEndime1, iEndime2, iEndime3;

	_itoa(Time1, charTemp1, 16);//HH24DD
	_itoa(Time2, charTemp2, 16);//SSMI
	_itoa(Time3, charTemp3, 16);//HH24DD
	_itoa(Time4, charTemp4, 16);//SSMI

	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<LPCSTR>(charTemp1), 10, strTemp1.GetBuffer(10 + 1), 10 + 1);
	strTemp1.ReleaseBuffer(sizeof(charTemp1));
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<LPCSTR>(charTemp2), 10, strTemp2.GetBuffer(10 + 1), 10 + 1);
	strTemp2.ReleaseBuffer(sizeof(charTemp2));
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<LPCSTR>(charTemp3), 10, strTemp3.GetBuffer(10 + 1), 10 + 1);
	strTemp3.ReleaseBuffer(sizeof(charTemp3));
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<LPCSTR>(charTemp4), 10, strTemp4.GetBuffer(10 + 1), 10 + 1);
	strTemp4.ReleaseBuffer(sizeof(charTemp4));

	strTime1.Format(_T("%s"), strTemp1);
	strTime2.Format(_T("%s"), strTemp2);
	strTime3.Format(_T("%s"), strTemp3);
	strTime4.Format(_T("%s"), strTemp4);

	if (strTime1.GetLength() == 3)
		strTime1 = _T("0") + strTime1;

	if (strTime2.GetLength() == 3)
		strTime2 = _T("0") + strTime2;

	if (strTime3.GetLength() == 3)
		strTime3 = _T("0") + strTime3;

	if (strTime4.GetLength() == 3)
		strTime4 = _T("0") + strTime4;

	//Start Time
	iStartTime1 = _ttoi(strTime1.Left(2));   // 시 
	iStartTime2 = _ttoi(strTime2.Mid(2, 3)); // 분
	iStartTime3 = _ttoi(strTime2.Left(2));   // 초
	iEndime1 = _ttoi(strTime3.Left(2));   // 시 
	iEndime2 = _ttoi(strTime4.Mid(2, 3)); // 분
	iEndime3 = _ttoi(strTime4.Left(2));   // 초

	iStartIme = (iStartTime1 * 3600) + (iStartTime2 * 60) + iStartTime3;
	iEndTime = (iEndime1 * 3600) + (iEndime2 * 60) + iEndime3;
	
	if (iEndTime - iStartIme < 0)
		iResult = iEndTime - iStartIme + 86400;
	else
		iResult = iEndTime - iStartIme;

	strResult.Format(_T("%f"), (float)(iResult/10.0));

	return strResult;
}

#if _SYSTEM_AMTAFT_
void CPlcThread::DefectCodeStart(int iNum)
{
	m_csDefectCode.Lock();

	PanelData pPanelData;
	FpcIDData pFpcData;
	DefectCodeRank pDefectCodeRank;
	DefectGradeRank pDefectGradeRank;

	CString strPanelID, strFpcID;
	CString strCode, strGrade;

	theApp.m_pEqIf->m_pMNetH->GetPanelData(eWordType_DefectCodePanelID1 + iNum, &pPanelData);
	strPanelID = CStringSupport::ToWString(pPanelData.m_PanelData, sizeof(pPanelData.m_PanelData));

	theApp.m_pEqIf->m_pMNetH->GetFpcIdData(eWordType_DefectCodeFpcID1 + iNum, &pFpcData);
	strFpcID = CStringSupport::ToWString(pFpcData.m_FpcIDData, sizeof(pFpcData.m_FpcIDData));

	if (strPanelID.IsEmpty())
		strPanelID = strFpcID;
	//220316 START
	theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] FpcID [%s] AOI DefectCode Start [%d]"), strPanelID, strFpcID, iNum);
	//220316 End
	CString strCodeGrade = theApp.SetTotalLoadResultCode(strPanelID, strFpcID, Machine_AOI);

	theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] FpcID [%s] AOI DefectCode [%s] Start [%d]"), strPanelID, strFpcID, strCodeGrade, iNum);

	if (strCodeGrade.IsEmpty() == FALSE)
	{
		CStringArray responseTokens;
		CStringSupport::GetTokenArray(strCodeGrade, _T('^'), responseTokens);

		strCode = responseTokens[0];
		strGrade = responseTokens[1];
		//>> Index ok grad 220112
		if (strCode == "OK")
			strGrade = theApp.m_strIndexOkGrade;

		CStringSupport::ToAString(strCode, pDefectCodeRank.m_DefectCode, sizeof(pDefectCodeRank.m_DefectCode));
		CStringSupport::ToAString(strGrade, pDefectGradeRank.m_DefectGrade, sizeof(pDefectGradeRank.m_DefectGrade));
	}
	else
	{
		theApp.m_PlcLog->LOG_INFO(_T("strCodeGrade Error"));
	}
	//220316 START
	//theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] FpcID [%s] AOI DefectCode End"), strPanelID, strFpcID);
	theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] FpcID [%s] AOI DefectCode [%s] End [%d]"), strPanelID, strFpcID, strCodeGrade, iNum);
	theApp.m_PlcLog->LOG_INFO(_T("[AOI] Panel : [%s][%s] SendPlcDefectCode : [%s][%s]"), strPanelID, strFpcID, strCode, strGrade);
	//220316 End

	theApp.m_pSendDefectCodeLog->LOG_INFO(_T("[AOI] Panel : [%s][%s] SendPlcDefectCode : [%s][%s]"), strPanelID, strFpcID, strCode, strGrade);

	theApp.m_pEqIf->m_pMNetH->SetDefectRankData(eWordType_DefectCodeResult1 + iNum, &pDefectCodeRank);
	theApp.m_pEqIf->m_pMNetH->SetDefectGradeRankData(eWordType_DefectGradeResult1 + iNum, &pDefectGradeRank);
	theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_DefectCodeEnd1 + iNum, OffSet_0, TRUE);


	/*CString strPath, strFilePath, strShift; 
	strShift = theApp.m_lastShiftIndex == 0 ? _T("DY") : _T("NT");
	strPath.Format(_T("%s\\%s\\%s_%s"), DATA_DEFECT_CODE_PATH, _T("AOI"), theApp.m_strCurrentToday, strShift);

	CreateFolders(strPath);
	strFilePath.Format(_T("%s\\%s.ini"), strPath, strFpcID);
	BOOL retVal = DeleteFile(strFilePath);*/

	m_csDefectCode.Unlock();
}

void CPlcThread::DFSDataStart(int iNum, int iOkNg, int iType)
{
	m_csDFS.Lock();

	DfsData pDfsData;
	DfsDataValue pDfsDateValue;

#if _SYSTEM_AMTAFT_
	// 根据 OK/NG 从不同的 PLC 地址读取
	if (iOkNg == OKPanel)
		theApp.m_pEqIf->m_pMNetH->GetDfsData(eWordType_UnloadOKDFSValue1 + iNum, &pDfsData);
	else
		theApp.m_pEqIf->m_pMNetH->GetDfsData(eWordType_UnloadNGDFSValue1 + iNum, &pDfsData);
#else
	if (iOkNg == OKPanel)
		theApp.m_pEqIf->m_pMNetH->GetDfsData(eWordType_DFSValue1 + iNum, &pDfsData);
	else
		theApp.m_pEqIf->m_pMNetH->GetDfsData(eWordType_GammaNGDFSValue1 + iNum, &pDfsData);
#endif

	//pDfsDateValue.m_FpcID = CStringSupport::ToWString(pDfsData.m_FpcID, sizeof(pDfsData.m_FpcID));
	//pDfsDateValue.m_PanelID = CStringSupport::ToWString(pDfsData.m_PanelID, sizeof(pDfsData.m_PanelID));
	//pDfsDateValue.m_StartTime = DFSDataTimeParser(pDfsData.m_StartTime1, pDfsData.m_StartTime2, pDfsData.m_StartTime3);
	//pDfsDateValue.m_LoadHandlerTime = DFSDataTimeParser(pDfsData.m_LoadHandlerTime1, pDfsData.m_LoadHandlerTime2, pDfsData.m_LoadHandlerTime3);
	//pDfsDateValue.m_UnloadHandlerTime = DFSDataTimeParser(pDfsData.m_UnLoadHandlerTime1, pDfsData.m_UnLoadHandlerTime2, pDfsData.m_UnLoadHandlerTime3);
	//pDfsDateValue.m_TpTime = Int2String(pDfsData.m_TPTime);
	//pDfsDateValue.m_PreGammaTime = Int2String(pDfsData.m_PreGammaTime);
	//pDfsDateValue.m_EndTime = DFSDataTimeParser(pDfsData.m_EndTime1, pDfsData.m_EndTime2, pDfsData.m_EndTime3);
	//pDfsDateValue.m_TactTime = DFSDataTactTimeParser(pDfsData.m_StartTime2, pDfsData.m_StartTime3, pDfsData.m_EndTime2, pDfsData.m_EndTime3);
	//pDfsDateValue.m_PreGammaContactStatus = Int2String(pDfsData.m_PreGammaContactStatus);
	//pDfsDateValue.m_ModelID = CStringSupport::ToWString(pDfsData.m_ModelID, sizeof(pDfsData.m_ModelID));
	//pDfsDateValue.m_IndexNum = Int2String(pDfsData.m_IndexNum);
	//pDfsDateValue.m_ChNum = Int2String(pDfsData.m_ChNum);
	//pDfsDateValue.m_TpResult = Int2String(pDfsData.m_TpResult);
	///*pDfsDateValue.m_TpResult2 = pDfsData.m_TpResult2 == 0 ? _T("OK") : pDfsData.m_TpResult2 == 1 ? _T("NG") : _T("BYPASS");
	//pDfsDateValue.m_Contact = pDfsData.m_Contact == 0 ? _T("OK") : pDfsData.m_Contact == 1 ? _T("NG") : _T("BYPASS");;
	//pDfsDateValue.m_PreGamma = pDfsData.m_PreGamma == 0 ? _T("OK") : pDfsData.m_PreGamma == 1 ? _T("NG") : _T("BYPASS");;
	//pDfsDateValue.m_AOIInpsect = pDfsData.m_AOIInpsect == 0 ? _T("OK") : pDfsData.m_AOIInpsect == 1 ? _T("NG") : _T("BYPASS");;
	//pDfsDateValue.m_Lumitop = pDfsData.m_Lumitop == 0 ? _T("OK") : pDfsData.m_Lumitop == 1 ? _T("NG") : _T("BYPASS");;
	//pDfsDateValue.m_mura = pDfsData.m_mura == 0 ? _T("OK") : pDfsData.m_mura == 1 ? _T("NG") : _T("BYPASS");;*/
	//pDfsDateValue.m_TypeNum = iType;

	pDfsDateValue.m_FpcID = CStringSupport::ToWString(pDfsData.m_FpcID, sizeof(pDfsData.m_FpcID));
	pDfsDateValue.m_PanelID = CStringSupport::ToWString(pDfsData.m_PanelID, sizeof(pDfsData.m_PanelID));
	pDfsDateValue.m_StartTime = DFSDataTimeParser(pDfsData.m_StartTime1, pDfsData.m_StartTime2, pDfsData.m_StartTime3);
	pDfsDateValue.m_LoadHandlerTime = DFSDataTimeParser(pDfsData.m_LoadHandlerTime1, pDfsData.m_LoadHandlerTime2, pDfsData.m_LoadHandlerTime3);
	pDfsDateValue.m_UnloadHandlerTime = DFSDataTimeParser(pDfsData.m_UnLoadHandlerTime1, pDfsData.m_UnLoadHandlerTime2, pDfsData.m_UnLoadHandlerTime3);
	pDfsDateValue.m_TpTime = Int2String((pDfsData.m_TPTime));
	pDfsDateValue.m_PreGammaTime = Int2String(pDfsData.m_PreGammaTime);
	pDfsDateValue.m_EndTime = DFSDataTimeParser(pDfsData.m_EndTime1, pDfsData.m_EndTime2, pDfsData.m_EndTime3);
	pDfsDateValue.m_TactTime = DFSDataTactTimeParser(pDfsData.m_StartTime2, pDfsData.m_StartTime3, pDfsData.m_EndTime2, pDfsData.m_EndTime3);
	pDfsDateValue.m_PreGammaContactStatus = Int2String(pDfsData.m_PreGammaContactStatus);
	pDfsDateValue.m_ModelID = CStringSupport::ToWString(pDfsData.m_ModelID, sizeof(pDfsData.m_ModelID));
	pDfsDateValue.m_IndexNum = Int2String(pDfsData.m_IndexNum);
	pDfsDateValue.m_ChNum = Int2String(pDfsData.m_ChNum);
	pDfsDateValue.m_TpResult = Int2String(pDfsData.m_TpResult);
	pDfsDateValue.m_Contact = pDfsData.m_Contact == 1 ? _T("OK") : pDfsData.m_Contact == 2 ? _T("NG") : _T("BYPASS");
	pDfsDateValue.m_PreGamma = pDfsData.m_PreGamma == 1 ? _T("OK") : pDfsData.m_PreGamma == 2 ? _T("NG") : _T("BYPASS");

#if _SYSTEM_AMTAFT_
	// 从 MySQL 查询 AOI 结果（点灯检数据）
	CString strPanelID = CStringSupport::ToWString(pDfsData.m_PanelID, sizeof(pDfsData.m_PanelID));
	CString strFpcID = CStringSupport::ToWString(pDfsData.m_FpcID, sizeof(pDfsData.m_FpcID));
	CString strQueryID = strPanelID.IsEmpty() ? strFpcID : strPanelID;

	CString strUniqueID = _T("");
	CIDMapInfo idMapInfo;
	if (GetDBInterface().QueryIDMapByPanelID(strQueryID, idMapInfo))
	{
		strUniqueID = idMapInfo.UniqueID;
	}

	if (!strUniqueID.IsEmpty())
	{
		CInspectionResult aoiResult;
		if (GetDBInterface().QueryByUniqueID(strUniqueID, aoiResult))
		{
			if (aoiResult.AOIResult.CompareNoCase(_T("OK")) == 0)
				pDfsDateValue.m_AOIInpsect = _T("OK");
			else
				pDfsDateValue.m_AOIInpsect = _T("NG");
			LogWrite(CStringSupport::FormatString(_T("Panel [%s] AOI Result from MySQL: %s, UniqueID: %s"),
				strQueryID, aoiResult.AOIResult, strUniqueID));
		}
		else
		{
			pDfsDateValue.m_AOIInpsect = pDfsData.m_AOIInpsect == 1 ? _T("OK") : pDfsData.m_AOIInpsect == 2 ? _T("NG") : _T("BYPASS");
			LogWrite(CStringSupport::FormatString(_T("Panel [%s] AOI Result from PLC: %s, MySQL Error: %s"),
				strQueryID, pDfsDateValue.m_AOIInpsect, GetDBInterface().GetLastError()));
		}
	}
	else
	{
		pDfsDateValue.m_AOIInpsect = pDfsData.m_AOIInpsect == 1 ? _T("OK") : pDfsData.m_AOIInpsect == 2 ? _T("NG") : _T("BYPASS");
	}
#else
	pDfsDateValue.m_AOIInpsect = pDfsData.m_AOIInpsect == 1 ? _T("OK") : pDfsData.m_AOIInpsect == 2 ? _T("NG") : _T("BYPASS");
#endif

	pDfsDateValue.m_TpResult2 = pDfsData.m_TpResult2 == 1 ? _T("OK") : pDfsData.m_TpResult2 == 2 ? _T("NG") : _T("BYPASS");
	pDfsDateValue.m_Lumitop = pDfsData.m_Lumitop == 1 ? _T("OK") : pDfsData.m_Lumitop == 2 ? _T("NG") : _T("BYPASS");
	pDfsDateValue.m_ContactCount = Int2String(pDfsData.m_ContactCount);
	pDfsDateValue.m_LoadeHandlerNUM = Int2String(pDfsData.m_LoadeHandlerNUM);
	pDfsDateValue.m_UnLoadeHandlerNUM = Int2String(pDfsData.m_UnLoadeHandlerNUM);
	pDfsDateValue.m_opViewResult = pDfsData.m_OPView == 1 ? _T("OK") : pDfsData.m_OPView == 2 ? _T("NG") : _T("BYPASS");;
	pDfsDateValue.m_TypeNum = iType;
	pDfsDateValue.m_StageNum = iNum + 1;

	if (pDfsDateValue.m_PanelID.IsEmpty())
		pDfsDateValue.m_PanelID = pDfsDateValue.m_FpcID;
	if (iType == Machine_AOI)
		theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] FpcID [%s] AOI DFS Start iType : [Machine_AOI_%d]"), pDfsDateValue.m_PanelID, pDfsDateValue.m_FpcID, iType);
	else
		theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] FpcID [%s] AOI DFS Start iType : [Machine_ULD_%d]"), pDfsDateValue.m_PanelID, pDfsDateValue.m_FpcID, iType);
	theApp.m_PlcLog->LOG_INFO(_T("*******DFSDataStart -- MMYY : %d, HH24DD : %d, SSMI : %d **********"), pDfsData.m_StartTime1, pDfsData.m_StartTime2, pDfsData.m_StartTime3); // 20200406 kty
	
	theApp.m_pFTP->AddTransferFile(pDfsDateValue);

	//220330 OPVDFS COPY

	theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] FpcID [%s] AOI DFS OPV Copy Start"), pDfsDateValue.m_PanelID, pDfsDateValue.m_FpcID);

	CString strPath, strFilePath, strCodeCount, strShift, strCodeGrade;
	
	strShift = theApp.m_lastShiftIndex == 0 ? _T("DY") : _T("NT");
	strPanelID = pDfsDateValue.m_PanelID;
	
	strPath = DFS_SHARE_OPVDFS_PATH + GetDateString2() + _T("\\") + strPanelID + _T("\\") + strPanelID + _T(".csv");

	strFilePath.Format(_T("%s\\%s\\%s_%s\\%s\\"), DATA_OPVDFS_PATH, _T("AOI"), theApp.m_strCurrentToday, strShift, strPanelID); // 0628
	CreateFolders(strFilePath);

	strFilePath = strFilePath + strPanelID + _T(".csv");
	BOOL bCopy_result = FALSE;

	bCopy_result = CopyFile(strPath, strFilePath, FALSE);
	if (bCopy_result == TRUE)
		theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] Copy OK"), pDfsDateValue.m_PanelID);
	else
		theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] Copy NG"), pDfsDateValue.m_PanelID);

	theApp.m_PlcLog->LOG_INFO(_T("PanelID [%s] FpcID [%s] AOI DFS OPV Copy End"), pDfsDateValue.m_PanelID, pDfsDateValue.m_FpcID);
	/////////////////////////
	SendPlcDefectCode(iNum, pDfsDateValue, iType);



	theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_DFSEnd1 + iNum, OffSet_0, TRUE);

	m_csDFS.Unlock();
}

void CPlcThread::SendPlcDefectCode(int iNum, DfsDataValue PanelData, int iType)
{
	map<CString, CString>::iterator iter;
	DefectCodeRank pDefectCodeRank;
	DefectGradeRank pDefectGradeRank;
	PLCSendDefect PlcSendDefect;
	CString strCodeGrade, strCode = _T(""), strGrade = _T(""), strPanelID = _T(""), strFpcID = _T("");
	strPanelID = PanelData.m_PanelID;
	strFpcID = PanelData.m_FpcID;
	int iCount = 0;
	theApp.m_PlcLog->LOG_INFO(_T("SendPlcDefectCode Start PanelID [%s] FpcID [%s]  iType : [%d]"), strPanelID, strFpcID, iType);
	if (iType == Machine_AOI)
	{
		theApp.m_PlcLog->LOG_INFO(_T("strCodeGrade Start PanelID [%s] FpcID [%s]  Machine_AOI"), strPanelID, strFpcID);
		if (_ttoi(PanelData.m_PreGammaContactStatus) == m_dfsContactNG)
			strCodeGrade = CStringSupport::FormatString(_T("%s^%s"), theApp.m_strContactNgCode, theApp.m_strContactNgGrade);
		else if (_ttoi(PanelData.m_TpResult) == m_dfsTpNG)
			strCodeGrade = CStringSupport::FormatString(_T("%s^%s"), _T("XIMXDE"), _T("R1"));
		else if (_ttoi(PanelData.m_PreGammaContactStatus) == m_dfsPreGammaNG)
			strCodeGrade = CStringSupport::FormatString(_T("%s^%s"), _T("XIMXPG"), _T("R1"));
		else
		{
			theApp.SetLoadResultCode(strPanelID, strFpcID);

			if (theApp.m_Send_Result_Code_Map.size() > 0)
			{
				for (auto Rank : theApp.m_VecRank[INDEX])
				{
					if (iCount == theApp.m_iNumberSendToPlc)
						break;

					iter = theApp.m_Send_Result_Code_Map.find(Rank.strCode);
					if (iter != theApp.m_Send_Result_Code_Map.end())
					{
						iCount++;
						if (iCount == 1)
						{
							strCode.Format(_T("%s"), iter->first);
							strGrade = Rank.strGrade; // DataServer에서 설정한 것으로 쓰게끔.
						}
					}
				}
			}

			if (strCode.IsEmpty() == FALSE && strGrade.IsEmpty() == FALSE)
				strCodeGrade.Format(_T("%s^%s"), strCode, strGrade);
			else
				strCodeGrade = _T("");
		}
		theApp.m_PlcLog->LOG_INFO(_T("strCodeGrade End PanelID [%s] FpcID [%s]  Machine_AOI"), strPanelID, strFpcID);
		theApp.m_pTestLog->LOG_INFO(_T("strCodeGrade End PanelID [%s] FpcID [%s]  Machine_AOI"), strPanelID, strFpcID);
	}
	else
	{
		theApp.m_PlcLog->LOG_INFO(_T("strCodeGrade Start PanelID [%s] FpcID [%s]  Machine_ULD"), strPanelID, strFpcID);
		if (_ttoi(PanelData.m_PreGammaContactStatus) == m_dfsContactNG)
			strCodeGrade = CStringSupport::FormatString(_T("%s^%s"), theApp.m_strContactNgCode, theApp.m_strContactNgGrade);
		else if (_ttoi(PanelData.m_PreGammaContactStatus) == m_dfsPreGammaNG)
			strCodeGrade = CStringSupport::FormatString(_T("%s^%s"), _T("XIMXPG"), _T("R1"));
		else
			strCodeGrade = theApp.SetLoadOpvResultCode(strPanelID);

		if (strCodeGrade == _T(""))
		{
			strCodeGrade = theApp.SetTotalLoadResultCode(strPanelID, strFpcID, Machine_AOI);
		}
		theApp.m_PlcLog->LOG_INFO(_T("strCodeGrade End PanelID [%s] FpcID [%s]  Machine_ULD"), strPanelID, strFpcID);
		theApp.m_pTestLog->LOG_INFO(_T("strCodeGrade End PanelID [%s] FpcID [%s]  Machine_ULD"), strPanelID, strFpcID);
	}


	
	if (strCodeGrade.IsEmpty() == FALSE)
	{
		CStringArray responseTokens;
		CStringSupport::GetTokenArray(strCodeGrade, _T('^'), responseTokens);
		
		strCode = responseTokens[0];
		strGrade = responseTokens[1];

		PlcSendDefect.m_strCode = strCode;
		PlcSendDefect.m_strGrade = strGrade;
		PlcSendDefect.m_iCount = theApp.m_iNumberSendToPlc;
		theApp.SetSaveResultCode(strPanelID, strFpcID, _T("TotalDefectCode"), PlcSendDefect, iType);
		theApp.m_pTestLog->LOG_DEBUG(_T("SetSaveResultCode[TotalDefectCode] PanelID : %s, strCode : %s, strGrade : %s,"),
			strPanelID, PlcSendDefect.m_strCode, PlcSendDefect.m_strGrade);
		
	}
	else
	{
		PlcSendDefect.m_strCode = _T("");
		PlcSendDefect.m_strGrade = theApp.m_strOkGrade;
		PlcSendDefect.m_iCount = theApp.m_iNumberSendToPlc;
		theApp.SetSaveResultCode(strPanelID, strFpcID, _T("TotalDefectCode"), PlcSendDefect, iType);
		theApp.m_pTestLog->LOG_DEBUG(_T("SetSaveResultCode[TotalDefectCode] PanelID : %s, strCode : %s, strGrade : %s,"),
			strPanelID, PlcSendDefect.m_strCode, PlcSendDefect.m_strGrade);
	}

	theApp.m_Send_Result_Code_Map.clear();
}

void CPlcThread::AOIInspectDataParser(int iPanelNum, int iCommand)
{
	m_csData.Lock();
	DataStatus pDataStatus;
	CString strCell_ID, strFpc_ID, strStageNum;
	int iCurrentNum;
	DataStatusItem dataItem;
	dataItem.Reset();

	BOOL bNgFlag = FALSE;

	if (iCommand == Data_TrayOut)
		theApp.m_pEqIf->m_pMNetH->GetDataStatus(eWordType_TrayReportValue1 + iPanelNum, &pDataStatus);
	else
		theApp.m_pEqIf->m_pMNetH->GetDataStatus(eWordType_DataReportValue1 + iPanelNum, &pDataStatus);

	strCell_ID = CStringSupport::ToWString(pDataStatus.m_PanelData, sizeof(pDataStatus.m_PanelData));
	strFpc_ID = CStringSupport::ToWString(pDataStatus.m_FpcData, sizeof(pDataStatus.m_FpcData));

	iCurrentNum = pDataStatus.m_IndexNumStatus - 1;
	strStageNum.Format(_T("%d"), iCurrentNum + 1);

	theApp.m_shiftProduction[iCurrentNum].m_InspectionTotal[theApp.m_lastShiftIndex]++;
	theApp.m_UiShiftProduction[iCurrentNum].m_InspectionTotal[theApp.m_lastShiftIndex]++;
	theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_InspectionTotal[theApp.m_lastShiftIndex]++;;
	theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_InspectionTotal[theApp.m_lastShiftIndex]++;;

	if (pDataStatus.m_ContactStatus == m_codeFail)
	{
		theApp.m_shiftProduction[iCurrentNum].m_ContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShiftProduction[iCurrentNum].m_ContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_ContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_ContactResult[theApp.m_lastShiftIndex]++;

		theApp.m_shiftProduction[iCurrentNum].m_ContactNg[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShiftProduction[iCurrentNum].m_ContactNg[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_ContactNg[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_ContactNg[theApp.m_lastShiftIndex][iPanelNum]++;
		dataItem.DataContactStatus = _T("NG");
		bNgFlag = TRUE;
	}
	else if (pDataStatus.m_ContactStatus == m_codeOk)
	{
		theApp.m_shiftProduction[iCurrentNum].m_ContactGood[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShiftProduction[iCurrentNum].m_ContactGood[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_ContactGood[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_ContactGood[theApp.m_lastShiftIndex][iPanelNum]++;
		dataItem.DataContactStatus = _T("GOOD");
	}

	if (pDataStatus.m_FirstContactStatus == m_codeFail)
	{
		theApp.m_shiftProduction[iCurrentNum].m_FirstContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShiftProduction[iCurrentNum].m_FirstContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_FirstContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_FirstContactResult[theApp.m_lastShiftIndex]++;

		theApp.m_shiftProduction[iCurrentNum].m_FirstContactNG[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShiftProduction[iCurrentNum].m_FirstContactNG[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_FirstContactNG[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_FirstContactNG[theApp.m_lastShiftIndex][iPanelNum]++;
		dataItem.DataFirstContactStatus = _T("NG");
	}

	if (pDataStatus.m_TpStatus == m_codeFail)
	{
		theApp.m_shiftProduction[iCurrentNum].m_TpResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShiftProduction[iCurrentNum].m_TpResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_TpResult[theApp.m_lastShiftIndex]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_TpResult[theApp.m_lastShiftIndex]++;

		theApp.m_shiftProduction[iCurrentNum].m_TpNg[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShiftProduction[iCurrentNum].m_TpNg[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_TpNg[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_TpNg[theApp.m_lastShiftIndex][iPanelNum]++;
		dataItem.DataTpStatus = _T("NG");
		bNgFlag = TRUE;
	}
	else if (pDataStatus.m_TpStatus == m_codeOk)
	{
		theApp.m_shiftProduction[iCurrentNum].m_TpGood[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShiftProduction[iCurrentNum].m_TpGood[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_TpGood[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_TpGood[theApp.m_lastShiftIndex][iPanelNum]++;
		dataItem.DataTpStatus = _T("GOOD");
	}

	if (pDataStatus.m_OtpStatus == m_codeFail)
	{
		theApp.m_shiftProduction[iCurrentNum].m_PreGammaResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShiftProduction[iCurrentNum].m_PreGammaResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_PreGammaResult[theApp.m_lastShiftIndex]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_PreGammaResult[theApp.m_lastShiftIndex]++;

		theApp.m_shiftProduction[iCurrentNum].m_PreGammaNg[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShiftProduction[iCurrentNum].m_PreGammaNg[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_PreGammaNg[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_PreGammaNg[theApp.m_lastShiftIndex][iPanelNum]++;
		dataItem.DataOtpStatus = _T("NG");
		bNgFlag = TRUE;
	}
	else if (pDataStatus.m_OtpStatus == m_codeOk)
	{
		theApp.m_shiftProduction[iCurrentNum].m_PreGammaGood[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShiftProduction[iCurrentNum].m_PreGammaGood[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_PreGammaGood[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_PreGammaGood[theApp.m_lastShiftIndex][iPanelNum]++;
		dataItem.DataOtpStatus = _T("GOOD");
	}

	if (pDataStatus.m_VisionStatus == m_codeFail)
	{
		theApp.m_shiftProduction[iCurrentNum].m_VisionResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShiftProduction[iCurrentNum].m_VisionResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_VisionResult[theApp.m_lastShiftIndex]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_VisionResult[theApp.m_lastShiftIndex]++;
		dataItem.DataVisionStatus = _T("NG");
		bNgFlag = TRUE;
	}
	else if (pDataStatus.m_VisionStatus == m_codeOk)
	{
		dataItem.DataVisionStatus = _T("GOOD");
	}

	if (pDataStatus.m_ViewingStatus == m_codeFail)
	{
		theApp.m_shiftProduction[iCurrentNum].m_ViewingResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShiftProduction[iCurrentNum].m_ViewingResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_ViewingResult[theApp.m_lastShiftIndex]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_ViewingResult[theApp.m_lastShiftIndex]++;
		dataItem.DataViewingStatus = _T("NG");
		bNgFlag = TRUE;
	}
	else if (pDataStatus.m_ViewingStatus == m_codeOk)
	{
		dataItem.DataViewingStatus = _T("GOOD");
	}

	if (pDataStatus.m_TryInsertStatus == m_TrayInsert)
	{
		theApp.m_shiftProduction[iCurrentNum].m_TrayInsertstatus[theApp.m_lastShiftIndex]++;
		theApp.m_UiShiftProduction[iCurrentNum].m_TrayInsertstatus[theApp.m_lastShiftIndex]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_TrayInsertstatus[theApp.m_lastShiftIndex]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_TrayInsertstatus[theApp.m_lastShiftIndex]++;
		dataItem.DataInStatus = _T("Tray");
	}
	else
	{
		theApp.m_shiftProduction[iCurrentNum].m_UpperInsertstatus[theApp.m_lastShiftIndex]++;
		theApp.m_UiShiftProduction[iCurrentNum].m_UpperInsertstatus[theApp.m_lastShiftIndex]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_UpperInsertstatus[theApp.m_lastShiftIndex]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_UpperInsertstatus[theApp.m_lastShiftIndex]++;
		dataItem.DataInStatus = _T("Upper");
	}

	if (iCommand == Data_TrayOut)
	{
		theApp.m_shiftProduction[iCurrentNum].m_TrayDataOut[theApp.m_lastShiftIndex]++;
		theApp.m_UiShiftProduction[iCurrentNum].m_TrayDataOut[theApp.m_lastShiftIndex]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_TrayDataOut[theApp.m_lastShiftIndex]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_TrayDataOut[theApp.m_lastShiftIndex]++;
		dataItem.DataOutStatus = _T("Tray");
	}
	else
	{
		theApp.m_shiftProduction[iCurrentNum].m_LowerDataOut[theApp.m_lastShiftIndex]++;
		theApp.m_UiShiftProduction[iCurrentNum].m_LowerDataOut[theApp.m_lastShiftIndex]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_LowerDataOut[theApp.m_lastShiftIndex]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_LowerDataOut[theApp.m_lastShiftIndex]++;
		dataItem.DataOutStatus = _T("Lower");
	}

	if (bNgFlag == TRUE)
	{
		theApp.m_shiftProduction[iCurrentNum].m_BadResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShiftProduction[iCurrentNum].m_BadResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_BadResult[theApp.m_lastShiftIndex]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_BadResult[theApp.m_lastShiftIndex]++;
		dataItem.DataDefect = _T("NG");
	}
	else
	{
		theApp.m_shiftProduction[iCurrentNum].m_GoodResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShiftProduction[iCurrentNum].m_GoodResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_GoodResult[theApp.m_lastShiftIndex]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_GoodResult[theApp.m_lastShiftIndex]++;
		dataItem.DataDefect = _T("GOOD");

		if (pDataStatus.m_OkGrade == Panel_A_GRADE)
		{
			theApp.m_shiftProduction[iCurrentNum].m_GoodAGradeResult[theApp.m_lastShiftIndex]++;
			theApp.m_UiShiftProduction[iCurrentNum].m_GoodAGradeResult[theApp.m_lastShiftIndex]++;
			theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_GoodAGradeResult[theApp.m_lastShiftIndex]++;
			theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_GoodAGradeResult[theApp.m_lastShiftIndex]++;
			dataItem.DataOkGrade = _T("A");
		}
		else if (pDataStatus.m_OkGrade == Panel_B_GRADE)
		{
			theApp.m_shiftProduction[iCurrentNum].m_GoodBGradeResult[theApp.m_lastShiftIndex]++;
			theApp.m_UiShiftProduction[iCurrentNum].m_GoodBGradeResult[theApp.m_lastShiftIndex]++;
			theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_GoodBGradeResult[theApp.m_lastShiftIndex]++;
			theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_GoodBGradeResult[theApp.m_lastShiftIndex]++;
			dataItem.DataOkGrade = _T("B");
		}
		else if (pDataStatus.m_OkGrade == Panel_C_GRADE)
		{
			theApp.m_shiftProduction[iCurrentNum].m_GoodCGradeResult[theApp.m_lastShiftIndex]++;
			theApp.m_UiShiftProduction[iCurrentNum].m_GoodCGradeResult[theApp.m_lastShiftIndex]++;
			theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_GoodCGradeResult[theApp.m_lastShiftIndex]++;
			theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_GoodCGradeResult[theApp.m_lastShiftIndex]++;
			dataItem.DataOkGrade = _T("C");
		}
		else
		{
			theApp.m_shiftProduction[iCurrentNum].m_GoodAGradeResult[theApp.m_lastShiftIndex]++;
			theApp.m_UiShiftProduction[iCurrentNum].m_GoodAGradeResult[theApp.m_lastShiftIndex]++;
			theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_GoodAGradeResult[theApp.m_lastShiftIndex]++;
			theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_GoodAGradeResult[theApp.m_lastShiftIndex]++;
			dataItem.DataOkGrade = _T("A");
		}
	}

	CFile   File;
	CString FileName, strString, strTemp, strShift;
	strShift = theApp.m_lastShiftIndex == 0 ? _T("DY") : _T("NT");
	CString strEQPID = CStringSupport::FormatString(_T("%s%s"), theApp.m_strEqpId, theApp.m_strEqpNum);
	FileName.Format(_T("%s\\%s_%s_Cell_Log_%s.csv"), AOI_DATA_INSPECT_CSV_PATH, theApp.m_strCurrentToday, strEQPID, strShift);
	
	CString strName = theApp.m_strEqpId == _T("MFBAP") ? _T("OTP") : _T("LUMITOP");

	BOOL bOpen = FALSE;
	if (!File.Open(FileName, CFile::modeReadWrite | CFile::shareDenyNone))
	{
		if (File.Open(FileName, CFile::modeCreate | CFile::modeWrite))
		{
			bOpen = TRUE;

			strString = CStringSupport::FormatString(_T("TIME,EQP_ID,INDEX,PANEL,CELLID,FPCID,DEFECT,OK_GRADE,1ST CONTACT,2ND CONTACT,%s,AOI,VIEWING,TP,DATAIN,DATAOUT"), strName);
			strString += "\r\n";
			File.Write(strString.GetBuffer(), strString.GetLength() * 2);
			strString.ReleaseBuffer();
		}

	}
	else bOpen = TRUE;

	if (bOpen){
		CString strTime, strDefectString = _T("");
		CTime cTime;

		cTime = CTime::GetCurrentTime();
		File.SeekToEnd();

		strString.Format(_T("%s,%s,%s,%d,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s"),
			GetDateString6(),
			strEQPID,
			PG_IndexName[iCurrentNum],
			iPanelNum + 1,
			strCell_ID,
			strFpc_ID,
			dataItem.DataDefect,
			dataItem.DataOkGrade,
			dataItem.DataFirstContactStatus,
			dataItem.DataContactStatus,
			dataItem.DataOtpStatus,
			dataItem.DataVisionStatus,
			dataItem.DataViewingStatus,
			dataItem.DataTpStatus,
			dataItem.DataInStatus,
			dataItem.DataOutStatus);


		strString += "\r\n";

		int iLen = strString.GetLength();
		File.Write(strString.GetBuffer(), iLen * 2);
		strString.ReleaseBuffer();
		File.Close();
	}



	theApp.m_pDataStatusLog->LOG_INFO(CStringSupport::FormatString(_T("INDEX : %s PANEL : %d CELLID : %s FPCID : %s DEFECT: %s OK_GRADE: %d 1ST CONTACT : %d 2ND CONTACT: %d AOI: %d OTP: %d VIEWING: %d TP : %d DATAIN : %d DATAOUT : %d"),
		PG_IndexName[iCurrentNum],
		iPanelNum + 1,
		strCell_ID,
		strFpc_ID,
		dataItem.DataDefect,
		pDataStatus.m_OkGrade,
		pDataStatus.m_FirstContactStatus,
		pDataStatus.m_ContactStatus,
		pDataStatus.m_VisionStatus,
		pDataStatus.m_OtpStatus,
		pDataStatus.m_ViewingStatus,
		pDataStatus.m_TpStatus,
		pDataStatus.m_TryInsertStatus,
		iCommand));


	theApp.AOIInspectionDataSave(theApp.m_lastShiftIndex);
	theApp.AlignDataSave(theApp.m_lastShiftIndex);
	theApp.ContactDataSave(theApp.m_lastShiftIndex);
	theApp.TpDataSave(theApp.m_lastShiftIndex);
	theApp.PreGammaDataSave(theApp.m_lastShiftIndex);
	theApp.AOIInspectionTimeDataSave(theApp.m_lastShiftIndex);


	if (iCommand == Data_TrayOut)
		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_TrayReportEnd1 + iPanelNum, OffSet_0, TRUE);
	else
		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_DataReportEnd1 + iPanelNum, OffSet_0, TRUE);
	theApp.m_PlcThread->LogWrite(CStringSupport::FormatString(_T("AOI Lower DataReport Flag %d END"), iPanelNum));
	m_csData.Unlock();
}

void CPlcThread::ULDInspectDataParser(int iPanelNum, int iCommand)
{
	m_csData.Lock();
	UnloaderDataStatus pDataStatus;
	DataStatusItem dataItem;
	CString strCell_ID, strFpc_ID;
	int iData = iCommand * 2;
	int iChNum = iPanelNum;
	dataItem.Reset();
	long falg = FALSE;
	
	theApp.m_pEqIf->m_pMNetH->ULDGetDataStatus(eWordType_GoodReportValue1 + iData + iPanelNum, &pDataStatus);
	strCell_ID = CStringSupport::ToWString(pDataStatus.m_PanelData, sizeof(pDataStatus.m_PanelData));
	strFpc_ID = CStringSupport::ToWString(pDataStatus.m_FpcData, sizeof(pDataStatus.m_FpcData));

	theApp.m_pEqIf->m_pMNetH->GetPlcWordData(eWordType_GoodReportValue1 + iPanelNum, &falg);
	if (falg) {
		theApp.m_pDataStatusLog->LOG_INFO(CStringSupport::FormatString(_T("$$$$$$$$$$$$$$$$$$$ %s____%s Waring $$$$$$$$$$$$$$$$$$$"), strCell_ID, strFpc_ID));
	}
		
	
	if (iCommand == Data_GoodOut)
	{
		theApp.m_ULDshiftProduction[iChNum].m_GoodResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShiftProduction[iChNum].m_GoodResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShift_TimeProduction[theApp.m_iTimeInspectNum].m_GoodResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDshift_TimeProduction[theApp.m_iTimeInspectNum].m_GoodResult[theApp.m_lastShiftIndex]++;
		dataItem.DataOutStatus = _T("GOOD");
		dataItem.DataDefect = _T("GOOD");
	}
	else if (iCommand == Data_NgOut)
	{
		iChNum = pDataStatus.m_ManualStageNum - 1;

		theApp.m_ULDshiftProduction[iChNum].m_BadResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShiftProduction[iChNum].m_BadResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShift_TimeProduction[theApp.m_iTimeInspectNum].m_BadResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDshift_TimeProduction[theApp.m_iTimeInspectNum].m_BadResult[theApp.m_lastShiftIndex]++;
		dataItem.DataOutStatus = _T("NG");
		dataItem.DataDefect = _T("NG");

		if (pDataStatus.m_ManualStageNum == m_ManualStage1)
			dataItem.DataStageNum = _T("MStage1");
		else if (pDataStatus.m_ManualStageNum == m_ManualStage2)
			dataItem.DataStageNum = _T("MStage2");
	}
	else if (iCommand == Data_SampleOut)
	{
		theApp.m_ULDshiftProduction[iChNum].m_SampleResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShiftProduction[iChNum].m_SampleResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShift_TimeProduction[theApp.m_iTimeInspectNum].m_SampleResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDshift_TimeProduction[theApp.m_iTimeInspectNum].m_SampleResult[theApp.m_lastShiftIndex]++;
		dataItem.DataOutStatus = _T("Sample");
		dataItem.DataDefect = _T("BYPASS");
	}
	else if (iCommand == Data_BufferTrayOut)
	{
		theApp.m_ULDshiftProduction[iChNum].m_BufferTrayResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShiftProduction[iChNum].m_BufferTrayResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShift_TimeProduction[theApp.m_iTimeInspectNum].m_BufferTrayResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDshift_TimeProduction[theApp.m_iTimeInspectNum].m_BufferTrayResult[theApp.m_lastShiftIndex]++;
		dataItem.DataOutStatus = _T("Buffer Tray");
		dataItem.DataDefect = _T("BYPASS");
	}
	else if (iCommand == Data_BufferTrayIn)
	{
		theApp.m_ULDshiftProduction[iChNum].m_BufferTrayResult[theApp.m_lastShiftIndex]--;
		theApp.m_ULDUiShiftProduction[iChNum].m_BufferTrayResult[theApp.m_lastShiftIndex]--;
		theApp.m_ULDUiShift_TimeProduction[theApp.m_iTimeInspectNum].m_BufferTrayResult[theApp.m_lastShiftIndex]--;
		theApp.m_ULDshift_TimeProduction[theApp.m_iTimeInspectNum].m_BufferTrayResult[theApp.m_lastShiftIndex]--;
		dataItem.DataOutStatus = _T("Buffer Tray");
		dataItem.DataDefect = _T("BYPASS");
	}

	//버퍼 나가는것은 총카운터 집계 못하도록
	if (iCommand == Data_GoodOut || iCommand == Data_SampleOut)
	{
		theApp.m_ULDshiftProduction[iChNum].m_InspectionTotal[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShiftProduction[iChNum].m_InspectionTotal[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShift_TimeProduction[theApp.m_iTimeInspectNum].m_InspectionTotal[theApp.m_lastShiftIndex]++;
		theApp.m_ULDshift_TimeProduction[theApp.m_iTimeInspectNum].m_InspectionTotal[theApp.m_lastShiftIndex]++;
	}
	else if (iCommand == Data_NgOut)
	{
		theApp.m_ULDshiftProduction[iChNum].m_InspectionTotal[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShiftProduction[iChNum].m_InspectionTotal[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShift_TimeProduction[theApp.m_iTimeInspectNum].m_InspectionTotal[theApp.m_lastShiftIndex]++;
		theApp.m_ULDshift_TimeProduction[theApp.m_iTimeInspectNum].m_InspectionTotal[theApp.m_lastShiftIndex]++;
	}

	if (pDataStatus.m_ContactStatus == m_codeFail)
	{
		theApp.m_ULDshiftProduction[iChNum].m_ContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShiftProduction[iChNum].m_ContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShift_TimeProduction[theApp.m_iTimeInspectNum].m_ContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDshift_TimeProduction[theApp.m_iTimeInspectNum].m_ContactResult[theApp.m_lastShiftIndex]++;

		dataItem.DataContactStatus = _T("NG");
	}
	else if (pDataStatus.m_ContactStatus == m_codeOk)
	{
		dataItem.DataContactStatus = _T("GOOD");
	}

	if (pDataStatus.m_FirstContactStatus == m_codeFail)
	{
		theApp.m_ULDshiftProduction[iChNum].m_FirstContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShiftProduction[iChNum].m_FirstContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShift_TimeProduction[theApp.m_iTimeInspectNum].m_FirstContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDshift_TimeProduction[theApp.m_iTimeInspectNum].m_FirstContactResult[theApp.m_lastShiftIndex]++;

		dataItem.DataFirstContactStatus = _T("NG");
	}

	if (pDataStatus.m_ManualContactStatus == m_codeFail)
	{
		theApp.m_ULDshiftProduction[iChNum].m_ManualContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShiftProduction[iChNum].m_ManualContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShift_TimeProduction[theApp.m_iTimeInspectNum].m_ManualContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDshift_TimeProduction[theApp.m_iTimeInspectNum].m_ManualContactResult[theApp.m_lastShiftIndex]++;
		dataItem.DataManualContactStatus = _T("NG");
	}
	else if (pDataStatus.m_ManualContactStatus == m_codeOk)
	{
		dataItem.DataManualContactStatus = _T("GOOD");
	}

	if (pDataStatus.m_PreGammaStatus == m_codeFail)
	{
		theApp.m_ULDshiftProduction[iChNum].m_GammaResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShiftProduction[iChNum].m_GammaResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShift_TimeProduction[theApp.m_iTimeInspectNum].m_GammaResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDshift_TimeProduction[theApp.m_iTimeInspectNum].m_GammaResult[theApp.m_lastShiftIndex]++;
		dataItem.DataGammaStatus = _T("NG");
	}
	else if (pDataStatus.m_PreGammaStatus == m_codeOk)
	{
		dataItem.DataGammaStatus = _T("GOOD");
	}

	if (pDataStatus.m_TpStatus == m_codeFail)
	{
		theApp.m_ULDshiftProduction[iChNum].m_TouchResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShiftProduction[iChNum].m_TouchResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShift_TimeProduction[theApp.m_iTimeInspectNum].m_TouchResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDshift_TimeProduction[theApp.m_iTimeInspectNum].m_TouchResult[theApp.m_lastShiftIndex]++;
		dataItem.DataTpStatus = _T("NG");
	}
	else if (pDataStatus.m_TpStatus == m_codeOk)
	{
		dataItem.DataTpStatus = _T("GOOD");
	}

	if (pDataStatus.m_OpvStatus == m_codeFail)
	{
		theApp.m_ULDshiftProduction[iChNum].m_OpvResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShiftProduction[iChNum].m_OpvResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShift_TimeProduction[theApp.m_iTimeInspectNum].m_OpvResult[theApp.m_lastShiftIndex]++;
		theApp.m_ULDshift_TimeProduction[theApp.m_iTimeInspectNum].m_OpvResult[theApp.m_lastShiftIndex]++;
		dataItem.DataOpvStatus = _T("NG");
	}
	else if (pDataStatus.m_OpvStatus == m_codeOk)
	{
		dataItem.DataOpvStatus = _T("GOOD");
	}

	if (pDataStatus.m_TryInsertStatus == m_TrayInsert)
	{
		theApp.m_ULDshiftProduction[iChNum].m_TrayInsertstatus[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShiftProduction[iChNum].m_TrayInsertstatus[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShift_TimeProduction[theApp.m_iTimeInspectNum].m_TrayInsertstatus[theApp.m_lastShiftIndex]++;
		theApp.m_ULDshift_TimeProduction[theApp.m_iTimeInspectNum].m_TrayInsertstatus[theApp.m_lastShiftIndex]++;
		dataItem.DataInStatus = _T("Tray");
	}
	else
	{
		theApp.m_ULDshiftProduction[iChNum].m_UpperInsertstatus[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShiftProduction[iChNum].m_UpperInsertstatus[theApp.m_lastShiftIndex]++;
		theApp.m_ULDUiShift_TimeProduction[theApp.m_iTimeInspectNum].m_UpperInsertstatus[theApp.m_lastShiftIndex]++;
		theApp.m_ULDshift_TimeProduction[theApp.m_iTimeInspectNum].m_UpperInsertstatus[theApp.m_lastShiftIndex]++;
		dataItem.DataInStatus = _T("Upper");
	}

	CFile   File;
	CString FileName, strString, strTemp, strShift;
	strShift = theApp.m_lastShiftIndex == 0 ? _T("DY") : _T("NT");
	CString strEQPID = CStringSupport::FormatString(_T("%s%s"), theApp.m_strEqpId, theApp.m_strEqpNum);
	FileName.Format(_T("%s\\%s_%s_Cell_Log_%s.csv"), ULD_DATA_INSPECT_CSV_PATH, theApp.m_strCurrentToday, strEQPID, strShift);

	BOOL bOpen = FALSE;
	if (!File.Open(FileName, CFile::modeReadWrite | CFile::shareDenyNone))
	{
		if (File.Open(FileName, CFile::modeCreate | CFile::modeWrite))
		{
			bOpen = TRUE;

			strString.Format(_T("TIME,EQP_ID,CH,STAGE,CELLID,FPCID,DEFECT,1ST_CONTACT,2ND_CONTACT,MANUALCONTACT,PREGAMMA,TP,OPV,DATAIN,DATAOUT,MAIN_GRADE,MAIN_CODE,PATTERN"));
			strString += "\r\n";
			File.Write(strString.GetBuffer(), strString.GetLength() * 2);
			strString.ReleaseBuffer();
		}

	}
	else bOpen = TRUE;

	if (bOpen){
		CString strTime, strDefectString = _T("");
		CTime cTime;

		cTime = CTime::GetCurrentTime();
		File.SeekToEnd();
		CString strFilePath, name;
		strFilePath = CStringSupport::FormatString(_T("%s\\%s.ini"), DATA_MAIN_DEFECT_CODE_PATH, strCell_ID);
		if (FileExists(strFilePath))
		{
			EZIni ini(strFilePath);
			name = ini[_T("AOI")][_T("Main")];
			CStringArray responseTokens2;
			CStringSupport::GetTokenArray(name, _T('^'), responseTokens2);

			strString.Format(_T("%s,%s,%d,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s"),
				GetDateString6(),
				strEQPID,
				iChNum + 1,
				dataItem.DataStageNum,
				strCell_ID,
				strFpc_ID,
				dataItem.DataDefect,
				dataItem.DataFirstContactStatus,
				dataItem.DataContactStatus,
				dataItem.DataManualContactStatus,
				dataItem.DataGammaStatus,
				dataItem.DataTpStatus,
				dataItem.DataOpvStatus,
				dataItem.DataInStatus,
				dataItem.DataOutStatus,
				responseTokens2[0],
				responseTokens2[1],
				responseTokens2[2]);
		}

		else
		{
			strString.Format(_T("%s,%s,%d,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,,,"),
				GetDateString6(),
				strEQPID,
				iChNum + 1,
				dataItem.DataStageNum,
				strCell_ID,
				strFpc_ID,
				dataItem.DataDefect,
				dataItem.DataFirstContactStatus,
				dataItem.DataContactStatus,
				dataItem.DataManualContactStatus,
				dataItem.DataGammaStatus,
				dataItem.DataTpStatus,
				dataItem.DataOpvStatus,
				dataItem.DataInStatus,
				dataItem.DataOutStatus
				);
		}


		strString += "\r\n";

		int iLen = strString.GetLength();
		File.Write(strString.GetBuffer(), iLen * 2);
		strString.ReleaseBuffer();
		File.Close();
		DeleteFile(strFilePath);
	}

	theApp.m_pDataStatusLog->LOG_INFO(CStringSupport::FormatString(_T("PANEL : %d STAGE : %s CELLID : %s FPCID : %s DEFECT: %s CONTACT: %d Manual 1ST_CONTACT :%d 2ND_CONTACT: %d PreGamma: %d TP: %d OPV: %d DATAIN : %d DATAOUT : %d"),
		iChNum + 1,
		dataItem.DataStageNum,
		strCell_ID,
		strFpc_ID,
		dataItem.DataDefect,
		pDataStatus.m_FirstContactStatus,
		pDataStatus.m_ContactStatus,
		pDataStatus.m_ManualContactStatus,
		pDataStatus.m_PreGammaStatus,
		pDataStatus.m_TpStatus,
		pDataStatus.m_OpvStatus,
		pDataStatus.m_TryInsertStatus,
		iCommand));

	theApp.ULDInspectionDataSave(theApp.m_lastShiftIndex);
	theApp.ULDInspectionTimeDataSave(theApp.m_lastShiftIndex);

	theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_GoodReportEnd + iCommand, iPanelNum, TRUE);

	m_csData.Unlock();
}
#else
void CPlcThread::GAMMAInspectDataParser(int iPanelNum, int iCommand)
{
	m_csData.Lock();
	DataStatus pDataStatus;
	CString strCell_ID, strFpc_ID, strDefectCode;
	DataStatusItem dataItem;
	dataItem.Reset();

	int iStageNum = 0;
	BOOL bNgFlag = FALSE;

	if (iCommand == Data_TrayOut)
		theApp.m_pEqIf->m_pMNetH->GetDataStatus(eWordType_NgReportValue1 + iPanelNum, &pDataStatus);
	else
		theApp.m_pEqIf->m_pMNetH->GetDataStatus(eWordType_DataReportValue1 + iPanelNum, &pDataStatus);
	
	strCell_ID = CStringSupport::ToWString(pDataStatus.m_PanelData, sizeof(pDataStatus.m_PanelData));
	strFpc_ID = CStringSupport::ToWString(pDataStatus.m_FpcData, sizeof(pDataStatus.m_FpcData));
	
	iStageNum = pDataStatus.m_GammaStageNum - 1;

	theApp.m_shiftProduction[iStageNum].m_InspectionTotal[theApp.m_lastShiftIndex]++;
	theApp.m_UiShiftProduction[iStageNum].m_InspectionTotal[theApp.m_lastShiftIndex]++;

	theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_InspectionTotal[theApp.m_lastShiftIndex]++;
	theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_InspectionTotal[theApp.m_lastShiftIndex]++;

	if (pDataStatus.m_ContactStatus == m_codeFail)
	{
		theApp.m_shiftProduction[iStageNum].m_ContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShiftProduction[iStageNum].m_ContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_ContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_ContactResult[theApp.m_lastShiftIndex]++;

		theApp.m_shiftProduction[iStageNum].m_ContactNg[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShiftProduction[iStageNum].m_ContactNg[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_ContactNg[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_ContactNg[theApp.m_lastShiftIndex][iPanelNum]++;
		dataItem.DataContactStatus = _T("NG");
		bNgFlag = TRUE;
	}
	else if (pDataStatus.m_ContactStatus == m_codeOk)
	{
		theApp.m_shiftProduction[iStageNum].m_ContactGood[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShiftProduction[iStageNum].m_ContactGood[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_ContactGood[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_ContactGood[theApp.m_lastShiftIndex][iPanelNum]++;
		dataItem.DataContactStatus = _T("GOOD");
	}

	if (pDataStatus.m_FirstContactStatus == m_codeFail)
	{
		theApp.m_shiftProduction[iStageNum].m_FirstContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShiftProduction[iStageNum].m_FirstContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_FirstContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_FirstContactResult[theApp.m_lastShiftIndex]++;

		theApp.m_shiftProduction[iStageNum].m_FirstContactNG[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShiftProduction[iStageNum].m_FirstContactNG[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_FirstContactNG[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_FirstContactNG[theApp.m_lastShiftIndex][iPanelNum]++;
		dataItem.DataFirstContactStatus = _T("NG");
	}
	else if (pDataStatus.m_FirstContactStatus == m_codeOk)
		dataItem.DataFirstContactStatus = _T("GOOD");

	if (pDataStatus.m_ManualContactStatus == m_codeFail)
	{
		theApp.m_shiftProduction[iStageNum].m_ManualContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShiftProduction[iStageNum].m_ManualContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_ManualContactResult[theApp.m_lastShiftIndex]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_ManualContactResult[theApp.m_lastShiftIndex]++;
		dataItem.DataManualContactStatus = _T("NG");
	}
	else if (pDataStatus.m_ManualContactStatus == m_codeOk)
		dataItem.DataManualContactStatus = _T("GOOD");

	if (pDataStatus.m_MtpStatus == m_codeFail)
	{
		theApp.m_shiftProduction[iStageNum].m_MtpResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShiftProduction[iStageNum].m_MtpResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_MtpResult[theApp.m_lastShiftIndex]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_MtpResult[theApp.m_lastShiftIndex]++;

		theApp.m_shiftProduction[iStageNum].m_MtpNg[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShiftProduction[iStageNum].m_MtpNg[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_MtpNg[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_MtpNg[theApp.m_lastShiftIndex][iPanelNum]++;
		bNgFlag = TRUE;

		CString strCodeGrade = theApp.GammaDefectInfoLoad(strCell_ID, strFpc_ID);

		if (strCodeGrade.IsEmpty() == FALSE)
		{
			CStringArray responseTokens;
			CStringSupport::GetTokenArray(strCodeGrade, _T('^'), responseTokens);

			strDefectCode = responseTokens[0];

			if (!strDefectCode.CompareNoCase(_T("EIMECC")))
			{
				dataItem.DataCIEStatus = _T("NG");
				dataItem.DataGammaTuningStatus = _T("BYPASS");
				dataItem.DataOtpStatus = _T("BYPASS");
			}
			else if (!strDefectCode.CompareNoCase(_T("XIMXTF")) || !strDefectCode.CompareNoCase(_T("XIMXGF")))
			{
				dataItem.DataCIEStatus = _T("GOOD");
				dataItem.DataGammaTuningStatus = _T("NG");
				dataItem.DataOtpStatus = _T("NG");
			}
		}
	}
	else if (pDataStatus.m_MtpStatus == m_codeOk)
	{
		theApp.m_shiftProduction[iStageNum].m_MtpGood[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShiftProduction[iStageNum].m_MtpGood[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_MtpGood[theApp.m_lastShiftIndex][iPanelNum]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_MtpGood[theApp.m_lastShiftIndex][iPanelNum]++;
		dataItem.DataOtpStatus = _T("GOOD");
		dataItem.DataCIEStatus = _T("GOOD");
		dataItem.DataGammaTuningStatus = _T("GOOD");
	}

	if (iCommand == Data_TrayOut)
		dataItem.DataOutStatus = _T("NGTray");
	else
		dataItem.DataOutStatus = _T("DownFlow");
	
	if (bNgFlag == TRUE)
	{
		theApp.m_shiftProduction[iStageNum].m_BadResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShiftProduction[iStageNum].m_BadResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_BadResult[theApp.m_lastShiftIndex]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_BadResult[theApp.m_lastShiftIndex]++;
		dataItem.DataDefect = _T("NG");
	}
	else
	{
		theApp.m_shiftProduction[iStageNum].m_GoodResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShiftProduction[iStageNum].m_GoodResult[theApp.m_lastShiftIndex]++;
		theApp.m_UiShift_TimeProduction[theApp.m_iTimeInspectNum].m_GoodResult[theApp.m_lastShiftIndex]++;
		theApp.m_shift_TimeProduction[theApp.m_iTimeInspectNum].m_GoodResult[theApp.m_lastShiftIndex]++;
		dataItem.DataDefect = _T("GOOD");
	}

	dataItem.DataGammaTuningTime = CStringSupport::FormatString(_T("%d"), pDataStatus.m_GammaTunningTime);
	CString strGammaTuningTime = GammaTuningTimeParser(dataItem.DataGammaTuningTime);
	dataItem.DataInTrayNum = CStringSupport::FormatString(_T("%d"), pDataStatus.m_InTrayNum);

	// DataCIEStatus, DataGammaTuningStatus 만 PC에서 정합니다.
	CFile   File;
	CString FileName, strString, strTemp, strShift;
	strShift = theApp.m_lastShiftIndex == 0 ? _T("DY") : _T("NT");
	CString strEQPID = CStringSupport::FormatString(_T("%s%s"), theApp.m_strEqpId, theApp.m_strEqpNum);

	FileName.Format(_T("%s%s_%s_Cell_Log_%s.csv"), DATA_INSPECT_CSV_PATH, theApp.m_strCurrentToday, strEQPID, strShift);

	BOOL bOpen = FALSE;
	if (!File.Open(FileName, CFile::modeReadWrite | CFile::shareDenyNone))
	{
		if (File.Open(FileName, CFile::modeCreate | CFile::modeWrite))
		{
			bOpen = TRUE;

			strString.Format(_T("TIME,EQP_ID,STAGE,PANEL,CELLID,FPCID,1ST_CONTACT,2ND_CONTACT,MANUALCONTACT,CIE,GAMMA TUNING,GAMMA,GAMMA TUNING TT,DATAOUT,TRAY POS"));
			strString += "\r\n";
			File.Write(strString.GetBuffer(), strString.GetLength() * 2);
			strString.ReleaseBuffer();
		}
	}
	else bOpen = TRUE;

	if (bOpen){
		CString strTime, strDefectString = _T("");
		CTime cTime;

		cTime = CTime::GetCurrentTime();
		File.SeekToEnd();

		strString.Format(_T("%s,%s,%s,%d,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s"),
			GetDateString6(),
			strEQPID,
			PG_IndexName[iStageNum],
			iPanelNum + 1,
			strCell_ID,
			strFpc_ID,
			dataItem.DataFirstContactStatus,
			dataItem.DataContactStatus,
			dataItem.DataManualContactStatus,
			dataItem.DataCIEStatus,
			dataItem.DataGammaTuningStatus,
			dataItem.DataOtpStatus,
			strGammaTuningTime,
			dataItem.DataOutStatus,
			dataItem.DataInTrayNum);

		strString += "\r\n";

		int iLen = strString.GetLength();
		File.Write(strString.GetBuffer(), iLen * 2);
		strString.ReleaseBuffer();
		File.Close();
	}

	theApp.m_pDataStatusLog->LOG_INFO(CStringSupport::FormatString(_T("STAGE : %s PANEL : %d CELLID : %s FPCID : %s 1ST_CONTACT :%d 2ND_CONTACT: %d MANUALCONTACT: %d GAMMA: %d GAMMA TUNING TT : %s DATAOUT : %d TRAY POS : %d"),
		PG_IndexName[iStageNum],
		iPanelNum + 1,
		strCell_ID,
		strFpc_ID,
		pDataStatus.m_FirstContactStatus,
		pDataStatus.m_ContactStatus,
		pDataStatus.m_ManualContactStatus,
		pDataStatus.m_MtpStatus,
		strGammaTuningTime,
		iCommand,
		pDataStatus.m_InTrayNum));

	theApp.InspectionDataSave(theApp.m_lastShiftIndex);
	theApp.AlignDataSave(theApp.m_lastShiftIndex);
	theApp.ContactDataSave(theApp.m_lastShiftIndex);
	theApp.MtpDataSave(theApp.m_lastShiftIndex);
	theApp.InspectionTimeDataSave(theApp.m_lastShiftIndex);

	if (iCommand == Data_TrayOut)
		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_NgReportEnd1 + iPanelNum, OffSet_0, TRUE);
	else
		theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_DataReportEnd1 + iPanelNum, OffSet_0, TRUE);
	
	m_csData.Unlock();
}
#endif