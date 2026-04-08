
#include "stdafx.h"

#if _SYSTEM_AMTAFT_
#include "DlgMainView.h"
#include "RankThread.h"
#include "DFSInfo.h"
#include "DataInfo.h"

CRankThread::CRankThread()
{
	m_hQuit = CreateEvent(NULL, TRUE, FALSE, NULL);
}

CRankThread::~CRankThread()
{
}

void CRankThread::ThreadRun()
{
	map<CString, CString>::iterator iter2;
	map<CString, map<CString, CString>>::iterator iter;
	map<CString, int>::iterator DefectCodeiter;
	map<CString, map<CString, int>>::iterator Totalrankiter;
	map<CString, int>::iterator rankiter;
	map<CString, vector<pair< CString, ResultCodeRank>>>::iterator Totaliter;
	ResultCodeRank RankList;
	PLCSendDefect defectinfo;
	CStringArray responseTokens;

	CString strPath, strPanelID, strFpcID, strDefectCode, strInspName, strFilePath, strKey;
	CDFSInfo DfsInfo;
	int iPanelNum, iInspNum, iIndexNum, iValue;
	BOOL bFlag = FALSE;

	while (::WaitForSingleObject(m_hQuit, 200) != WAIT_OBJECT_0)
	{
		if (!m_RankCodeList.empty())
		{
			while (!m_RankCodeList.empty())
			{
				theApp.m_pTraceLog->LOG_INFO(_T("**************** START!!!! ****************"));
				strPath = strPanelID = strFpcID = strDefectCode = strInspName = strFilePath = _T("");
				RankList.Reset();
				defectinfo.Reset();
				DfsInfo.Clear();
				responseTokens.RemoveAll();

				m_csDfsData.Lock();
				strDefectCode = m_RankCodeList.front();
				m_csDfsData.Unlock();

				if (strDefectCode.IsEmpty())
				{
					theApp.m_pTraceLog->LOG_INFO(_T("**************** Defect Code What Error ****************"));
				}
				else
				{
					if (strDefectCode.Find(_T('^')) != -1)
					{
						CStringSupport::GetTokenArray(strDefectCode, _T('^'), responseTokens);
						if (responseTokens.GetSize() < 5)
						{
							theApp.m_pTraceLog->LOG_INFO(_T("**************** Defect Code Parser Size Error ****************"));
							theApp.m_pTestLog->LOG_INFO(_T("**************** Defect Code Parser Size Error ****************"));
						}
						else
						{
						strPanelID = responseTokens[0];
						strFpcID = responseTokens[1];
						iPanelNum = _ttoi(responseTokens[2]);
						iIndexNum = _ttoi(responseTokens[3]);
						iInspNum = _ttoi(responseTokens[4]);

						if (iInspNum == RankAOI)
						{
							// 【修改】使用 strFpcID（真实 barcode）作为目录/文件名，与 WriteAOICSVFile 写入路径保持一致
							CString strFinalPath;
							BOOL bFileFound = FALSE;

							for(int iTry = 0 ; iTry < 8 ; iTry++)
							{
								if (!bFileFound)
								{
									// 寻找文件（搜索7天内），只做一次
									for (int i = 0 ; i < 7 ; i++)
									{
										strPath = DFS_SHARE_PATH + GetDateString2ChangeDay((-1) * i) + _T("\\") + strFpcID + _T("\\AOI\\") + strFpcID + _T(".csv");
										if (FileExists(strPath))
										{
											strFinalPath = strPath;
											bFileFound = TRUE;
											break;
										}
									}

									if (!bFileFound)
									{
										// 文件还没出现，等100ms再试
										Delay(100, TRUE);
										theApp.m_pTestLog->LOG_DEBUG(_T("Rank File Path Error : %s,"), strPath);
										continue;
									}
								}

								// 文件已找到，读取 DEFECT_DATA
								strInspName = _T("AOI");
								DfsInfo.m_mapPanelDefect.clear();
								DfsInfo.m_mapDefectCodeList.clear();
								DfsInfo.DFSDefectBeginLoad(strFinalPath, strInspName, FALSE);

								// 检查是否读到有效缺陷数据
								BOOL bHasDefect = FALSE;
								for (auto& kv : DfsInfo.m_mapDefectCodeList)
								{
									if (!kv.first.IsEmpty())
									{
										bHasDefect = TRUE;
										break;
									}
								}

								if (bHasDefect)
								{
									// 读到有效数据，结束重试
									break;
								}
								else
								{
									// CSV 中 DEFECT_CODE 仍为空（ICW 缺陷坐标尚未写入 DB）
									// 等100ms后重新读取同一个 CSV 文件
									Delay(100, TRUE);
									theApp.m_pTestLog->LOG_DEBUG(_T("Rank DEFECT_DATA Empty, Re-read CSV : %s,"), strFinalPath);
								}
							}
						}
						else if (iInspNum == RankLumitop)
						{
							strPath = DFS_SHARE_PATH + GetDateString2() + _T("\\") + strPanelID + _T("\\Lumitop\\") + strPanelID + _T(".csv");
							strInspName = _T("Lumitop");
							DfsInfo.DFSDefectBeginLoad(strPath, strInspName, FALSE);
						}
						else if (iInspNum == RankViewing)
						{
							strPath = DFS_VIEWING_ANGLE_SHARE_PATH + GetDateString2() + _T("\\") + strPanelID + _T("\\Viewing\\") + strPanelID + _T(".csv");
							strInspName = _T("Viewing");
							DfsInfo.DFSDefectBeginLoad(strPath, strInspName, FALSE);
						}
						else if (iInspNum == RankTP
							   || iInspNum == RankContact)
						{
							CString strCodeTemp;
							map<CString, map<CString, CString>>::iterator iter;
							map<CString, CString> mapCode;

							iInspNum == RankTP ? strInspName = _T("TP"), strCodeTemp = _T("XIMXDE") 
								: strInspName = _T("CONTACT"), strCodeTemp = theApp.m_strContactNgCode;
							
							DfsInfo.m_mapPanelDefect.clear();
							DfsInfo.m_mapDefectCodeList.clear();
							/*iter = DfsInfo.m_mapPanelDefect.find(strInspName);
							if (iter != DfsInfo.m_mapPanelDefect.end())
								iter->second.insert(make_pair(strCodeTemp, _T("R1")));
							else
							{*/
								mapCode.insert(make_pair(strCodeTemp, _T("R1")));
								DfsInfo.m_mapPanelDefect.insert(make_pair(strInspName, mapCode));
								theApp.m_pTestLog->LOG_DEBUG(_T("DfsInfo.m_mapPanelDefect.insert  strPanelID :  strInspName : %s, mapCode : %s"), strPanelID, strInspName, mapCode);
							//}
						}

						theApp.m_pTestLog->LOG_INFO(_T("RankSave InspName : %s,"), strInspName);
					}
				}
				else
					theApp.m_pTraceLog->LOG_INFO(_T("**************** Parsing Error ****************"));
				}

				if (DfsInfo.m_mapPanelDefect.size() > 0)
				{
					/*CString strPath, strFilePath, strCodeCount, strShift, strCodeGrade;
					strShift = theApp.m_lastShiftIndex == 0 ? _T("DY") : _T("NT");
					strPath.Format(_T("%s\\%s\\%s_%s"), DATA_DEFECT_CODE_PATH, _T("AOI"), theApp.m_strCurrentToday, strShift);

					CreateFolders(strPath);
					strFilePath.Format(_T("%s\\%s.ini"), strPath, strFpcID);
					BOOL retVal = DeleteFile(strFilePath);*/

					for (auto Rank : theApp.m_VecRank[INDEX])
					{
						if (defectinfo.m_iCount == theApp.m_iNumberSendToPlc)
							break;

						iter = DfsInfo.m_mapPanelDefect.find(strInspName);
						if (iter != DfsInfo.m_mapPanelDefect.end())
						{
							iter2 = iter->second.find(Rank.strCode);
							if (iter2 != iter->second.end())
							{
								defectinfo.m_strCode = iter2->first;
								//defectinfo.m_strGrade = iter2->second;
								defectinfo.m_strGrade = Rank.strGrade;
								defectinfo.m_iCount++;

								theApp.SetSaveResultCode(strPanelID, strFpcID, strInspName, defectinfo, Machine_AOI);
								theApp.m_pTestLog->LOG_DEBUG(_T("RankSaveResultCode PanelID : %s, FPCID : %s, InspName : %s, strCode : %s, strGrade : %s,"),
									strPanelID, strFpcID, strInspName, defectinfo.m_strCode, defectinfo.m_strGrade);
							}
						}
						
						//if (defectinfo.m_iCount > 0)
						//{
						//	theApp.SetSaveResultCode(strPanelID, strFpcID, strInspName, defectinfo, Machine_AOI);
						//	theApp.m_pTestLog->LOG_DEBUG(_T("RankSaveResultCode PanelID : %s, FPCID : %s, InspName : %s, strCode : %s, strGrade : %s,"),
						//		strPanelID, strFpcID, strInspName, defectinfo.m_strCode, defectinfo.m_strGrade);
						//}
					}
				}
				else
				{
					theApp.m_pTestLog->LOG_INFO(_T("**************** PanelID [%s] FpcID [%s] SendPlcCode size error ****************"), strPanelID, strFpcID);						
					theApp.m_pTraceLog->LOG_INFO(_T("**************** PanelID [%s] FpcID [%s] SendPlcCode size error ****************"), strPanelID, strFpcID);
				}
					

				if (DfsInfo.m_mapDefectCodeList.size() > 0)
				{
					for (auto Rank : theApp.m_VecRank[INDEX])
					{
						DefectCodeiter = DfsInfo.m_mapDefectCodeList.find(Rank.strCode);
						if (DefectCodeiter != DfsInfo.m_mapDefectCodeList.end())
						{
							RankList.m_iCh = iPanelNum + 1;
							RankList.m_strZone = PG_IndexName[iIndexNum];
							RankList.m_strResultCode = DefectCodeiter->first;
							RankList.m_iResultCodeCount = DefectCodeiter->second;

							if (iInspNum == RankAOI)
							{
								theApp.m_iInspAoiMaxCount++;
								if (theApp.m_iInspAoiMaxCount > 999)
								{
									theApp.m_iInspAoiMaxCount = 0;
									bFlag = TRUE;
								}
							}
							else
							{
								theApp.m_iInspViewingMaxCount++;
								if (theApp.m_iInspViewingMaxCount > 999)
								{
									theApp.m_iInspViewingMaxCount = 0;
									bFlag = TRUE;
								}
							}

							theApp.SetSaveHistoryCode(strPanelID, strInspName, RankList, bFlag);

							strKey = _T("");
							iValue = 0;
							strKey.Format(_T("%s"), CStringSupport::FormatString(_T("%s^%d^%s"), RankList.m_strZone, RankList.m_iCh, RankList.m_strResultCode));

							Totalrankiter = theApp.m_mapRankCodeCount[theApp.m_lastShiftIndex].find(strInspName);
							if (Totalrankiter != theApp.m_mapRankCodeCount[theApp.m_lastShiftIndex].end())
							{
								rankiter = Totalrankiter->second.find(strKey);
								if (rankiter != Totalrankiter->second.end())
								{
									rankiter->second += RankList.m_iResultCodeCount;
									iValue = rankiter->second;
								}
								else
								{
									Totalrankiter->second.insert(make_pair(strKey, RankList.m_iResultCodeCount));
									iValue = RankList.m_iResultCodeCount;
								}

								theApp.SetSaveRankCode(strInspName, strKey, iValue, bFlag);
							}
							else
								theApp.m_pTraceLog->LOG_INFO(_T("**************** PanelID [%s] rank error****************"), strPanelID);

							Totaliter = theApp.m_mapRankTotalList[theApp.m_lastShiftIndex].find(strInspName);
							if (Totaliter != theApp.m_mapRankTotalList[theApp.m_lastShiftIndex].end())
								Totaliter->second.push_back(make_pair(strPanelID, RankList));
						}
					}
				}
				else
					theApp.m_pTraceLog->LOG_INFO(_T("**************** PanelID [%s] RankCode size error ****************"), strPanelID);

				theApp.m_pTraceLog->LOG_INFO(_T("**************** END!!!! ****************"));
				m_csDfsData.Lock();
				m_RankCodeList.pop();
				m_csDfsData.Unlock();
			}
		}
	}
}

void CRankThread::AddRankCodeList(CString strPanel, CString strFpcID, int iPanelNum, int iIndexNum, int iInspNum)
{
	if (strPanel.GetLength() > 0)
	{
		CString strInspName = InspectName[iInspNum];
		CString msg = CStringSupport::FormatString(_T("%s^%s^%d^%d^%d"), strPanel, strFpcID, iPanelNum, iIndexNum, iInspNum);
		theApp.m_pTraceLog->LOG_INFO(_T("******************[%s] %s Start ******************"), strInspName, msg);
		m_csDfsData.Lock();
		m_RankCodeList.push(msg);
		m_csDfsData.Unlock();
	}
}

UINT CRankThread::RankThreadProc(LPVOID pParam)
{
	CRankThread* pThis = reinterpret_cast<CRankThread*>(pParam);
	_ASSERTE(pThis != NULL);
	pThis->ThreadRun();
	return 1L;

}

BOOL CRankThread::CreateTask(){
	BOOL bRet = TRUE;
	m_pThreadRank = ::AfxBeginThread(RankThreadProc, this, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
	if (!m_pThreadRank)
		bRet = FALSE;
	m_pThreadRank->m_bAutoDelete = FALSE;	/// ¾²·¹µå Á¾·á½Ã WaitForSingleObject Àû¿ëÀ§ÇØ...
	m_pThreadRank->ResumeThread();
	return TRUE;
}

void CRankThread::CloseTask()
{
	if (m_pThreadRank != NULL)
	{
		SetEvent(m_hQuit);
		Delay(100, TRUE);
		if (::WaitForSingleObject(m_pThreadRank->m_hThread, 1000) == WAIT_TIMEOUT)
		{
			SetEvent(m_hQuit);
			Delay(100, TRUE);
			if (::WaitForSingleObject(m_pThreadRank->m_hThread, 1000) == WAIT_TIMEOUT) {
				::TerminateThread(m_pThreadRank->m_hThread, 1L);
				theApp.m_pTraceLog->LOG_INFO(_T("Terminate Rank Thread"));
			}
		}
		delete m_pThreadRank;
		m_pThreadRank = NULL;

	}
	if (m_hQuit)
	{
		CloseHandle(m_hQuit);
		m_hQuit = NULL;
	}
}
#endif


