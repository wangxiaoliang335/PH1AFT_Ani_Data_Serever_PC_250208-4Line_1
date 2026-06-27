// FTPClient.cpp: implementation of the CDFSClient class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "DFSClient.h"
#include "DfsInfo.h"
#include "DataInfo.h"
#include "Ani_Data_Serever_PC.h"
#include "DBInterface.h"
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

////////////////////////////////////////////////////////////////////////////////
// CDFSClient（DFS 报工 FTP 客户端，总体流程说明）
//
// 1) 角色
//    - 负责与 DFS Server（MES/报工服务器）做 FTP 连接与文件上传，属于「数据出口」模块：
//        • 连接 / 断线重连管理（`Connect` / `Disconnect`）
//        • 维护本地待上传队列（SUM/INDEX/图片路径等）
//        • 后台线程循环执行上传（通常在 `ThreadRun` 或类似函数中实现）
//
// 2) 与整机检测流程的关系
//    - PLC 线程在完成一片 Panel 的所有结果汇总后，会调用：
//        • `theApp.m_pFTP->DfsAddTransferFile(&DfsDataValue)`
//          将一条「整片 Panel 的检测汇总记录」加入 DFSClient 的上传队列
//    - CDFSClient 内部：
//        • 按顺序从队列中取出 DfsDataValue
//        • 生成符合 DFS 规范的 SUM/INDEX 文件内容（文件名含时间/机种/Stage 等）
//        • 通过 FTP 协议上传到 DFS Server 指定目录（`m_strAddress`/`m_strRemotePath`）
//        • 上传成功后，从队列中删除本条记录
//
// 3) 工程人员关注点
//    - 配置：
//        • IP / Port / 用户名 / 密码 等在配置或注册表中维护，本类在 `Connect` 中读取并建立 FTP 会话
//    - 故障现象：
//        • 若 DFS 端收不到 SUM/INDEX，优先检查：
//              - `m_bConnectState` 是否为 TRUE（是否连上 DFS）
//              - FTP 连接异常日志（CInternetException 信息）
//              - 本地是否有大量未删掉的 TEMP/SUM/INDEX 文件堆积
//    - 排查路径建议：
//        • 看 `CPlcThread::SumDFSDataStart` 是否有调用 `DfsAddTransferFile`
//        • 再看 `CDFSClient` 中对应的队列/上传线程逻辑（本文件中部），确认是否被正常消费。
////////////////////////////////////////////////////////////////////////////////

#if _SYSTEM_AMTAFT_
#define DEFAULT_MAIN_AOI_IMAGE_ROOT _T("D:\\MEMS_DFS_Data\\")
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CDFSClient::CDFSClient()
{
	//CFile File;
	//File.Open(DFS_LOCAL+_T("dummy.dmy"), CFile::modeCreate);
	//File.Close();


	m_bConnectState = FALSE;
	m_pFtpConnection = NULL;
	m_pDFSSession = NULL;

	m_strCurrentDirectory = "/";
	m_strRootDirectory = "/";

	//Default Param Setting
	m_strAddress = IP_ADDRESS_MCC;
	m_bUsePASVMode = FALSE;
	m_nRetries = 1;//3;	//0828Y13.JJH.ModifyFTP

	m_nPort = 21;

	m_strDescription = "";
	m_strLocalPath = "";
	m_nRetryDelay = 2;//10;	//0828Y13.JJH.ModifyFTP
	m_strUserName = "";
	m_strLogin = "";
	m_strName = "";
	m_strPassword = "";
	m_strRemotePath = "";
	m_hQuit = CreateEvent(NULL, TRUE, FALSE, NULL);
}

CDFSClient::~CDFSClient()
{
	Disconnect();
}

void CDFSClient::Connect(LPCTSTR lpszSiteName, LPCTSTR lpszUserName, LPCTSTR lpszPassword)
{
	//Default Setting Param
	// clear name!
	m_ftpSite.m_strName.Empty();

	m_strUserName = lpszUserName;
	m_strLogin = lpszUserName;
	m_strName = lpszSiteName;
	m_strPassword = lpszPassword;

	// if no site name is specified, show connect dialog
	m_ftpSite.m_bUsePASVMode = m_bUsePASVMode;
	m_ftpSite.m_nRetries = m_nRetries;
	m_ftpSite.m_nPort = m_nPort;
	m_ftpSite.m_nRetryDelay = m_nRetryDelay;
	m_ftpSite.m_strAddress = m_strAddress;
	m_ftpSite.m_strDescription = m_strDescription;
	m_ftpSite.m_strLocalPath = m_strLocalPath;
	m_ftpSite.m_strLogin = m_strLogin;
	m_ftpSite.m_strName = m_strName;
	m_ftpSite.m_strPassword = m_strPassword;
	m_ftpSite.m_strRemotePath = m_strRemotePath;

	// quick connect ?
	if (m_ftpSite.m_strName.IsEmpty())
	{
		m_ftpSite.m_bUsePASVMode = AfxGetApp()->GetProfileInt(_T("Settings"), _T("DefaultUsePASVMode"), 0);
		m_ftpSite.m_nRetries = AfxGetApp()->GetProfileInt(_T("Settings"), _T("DefaultRetries"), 3);
		m_ftpSite.m_nPort = 21;

		m_ftpSite.m_nRetryDelay = AfxGetApp()->GetProfileInt(_T("Settings"), _T("DefaultRetryDelay"), 2);
		m_ftpSite.m_strAddress = lpszSiteName;
		m_ftpSite.m_strDescription = _T("");
		m_ftpSite.m_strLocalPath = AfxGetApp()->GetProfileString(_T("Settings"), _T("DefaultLocalPath"), _T(""));
		m_ftpSite.m_strLogin = lpszUserName;
		m_ftpSite.m_strName = _T("");
		m_ftpSite.m_strPassword = lpszPassword;
		m_ftpSite.m_strRemotePath = _T("");
	}

	// fix paths the ways we like it...
	m_ftpSite.m_strLocalPath.TrimRight(_T("\\"));

	if (!m_ftpSite.m_strRemotePath.IsEmpty())
	{
		m_ftpSite.m_strRemotePath.Replace(_T("\\"), _T("/"));
		m_ftpSite.m_strRemotePath.TrimLeft('/');
		m_ftpSite.m_strAddress += _T("/");
		m_ftpSite.m_strAddress += m_ftpSite.m_strRemotePath;
	}

	CString strObject;
	INTERNET_PORT nPort;
	DWORD dwServiceType;

	// if there's already a connection open, close it first
	if (m_pFtpConnection != NULL)
	{
		m_pFtpConnection->Close();

		delete m_pFtpConnection;
		m_pFtpConnection = NULL;
	}

	if (m_pDFSSession != NULL)
	{
		m_pDFSSession->Close();
		delete m_pDFSSession;

		m_pDFSSession = NULL;
	}

	CString str;
	if (!str.LoadString(AFX_IDS_APP_TITLE))
		str = "AppUnknown";

	m_pDFSSession = new CDFSSession(str, 1, PRE_CONFIG_INTERNET_ACCESS);

	// Alert the user if the internet session could
	// not be started and close app
	if (!m_pDFSSession)
	{
		return;
	}

	m_pDFSSession->m_pMainWnd = NULL; //¾²Áö ¾ÊÀ¸¸®...
	m_pDFSSession->EnableStatusCallback();
	// check to see if this is a reasonable URL --
	// ie "ftp://servername/dirs" or just "servername/dirs"


	if (!AfxParseURL(m_ftpSite.m_strAddress, dwServiceType, m_strServerName, strObject, nPort))
	{
		// try adding the "ftp://" protocol
		CString strFtpURL = _T("ftp://");
		strFtpURL += m_ftpSite.m_strAddress;

		if (!AfxParseURL(strFtpURL, dwServiceType, m_strServerName, strObject, nPort))
		{
			AfxMessageBox(_T("URL IS NOT GOOD!"), MB_OK);
			return;
		}
	}

	//CWaitCursor cursor; // this will automatically display a wait cursor

	// Now open a FTP connection to the server
	if ((dwServiceType == INTERNET_SERVICE_FTP) && !m_strServerName.IsEmpty())
	{
		int nRetries = m_ftpSite.m_nRetries;
		while (nRetries > 0)
		{
			Delay(5, TRUE); //170205 KM
			nRetries--;
			try
			{
				int nTimeout = AfxGetApp()->GetProfileInt(_T("Settings"), _T("ConnectionTimeout"), 3);
				m_pDFSSession->SetOption(INTERNET_OPTION_CONNECT_TIMEOUT, nTimeout * 1000);
				m_pDFSSession->SetOption(INTERNET_OPTION_RECEIVE_TIMEOUT, nTimeout * 1000);
				m_pDFSSession->SetOption(INTERNET_OPTION_SEND_TIMEOUT, nTimeout * 1000);

				m_pFtpConnection = m_pDFSSession->GetFtpConnection(m_strServerName, m_ftpSite.m_strLogin, m_ftpSite.m_strPassword, m_ftpSite.m_nPort, m_ftpSite.m_bUsePASVMode);
				nRetries = 0;
			}
			catch (CInternetException* pEx)
			{
				DWORD dwLength = 255;//, dwError;

				CString strLastResponce;
				//				InternetGetLastResponseInfo(&dwError, strLastResponce.GetBuffer(dwLength), &dwLength);
				strLastResponce.ReleaseBuffer();
				strLastResponce.Remove('\n');
				strLastResponce.Remove('\r');

				// catch errors from WinINet
				dwLength = 255;
				CString strInfo;
				if (pEx->GetErrorMessage(strInfo.GetBuffer(dwLength), dwLength))
				{
					strInfo.ReleaseBuffer();
					strInfo.Remove('\n');
					strInfo.Remove('\r');

					// show wait dialog
					if (nRetries > 0)
					{
						Delay(m_ftpSite.m_nRetryDelay, TRUE); //<<150818 JYLee
					}
					else
					{
						if (!strLastResponce.IsEmpty())
						{
							strLastResponce += _T("\r\n");
						}

						strLastResponce += strInfo;
						//AfxMessageBox(strLastResponce, MB_OK);
					}
				}
				else
				{
					nRetries = 0;
					//AfxMessageBox(_T("Connect Fail!"), MB_OK);
				}
				pEx->Delete();
				pEx = NULL;

				delete m_pFtpConnection;
				m_pFtpConnection = NULL;
				delete m_pDFSSession;
				m_pDFSSession = NULL;
			}
		}
	}
	else
	{
		AfxMessageBox(_T("INVALID_URL"), MB_OK);
	}


	// PopulateTree() will display an error if the FTP connection
	// could not be made, otherwise, it grabs the root listing
	// and expands any folder indicated by the site name

	if (m_pFtpConnection != NULL)
	{
		// try to set remote directory
		if (m_pFtpConnection->SetCurrentDirectory(strObject))
		{
			m_pFtpConnection->GetCurrentDirectory(m_strRootDirectory);
		}

		//if (!m_ftpSite.m_strName.IsEmpty())
		//	AfxGetApp()->AddToRecentFileList(m_ftpSite.m_strName);

		m_bConnectState = TRUE;
	}
	else
	{
		m_bConnectState = FALSE;
		// 		GetTraceView()->AddTraceLine(3, _T("[%d] FTP connection could not be made."), AfxGetThread()->m_nThreadID);
		// 		PopulateTree();
	}

	if (m_pFtpConnection){
		m_pFtpConnection->Close();
		delete m_pFtpConnection;
		m_pFtpConnection = NULL;
	}
}

void CDFSClient::UploadFile(CString &source, CString &dest, BOOL &bSend)
{
	
	bSend = FALSE;
	if (!m_pDFSSession) return;
	BOOL bCreateDirectory = FALSE;

	// Create new ftp connection to retrieve file
	int nRetries = m_ftpSite.m_nRetries;
	while (nRetries > 0)
	{
		Delay(5, TRUE); //170205 KM
		nRetries--;
		try
		{
			int nTimeout = AfxGetApp()->GetProfileInt(_T("Settings"), _T("ConnectionTimeout"), 3);
			m_pDFSSession->SetOption(INTERNET_OPTION_CONNECT_TIMEOUT, nTimeout * 1000);
			m_pDFSSession->SetOption(INTERNET_OPTION_RECEIVE_TIMEOUT, nTimeout * 1000);
			m_pDFSSession->SetOption(INTERNET_OPTION_SEND_TIMEOUT, nTimeout * 1000);

			m_pFtpConnection = m_pDFSSession->GetFtpConnection(m_strServerName, m_ftpSite.m_strLogin, m_ftpSite.m_strPassword, m_ftpSite.m_nPort, m_ftpSite.m_bUsePASVMode);			
			nRetries = 0;
		}
		catch (CInternetException* pEx)
		{
			DWORD dwLength = 255;//, dwError;

			CString strLastResponce;
			//				InternetGetLastResponseInfo(&dwError, strLastResponce.GetBuffer(dwLength), &dwLength);
			strLastResponce.ReleaseBuffer();
			strLastResponce.Remove('\n');
			strLastResponce.Remove('\r');

			// catch errors from WinINet
			dwLength = 255;
			CString strInfo;
			if (pEx->GetErrorMessage(strInfo.GetBuffer(dwLength), dwLength))
			{
				strInfo.ReleaseBuffer();
				strInfo.Remove('\n');
				strInfo.Remove('\r');

				// show wait dialog
				if (nRetries > 0)
				{
					Delay(m_ftpSite.m_nRetryDelay, TRUE); //<<150818 JYLee
				}
				else
				{
					if (!strLastResponce.IsEmpty())
					{
						strLastResponce += _T("\r\n");
					}

					strLastResponce += strInfo;
					//AfxMessageBox(strLastResponce, MB_OK);
				}
			}
			else
			{
				nRetries = 0;
				//AfxMessageBox(_T("Connect Fail!"), MB_OK);
			}
			pEx->Delete();
			pEx = NULL;

			delete m_pFtpConnection;
			m_pFtpConnection = NULL;
			delete m_pDFSSession;
			m_pDFSSession = NULL;
			return;
		}
	}

	// 	if (!m_pFtpConnection){
	// 		delete m_pFtpConnection;
	// 		m_pFtpConnection = NULL;
	// 		delete m_pDFSSession;
	// 		m_pDFSSession= NULL;
	// 		return;
	// 	}
	//>> 140607 JSLee
	CString strDir;
	m_pFtpConnection->GetCurrentDirectory(strDir);
	m_strCurrentDirectory = strDir + _T("/") + m_strCurrentDirectory;
	// set current directory
	if (!m_pFtpConnection->SetCurrentDirectory(m_strCurrentDirectory)) //¿©±â¼­ °æ·Î ¼³Á¤ÇÏ°í ¾ÈµÇ¸é Æú´õ »ý¼º ±×¸®°í¾ÈµÇ¸é ³ª°¡! 
	{
		if (m_pFtpConnection->CreateDirectory(m_strCurrentDirectory))
		{
			if (!m_pFtpConnection->SetCurrentDirectory(m_strCurrentDirectory)) {
				delete m_pFtpConnection;
				m_pFtpConnection = NULL;
				delete m_pDFSSession;
				m_pDFSSession = NULL;
				return;
			}
		}
	}
	m_pFtpConnection->SetCurrentDirectory(_T("/"));
	
	theApp.m_pFTPLog->Info2(_T("==================FTP connection Upload================="));
	theApp.m_pFTPLog->Info(_T("[DFS] FTP Upload Start: source=%s, dest=%s"), source, dest);
	if (!m_pFtpConnection->PutFile(source, dest)){
		theApp.m_pFTPLog->Error(_T("[DFS] FTP PutFile FAILED: source=%s, dest=%s"), source, dest);
		delete m_pFtpConnection;
		m_pFtpConnection = NULL;
		delete m_pDFSSession;
		m_pDFSSession = NULL;
		return;
	}
	theApp.m_pFTPLog->Info(_T("[DFS] FTP PutFile SUCCESS: source=%s, dest=%s"), source, dest);
	m_pFtpConnection->Close();
	delete m_pFtpConnection;
	m_pFtpConnection = NULL;
	bSend = TRUE;
	theApp.m_pFTPLog->Info2(_T("====================FTP connection Close========================="));
	// close FTP connection
}

/********************************************************************/
/*																	*/
/* Function name : WaitWithMessageLoop								*/
/* Description   : Pump messages while waiting for event			*/
/*																	*/
/********************************************************************/
BOOL CDFSClient::WaitWithMessageLoop(HANDLE hEvent, int nTimeout)
{
	DWORD dwRet;

	DWORD dwMaxTick = GetTickCount() + nTimeout;

	while (1)
	{
		// wait for event or message, if it's a message, process it and return to waiting state
		dwRet = MsgWaitForMultipleObjects(1, &hEvent, FALSE, dwMaxTick - GetTickCount(), QS_ALLINPUT);
		if (dwRet == WAIT_OBJECT_0)
		{
			TRACE0("WaitWithMessageLoop() event triggered.\n");
			return TRUE;
		}
		else
			if (dwRet == WAIT_OBJECT_0 + 1)
			{
				// process window messages
				DoEvents();
			}
			else
			{
				// timed out !
				return FALSE;
			}
		Delay(2, TRUE);  //<< 150613 JYLee
	}
}

BOOL CDFSClient::CheckDirectory(CString strPath)
{
	if (!m_pDFSSession) return FALSE;

	// Create new ftp connection to retrieve file
	int nRetries = m_ftpSite.m_nRetries;
	while (nRetries > 0)
	{
		nRetries--;
		try
		{
			int nTimeout = AfxGetApp()->GetProfileInt(_T("Settings"), _T("ConnectionTimeout"), 3);
			m_pDFSSession->SetOption(INTERNET_OPTION_CONNECT_TIMEOUT, nTimeout * 1000);
			m_pDFSSession->SetOption(INTERNET_OPTION_RECEIVE_TIMEOUT, nTimeout * 1000);
			m_pDFSSession->SetOption(INTERNET_OPTION_SEND_TIMEOUT, nTimeout * 1000);

			m_pFtpConnection = m_pDFSSession->GetFtpConnection(m_strServerName, m_ftpSite.m_strLogin, m_ftpSite.m_strPassword, m_ftpSite.m_nPort, m_ftpSite.m_bUsePASVMode);
			nRetries = 0;
		}
		catch (CInternetException* pEx)
		{
			DWORD dwLength = 255;//, dwError;

			CString strLastResponce;
			//				InternetGetLastResponseInfo(&dwError, strLastResponce.GetBuffer(dwLength), &dwLength);
			strLastResponce.ReleaseBuffer();
			strLastResponce.Remove('\n');
			strLastResponce.Remove('\r');

			// catch errors from WinINet
			dwLength = 255;
			CString strInfo;
			if (pEx->GetErrorMessage(strInfo.GetBuffer(dwLength), dwLength))
			{
				strInfo.ReleaseBuffer();
				strInfo.Remove('\n');
				strInfo.Remove('\r');

				// show wait dialog
				if (nRetries > 0)
				{
					Delay(m_ftpSite.m_nRetryDelay, TRUE); //<<150818 JYLee
				}
				else
				{
					if (!strLastResponce.IsEmpty())
					{
						strLastResponce += _T("\r\n");
					}

					strLastResponce += strInfo;
					//AfxMessageBox(strLastResponce, MB_OK);
				}
			}
			else
			{
				nRetries = 0;
				//AfxMessageBox(_T("Connect Fail!"), MB_OK);
			}
			pEx->Delete();
			pEx = NULL;

			delete m_pFtpConnection;
			m_pFtpConnection = NULL;
			delete m_pDFSSession;
			m_pDFSSession = NULL;
			return FALSE;
		}
	}
	// 	if (!m_pFtpConnection){
	// 		delete m_pFtpConnection;
	// 		m_pFtpConnection = NULL;
	// 		delete m_pDFSSession;
	// 		m_pDFSSession= NULL;
	// 		return FALSE;
	// 	}
	CString strFilePath, strDir;
	int temp(0);
	m_pFtpConnection->GetCurrentDirectory(strFilePath);
	//strFilePath = strFilePath + _T("/") + strPath;
	strFilePath = strPath;
	BOOL bRet = TRUE;
	
	if (m_pFtpConnection->SetCurrentDirectory(strPath))
		bRet = TRUE;
	else
	{
		m_pFtpConnection->SetCurrentDirectory(_T("/"));
		while (temp != -1)
		{
			temp = strPath.Find(_T("/"));
			if (temp == -1)
			{
				strDir = strPath;
			}
			else
			{
				strDir = strPath.Left(temp);
				strPath = strPath.Mid(temp + 1);
			}
			m_pFtpConnection->GetCurrentDirectory(strFilePath);
			strFilePath = strFilePath + "/" + strDir;

			if (!m_pFtpConnection->SetCurrentDirectory(strFilePath))
			{
				if (m_pFtpConnection->CreateDirectory(strFilePath))
					m_pFtpConnection->SetCurrentDirectory(strFilePath);
				else
					bRet = FALSE;
			}
		}
	}
	if (m_pFtpConnection){
		m_pFtpConnection->Close();
		delete m_pFtpConnection;
		m_pFtpConnection = NULL;
	}
	return bRet;
}

//<<
/*--- END OF FTPClient.cpp ---*/
void CDFSClient::Disconnect()
{
	if (m_pDFSSession){
		m_pDFSSession->Close();
		delete m_pDFSSession;
		m_pDFSSession= NULL;
	}
	if (m_pFtpConnection){
		m_pFtpConnection->Close();
		delete m_pFtpConnection;
		m_pFtpConnection = NULL;
	}
}

// 中文说明：
//   **功能：** 将一片 Panel 的 DFS 上传任务加入队列，由 `RunDfsUploadThread` 线程异步处理。  
//   **典型调用：** 检测流程结束后，产线逻辑根据 PanelID/FPCID 等信息构造 `DfsDataValue`，调用本函数排队上传。  
//   **注意：** 这里只负责入队，不做任何文件生成与复制，具体 DFS 文件生成、搬运和 Index 文件创建在上传线程中完成。
void CDFSClient::DfsAddTransferFile(DfsDataValue strTransferFile)
{
	if (strTransferFile.m_PanelID.IsEmpty())
		theApp.m_pFTPLog->Info(_T("Sum Dfs PanelID Error"));

	m_csDfsUploadLock.Lock();
	m_DfsUploadtransferFileList.push(strTransferFile);
	m_csDfsUploadLock.Unlock();
}

void CDFSClient::DeleteFolderAndFile(LPCTSTR szFolderPath, BOOL flag)
{
	// TODO: ¿©±â¿¡ ¸Þ½ÃÁö Ã³¸®±â ÄÚµå¸¦ Ãß°¡ÇÕ´Ï´Ù.
	tstring strFileFound = szFolderPath;
	strFileFound += _T("\\*.*");

	WIN32_FIND_DATA info;

	HANDLE hp = FindFirstFile(strFileFound.c_str(), &info);   //µð·ºÅä¸®¿¡ ÆÄÀÏÀÌ ÀÖ´ÂÁö Ã¹¹øÂ° ÆÄÀÏ¸¸.
	do
	{
		while (1)
		{
			if (flag == TRUE)
				break;
		}
		Delay(150);
		if (!((_tcscmp(info.cFileName, _T(".")) == 0) || (_tcscmp(info.cFileName, _T("..")) == 0)))
		{
			if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY)  //Subµð·ºÅä¸®°¡ Á¸ÀçÇÏ´Â°æ¿ì 
			{
				tstring strSubFolder = szFolderPath;
				strSubFolder += _T("\\");
				strSubFolder += info.cFileName;

				DeleteFolderAndFile(strSubFolder.c_str(), flag); /// {return (_Ptr == 0 ? _Nullstr() : _Ptr); } 
				//RemoveDirectory(strSubFolder);		// ¿©±â¼­ È£ÃâÇÏ¸é ÇÏÀ§ Æú´õ¸¸ »èÁ¦
			}
			else
			{
				strFileFound = szFolderPath;
				strFileFound += _T("\\");
				strFileFound += info.cFileName;
				BOOL retVal = DeleteFile(strFileFound.c_str());
			}

		}

	} while (FindNextFile(hp, &info));

	FindClose(hp);

	RemoveDirectory(szFolderPath); //¿©±â¼­ È£ÃâÇÏ¸é ÃÖ»óÀ§ Æú´õ±îÁö »èÁ¦
}

void CDFSClient::RunDfsDeleatThread()
{
	CString strDFSPath, strMEMSPath, strMEMSMidPath;
	int i = 1;
	strMEMSMidPath = _T("MainAOI\\192.168.100.101-6501\\");
	while (::WaitForSingleObject(m_hDfsDeleatQuit, 1000) != WAIT_OBJECT_0)
	{
		COleDateTime now;
		CString strDate;
		now = COleDateTime::GetCurrentTime();

		// 正确获取前一天
		COleDateTime prevDay = now - COleDateTimeSpan(i, 0, 0, 0);

		strDate.Format(_T("%d%02d%02d"), prevDay.GetYear(), prevDay.GetMonth(), prevDay.GetDay());
		
		strDFSPath = DFS_SHARE_PATH + strDate;

		theApp.m_pFTPLog->Info(_T("RunDfsDeleatThread strDFSPath : %s, m_DeleteStart:%d"), strDFSPath, m_DeleteStart);

		DeleteFolderAndFile(strDFSPath, m_DeleteStart);

		COleDateTime prevDay1 = now - COleDateTimeSpan(i + 1, 0, 0, 0);
		strDate.Format(_T("%d-%02d-%02d"), prevDay1.GetYear(), prevDay1.GetMonth(), prevDay1.GetDay());
		for (int index = 1; index <= 4; index++)
		{
			strMEMSPath.Format(_T("%s%s%d\\"), theApp.m_strMainAOIImageRoot, strMEMSMidPath, index);
			strMEMSPath += strDate;
			theApp.m_pFTPLog->Info(_T("RunDfsDeleatThread strMEMSPath : %s, m_DeleteStart:%d"), strMEMSPath, m_DeleteStart);
			DeleteFolderAndFile(strMEMSPath, m_DeleteStart);
		}

		i++;
		if (now.GetHour() == 23)
			i = 1;
	}
}
void CDFSClient::RunDfsUploadThread()
{
	// 中文说明：
	//   **功能：** DFS 上传主线程入口函数，循环从 `m_DfsUploadtransferFileList` 中取出待上传 Panel，
	//             完成 SUM CSV/图片整理、目标 DFS 路径构建、DFS/INDEX/LINK 文件生成与搬运。
	//   **处理流程（_SYSTEM_AMTAFT_ 模式）：**
	//     1. 从队列取出 `DfsDataValue`（PanelID/FPCID/时间/结果等），清空 `CDFSInfo` / `CDataInfo` 等缓存。
	//     2. 依据 PanelID 和日期构造 AOI/Viewing/Lumitop/OPV 等源路径，创建 SUM 目录及 Image 目录。
	//     3. 调用 `CDFSInfo`/`CDataInfo` 读取检测结果、拷贝图像、生成 SUM CSV。
	//     4. 将 SUM CSV 与图像移动/复制至共享服务器路径（`/MODULE/.../Data`,`/MODULE/.../Image`）。
	//     5. 生成 LINK 文件和 INDEX 文件，并写入 DFS Index 列表。
	//     6. 处理完成后将当前 Panel 从队列中弹出，继续下一片 Panel。
	CDFSInfo DfsInfo;
	CDataInfo OpvInfo, DataInfo;
	DfsDataValue dfsData;

	CString strFpcID, strPanelID, strFilePath, strCsvFilePath, strImageFilePath, strIndexFile, strIndexTempFilePath, strLinkFilePath, strAlramFilePath;
	CString strAOIPath, strViewingPath, strLumitopPath, strSumPath, strSumImagePath, strTemp1, strAoiImagePath, strViewingImagePath, strIndexFilePath, strOpvFilPath, strOriAlramPath;

	while (::WaitForSingleObject(m_hDfsUploadQuit, 1000) != WAIT_OBJECT_0)
	{
		m_DeleteStart = TRUE;
		if (!m_DfsUploadtransferFileList.empty())
		{
			while (!m_DfsUploadtransferFileList.empty())
			{
				m_DeleteStart = FALSE;
				//m_csDfsUploadLock.Lock();

				dfsData.Reset();
				DfsInfo.Clear();
				DataInfo.Clear();
				OpvInfo.Clear();
				m_vecIndexValue.clear();
				DfsInfo.m_OpvDataList[Machine_ULD].clear();
			
				dfsData = m_DfsUploadtransferFileList.front();
				strPanelID = dfsData.m_PanelID;
				strFpcID = dfsData.m_FpcID;
				theApp.m_pFTPLog->Info(_T("Sum DFS START PanelID : %s, FPCID : %s,"), strPanelID, strFpcID);
				/*theApp.m_pFTPLog->Info(_T("[DFS] >>> Start Process PanelID=%s, Stage=%d, ChNum=%s, AOI=%s, TP=%s, Lumitop=%s"), 
					strPanelID, dfsData.m_StageNum, dfsData.m_ChNum, 
					dfsData.m_AOIInpsect, dfsData.m_TpResult2, dfsData.m_Lumitop);*/
			//	m_csDfsUploadLock.Unlock();
#if _SYSTEM_AMTAFT_
				if (strPanelID.IsEmpty() == FALSE)
				{
					strAOIPath = DFS_SHARE_PATH + GetDateString2() + _T("\\") + strPanelID + _T("\\AOI\\") + strPanelID + _T(".csv");
					strViewingPath = DFS_VIEWING_ANGLE_SHARE_PATH + GetDateString2() + _T("\\") + strPanelID + _T("\\VIEWING\\") + strPanelID + _T(".csv");
					strLumitopPath = DFS_SHARE_PATH + GetDateString2() + _T("\\") + strPanelID + _T("\\LUMITOP\\") + strPanelID + _T(".csv");
					strOpvFilPath = DFS_SHARE_OPVDFS_PATH + GetDateString2() + _T("\\") + strPanelID + _T("\\") + strPanelID + _T(".csv");

					strTemp1 = DFS_SHARE_PATH + GetDateString2() + _T("\\") + strPanelID + _T("\\SUM\\");
					CreateFolders(strTemp1);

					strSumImagePath = strTemp1 + _T("Image");
					CreateFolders(strSumImagePath);

					strAoiImagePath = DFS_SHARE_PATH + GetDateString2() + _T("\\") + strPanelID + _T("\\AOI\\Image");
					strViewingImagePath = DFS_VIEWING_ANGLE_SHARE_PATH + GetDateString2() + _T("\\") + strPanelID + _T("\\VIEWING\\Image");

					// ===== 按 ImagePath 复制缺陷图像到 AOI 目录 =====
					// 从数据库查询 ImagePath，先复制到 AOI\Image 目录，后续由 CopyImage 汇总到 SUM\Image
					CreateFolders(strAoiImagePath);
					theApp.m_pFTPLog->Info(_T("[DFS] AOI Image Path=%s"), strAoiImagePath);
					
					if (GetDBInterface().IsConnected())
					{
						theApp.m_pFTPLog->Debug(_T("[DFS] DB Connected, QueryByBarcode PanelID=%s"), strPanelID);
						// 按 ScreenID 查询最新的检测结果
						CInspectionResultList results;
						if (GetDBInterface().QueryByBarcode(strPanelID, results) && !results.empty())
						{
							theApp.m_pFTPLog->Info(_T("[DFS] QueryByBarcode SUCCESS, result count=%d, GUID=%s"), 
								results.size(), results.front().GUID);
							// 取最新的检测结果
							CInspectionResult& inspResult = results.front();
							CDefectInfoList defectList;
							if (GetDBInterface().QueryDefectsByParentGUID(inspResult.GUID, defectList))
							{
								theApp.m_pFTPLog->Info(_T("[DFS] QueryDefectsByParentGUID SUCCESS, defect count=%d"), defectList.size());
								int nDefectIndex = 0;
								for (const auto& defect : defectList)
								{
									nDefectIndex++;
								if (!defect.ImagePath.IsEmpty())
								{
									// ImagePath 可能是相对路径或绝对路径
									// 如果是相对路径，需要拼接根目录；如果是绝对路径直接使用
									CString strSrcImagePath;
									// 修复：检查是否包含盘符（如 D:\ 或 E:\）
									// 绝对路径特征：第2个字符是 ':'，第3个字符是 '\'
									BOOL bIsAbsolutePath = FALSE;
									if (defect.ImagePath.GetLength() >= 3)
									{
										TCHAR ch1 = defect.ImagePath.GetAt(1);  // 第2个字符
										TCHAR ch2 = defect.ImagePath.GetAt(2);  // 第3个字符
										if (ch1 == ':' && ch2 == '\\')
										{
											bIsAbsolutePath = TRUE;
										}
									}
									// 如果是绝对路径，直接使用
									if (bIsAbsolutePath)
									{
										strSrcImagePath = defect.ImagePath;
									}
									else
									{
										// 相对路径，拼接 MainAOI 根目录
										strSrcImagePath = DEFAULT_MAIN_AOI_IMAGE_ROOT + defect.ImagePath;
									}
										CString strFileName = strSrcImagePath;
									int nLastSlash = max(strSrcImagePath.ReverseFind('\\'), strSrcImagePath.ReverseFind('/'));
									if (nLastSlash >= 0)
										strFileName = strSrcImagePath.Mid(nLastSlash + 1);
									CString strDestImagePath = strAoiImagePath + _T("\\") + strFileName;
									theApp.m_pFTPLog->Debug(_T("[DFS] Defect[%d] ImagePath: src=%s, dest=%s"), 
										nDefectIndex, strSrcImagePath, strDestImagePath);

									if (FileExists(strSrcImagePath))
									{
										if (!::CopyFile(strSrcImagePath, strDestImagePath, FALSE))
										{
											theApp.m_pFTPLog->Error(_T("[DFS] DefectImage CopyFile FAILED: src=%s, dest=%s, error=%d"), 
												strSrcImagePath, strDestImagePath, GetLastError());
										}
										else
										{
											theApp.m_pFTPLog->Debug(_T("[DFS] DefectImage copied: %s -> AOI\\Image"), strSrcImagePath);
										}
									}
										else
										{
											theApp.m_pFTPLog->Info(_T("ImagePath file not found: %s"), strSrcImagePath);
										}
									}
								}
							}

							// ===== 复制特殊图片（L*.bmp 等级图、MarkImg.jpg 标记图） =====
							// 优先从缺陷表 ImagePath 获取目录路径
							CString strAoiImageDir;
							BOOL bFoundAoiDir = FALSE;
							for (const auto& defect : defectList)
							{
								if (!defect.ImagePath.IsEmpty())
								{
									CString strSrcImagePath = DEFAULT_MAIN_AOI_IMAGE_ROOT + defect.ImagePath;
									int nLastSlash = max(strSrcImagePath.ReverseFind('\\'), strSrcImagePath.ReverseFind('/'));
									if (nLastSlash >= 0)
									{
										strAoiImageDir = strSrcImagePath.Left(nLastSlash);  // PanelID 目录
										theApp.m_pFTPLog->Debug(_T("[DFS] Try AoiImageDir from defect ImagePath: %s"), strAoiImageDir);
										// 验证目录是否存在
										if (PathIsDirectory(strAoiImageDir))
										{
											bFoundAoiDir = TRUE;
											break;
										}
										else
										{
											theApp.m_pFTPLog->Debug(_T("[DFS] Defect ImagePath dir not exist: %s"), strAoiImageDir);
										}
									}
									break;
								}
							}

							// 如果缺陷表 ImagePath 获取不到，则从数据库字段构建路径
							// 路径结构：MainAOI\<LocalIP>\<PlatformID+1>\<StartTime日期>\<PanelID>
							if (!bFoundAoiDir && !inspResult.LocalIP.IsEmpty() && inspResult.PlatformID >= 0)
							{
								theApp.m_pFTPLog->Debug(_T("[DFS] Building path from DB fields: LocalIP=%s, PlatformID=%d, StartTime=%s"), 
									(LPCTSTR)inspResult.LocalIP, inspResult.PlatformID, inspResult.StartTime.Format(_T("%Y-%m-%d")));
								
								// 构建路径：MainAOI\<LocalIP>\<PlatformID+1>\<Date>\<PanelID>
								CString strIndexDir;
								strIndexDir.Format(_T("%d"), inspResult.PlatformID + 1);  // Index = PlatformID + 1
								
								CString strDateDir = inspResult.StartTime.Format(_T("%Y-%m-%d"));  // 日期
								
								strAoiImageDir.Format(_T("%sMainAOI\\%s\\%s\\%s\\%s"), 
									DEFAULT_MAIN_AOI_IMAGE_ROOT,
									(LPCTSTR)inspResult.LocalIP,
									(LPCTSTR)strIndexDir,
									(LPCTSTR)strDateDir,
									(LPCTSTR)strPanelID);
								
								theApp.m_pFTPLog->Debug(_T("[DFS] Built AoiImageDir from DB: %s"), strAoiImageDir);
								
								// 检查目录是否存在
								if (PathIsDirectory(strAoiImageDir))
								{
									bFoundAoiDir = TRUE;
								}
								else
								{
									theApp.m_pFTPLog->Debug(_T("[DFS] Built AoiImageDir not exist: %s"), strAoiImageDir);
								}
							}

							if (!strAoiImageDir.IsEmpty())
							{
								theApp.m_pFTPLog->Debug(_T("[DFS] AOI Image Dir=%s"), strAoiImageDir);
								// 复制 L*.bmp (等级标记图)
								CFileFind fileFind;
								CString strSearchPattern = strAoiImageDir + _T("\\L*.bmp");
								BOOL bFind = fileFind.FindFile(strSearchPattern);
								int nGradeImageCount = 0;
								while (bFind)
								{
									bFind = fileFind.FindNextFile();
									if (!fileFind.IsDots() && !fileFind.IsDirectory())
									{
										CString strSrcFile = fileFind.GetFilePath();
										CString strDestFile = strAoiImagePath + _T("\\") + fileFind.GetFileName();
										if (!::CopyFile(strSrcFile, strDestFile, FALSE))
										{
											theApp.m_pFTPLog->Error(_T("[DFS] GradeImage CopyFile FAILED: src=%s, dest=%s, error=%d"), 
												strSrcFile, strDestFile, GetLastError());
										}
										else
										{
											nGradeImageCount++;
											theApp.m_pFTPLog->Debug(_T("[DFS] GradeImage copied: %s -> AOI\\Image"), strSrcFile);
										}
									}
								}
								fileFind.Close();
								theApp.m_pFTPLog->Info(_T("[DFS] Grade images copied: count=%d"), nGradeImageCount);

								// 复制 MarkImg.jpg (Mark 标记图)，重命名为 AddsrcImageADD.jpg
								CString strMarkSrc = strAoiImageDir + _T("\\MarkImg.jpg");
								CString strMarkDest = strAoiImagePath + _T("\\AddsrcImageADD.jpg");
								if (FileExists(strMarkSrc))
								{
									if (!::CopyFile(strMarkSrc, strMarkDest, FALSE))
									{
										theApp.m_pFTPLog->Error(_T("[DFS] AddsrcImageADD CopyFile FAILED: src=%s, dest=%s, error=%d"), 
											strMarkSrc, strMarkDest, GetLastError());
									}
									else
									{
										theApp.m_pFTPLog->Info(_T("[DFS] AddsrcImageADD copied: %s -> %s"), strMarkSrc, strMarkDest);
									}
								}
							}
							else
							{
								theApp.m_pFTPLog->Info(_T("[DFS] No AOI Image Dir found, skip grade/mark image copy"));
							}
							// ===== 复制特殊图片结束 =====
						}
						else
						{
							theApp.m_pFTPLog->Info(_T("QueryByBarcode failed for PanelID: %s"), strPanelID);
						}
					}
					// ===== 按 ImagePath 复制结束 =====

					// ===== Lumitop CSV 检查日志 =====
					CString strLumitopFullPath = DFS_SHARE_PATH + GetDateString2() + _T("\\") + strPanelID + _T("\\LUMITOP\\") + strPanelID + _T(".csv");
					if (FileExists(strLumitopFullPath)) {
						theApp.m_pFTPLog->Debug(_T("[DFS] Lumitop CSV exists=%s"), strLumitopFullPath);
					}
					else {
						theApp.m_pFTPLog->Info(_T("[DFS] Lumitop File Path Error : %s,"), strLumitopFullPath);
					}
					// ===== Lumitop CSV 检查结束 =====

					// ===== 汇总图片到 SUM 目录 =====
					theApp.m_pFTPLog->Info(_T("[DFS] CopyImage Start: src=%s, dest=%s"), strAoiImagePath, strSumImagePath);
					DfsInfo.CopyImage(strAoiImagePath, strSumImagePath);
					theApp.m_pFTPLog->Info(_T("[DFS] CopyImage End"));
					//DfsInfo.CopyImage(strViewingImagePath, strSumImagePath);

					strSumPath = strTemp1 + strPanelID + _T(".csv");
					//strSumImagePath = strTemp1 + _T("Image");

					theApp.m_pFTPLog->Info(_T("[DFS] SetLoadFile Start: PanelID=%s"), strPanelID);
					DfsDataValue result;
					result = DataInfo.SetLoadFile(strPanelID);
					theApp.m_pFTPLog->Info(_T("[DFS] SetLoadFile End: ChNum=%s, PreGammaContactStatus=%s"), 
						result.m_ChNum, result.m_PreGammaContactStatus);

					if (_ttoi(result.m_ChNum) > 2)
					{
						if (result.m_ChNum == _T("3"))
							result.m_ChNum = _T("1");
						else
							result.m_ChNum = _T("2");
					}
					
					/*
					DfsInfo.m_EQPDataInfo.strRecipe_No = dfsData.m_ModelID;
					DfsInfo.m_EQPDataInfo.strLoad_Stage_No = result.m_ChNum;
					DfsInfo.m_EQPDataInfo.strUnload_Stage_No = CStringSupport::FormatString(_T("%d"), dfsData.m_StageNum);
					DfsInfo.m_EQPDataInfo.strStart_Time = dfsData.m_StartTime;
					DfsInfo.m_EQPDataInfo.strEnd_Time = dfsData.m_EndTime;
					DfsInfo.m_EQPDataInfo.strPre_Gamma_Time = dfsData.m_PreGammaTime;
					DfsInfo.m_EQPDataInfo.strTP_Time = dfsData.m_TpTime;
					DfsInfo.m_EQPDataInfo.strTact_Time = dfsData.m_TactTime;
					DfsInfo.m_EQPDataInfo.strContact = dfsData.m_Contact;
					DfsInfo.m_EQPDataInfo.strPreGamma= dfsData.m_PreGamma;
					DfsInfo.m_EQPDataInfo.strAOIInpsect = dfsData.m_AOIInpsect;
					DfsInfo.m_EQPDataInfo.strTpResult = dfsData.m_TpResult2;
					DfsInfo.m_EQPDataInfo.strLumitop =  dfsData.m_Lumitop;
					DfsInfo.m_EQPDataInfo.strmura =  dfsData.m_mura;
					*/

					//DfsInfo.m_EQPDataInfo.strAOI_RECIPE_NAME = dfsData.m_ModelID;
					//DfsInfo.m_EQPDataInfo.strPG_RECIPE_NAME = result.m_ChNum;
					//DfsInfo.m_EQPDataInfo.strTP_RECIPE_NAME = CStringSupport::FormatString(_T("%d"), dfsData.m_StageNum);
					theApp.m_pDataStatusLog->Info(CStringSupport::FormatString(_T(" GetEQPDataInfo()_DFS Start time dfsData.m_StartTime : %s"), dfsData.m_StartTime));
					DfsInfo.m_EQPDataInfo.strSTART_TIME = dfsData.m_StartTime;
					DfsInfo.m_EQPDataInfo.strEND_TIME = dfsData.m_EndTime;
					DfsInfo.m_EQPDataInfo.strLOAD_STAGE_NO = dfsData.m_LoadeHandlerNUM;
					DfsInfo.m_EQPDataInfo.strINSP_STAGE_NO = dfsData.m_ChNum;
					DfsInfo.m_EQPDataInfo.strUNLOAD_STAGE_NO = dfsData.m_UnLoadeHandlerNUM;	
					if (DfsInfo.m_EQPDataInfo.strUNLOAD_STAGE_NO == _T("0"))
						DfsInfo.m_EQPDataInfo.strUNLOAD_STAGE_NO = _T("1");
					DfsInfo.m_EQPDataInfo.strPROBE_CONTACT_CNT = dfsData.m_ContactCount;
					//DfsInfo.m_EQPDataInfo.strINDEX_PANEL_GRADE = dfsData.m_PreGamma;
					//DfsInfo.m_EQPDataInfo.strINDEX_MAIN_CODE = dfsData.m_AOIInpsect;
					//DfsInfo.m_EQPDataInfo.strFINAL_PANEL_GRADE = dfsData.m_TpResult2;
					//DfsInfo.m_EQPDataInfo.strFINAL_MAIN_CODE = dfsData.m_Lumitop;
					DfsInfo.m_EQPDataInfo.strOPERATOR_ID = theApp.m_OpvSocketManager[_ttoi(DfsInfo.m_EQPDataInfo.strUNLOAD_STAGE_NO)-1].m_strOPID;

					DfsInfo.m_PanelSummaryInfo.CONTACT_PAENL_GRADE = dfsData.m_Contact;
					DfsInfo.m_PanelSummaryInfo.AOI_PAENL_GRADE = dfsData.m_AOIInpsect;
					DfsInfo.m_PanelSummaryInfo.PRE_PAENL_GRADE = dfsData.m_PreGamma;
					DfsInfo.m_PanelSummaryInfo.DOT_PAENL_GRADE = dfsData.m_TpResult2;
					DfsInfo.m_PanelSummaryInfo.LUMITOP_PAENL_GRADE = dfsData.m_Lumitop;
					DfsInfo.m_PanelSummaryInfo.OPV_PAENL_GRADE = dfsData.m_opViewResult;
					DfsInfo.m_PanelSummaryInfo.OPERATOR_ID = theApp.m_OpvSocketManager[_ttoi(DfsInfo.m_EQPDataInfo.strUNLOAD_STAGE_NO) - 1].m_strOPID;
	
					if (_ttoi(result.m_PreGammaContactStatus) == m_dfsPreGammaNG || _ttoi(result.m_PreGammaContactStatus) == m_dfsContactNG || _ttoi(result.m_TpResult) == m_dfsTpNG)
						DfsInfo.AddDefectCodeResult(strPanelID, _ttoi(result.m_PreGammaContactStatus), _ttoi(result.m_TpResult), Machine_ULD);
					//>>PG DFS Load
					theApp.m_pFTPLog->Info(_T("[DFS] PGDfsInfoLoad Start: PanelID=%s"), strPanelID);
					DfsInfo.PGDfsInfoLoad(strPanelID);
					theApp.m_pFTPLog->Info(_T("[DFS] PGDfsInfoLoad End"));
					//<<

					theApp.m_pFTPLog->Info(_T("[DFS] AMTAFTSavePanelDFS_SUM Start: PanelID=%s"), strPanelID);
					DfsInfo.AMTAFTSavePanelDFS_SUM(result, strPanelID, strFpcID, strAOIPath, strViewingPath, strLumitopPath, strOpvFilPath, strSumPath);
					theApp.m_pFTPLog->Info(_T("[DFS] AMTAFTSavePanelDFS_SUM End, SUM Path=%s"), strSumPath);

				BOOL bTransfer = TRUE;

				if (!FileExists(strSumPath)){
					theApp.m_pFTPLog->Info2(CStringSupport::FormatString(_T("[%s] Inspect Not exist csv File"), strPanelID));
					theApp.m_pFTPLog->Info(_T("[DFS] ERROR: SUM CSV not exist=%s"), strSumPath);
				}
				else
				{
					// 获取文件大小用于日志
					WIN32_FIND_DATA findData;
					HANDLE hFind = FindFirstFile(strSumPath, &findData);
					int nFileSize = 0;
					if (hFind != INVALID_HANDLE_VALUE) {
						nFileSize = (int)(findData.nFileSizeLow / 1024); // KB
						FindClose(hFind);
					}
					theApp.m_pFTPLog->Info(_T("[DFS] SUM CSV exists=%s, size=%d KB"), strSumPath, nFileSize);
					CString strUploadEQPID = CStringSupport::FormatString(_T("%s%s"), theApp.m_strEqpId, theApp.m_strEqpNum);

						if (strPanelID.GetLength() >= DFS_CHECK_PANEL_SIZE)
						{
							if (theApp.m_bDFSTestMode == TRUE)
								strCsvFilePath = _T("D:\\TEST");
							else
								strCsvFilePath = _T("\\\\172.18.3.110\\module");

							SetFilePath(&strCsvFilePath, theApp.m_strEqpId);
							SetFilePath(&strCsvFilePath, GetDateString2());
							SetFilePath(&strCsvFilePath, DfsInfo.m_PanelDataBegin.strPanel_ID.Left(5));
							SetFilePath(&strCsvFilePath, DfsInfo.m_PanelDataBegin.strPanel_ID.Left(8));
							SetFilePath(&strCsvFilePath, DfsInfo.m_PanelDataBegin.strPanel_ID);

							strImageFilePath = strCsvFilePath;
							SetFilePath(&strImageFilePath, _T("Image"));
							CreateFolders(strImageFilePath);
							theApp.m_pFTPLog->Info(_T("[DFS] Image Path created=%s"), strImageFilePath);

							SetFilePath(&strCsvFilePath, _T("Data"));
							CreateFolders(strCsvFilePath);
							theApp.m_pFTPLog->Info(_T("[DFS] Data Path created=%s"), strCsvFilePath);

							if (theApp.m_bDFSTestMode == TRUE)
								strLinkFilePath = strIndexFilePath = _T("D:\\TEST");
							else
								strLinkFilePath = strIndexFilePath = _T("\\\\172.18.3.110\\module");

							// Index 파일 생성
							SetFilePath(&strIndexFilePath, _T("INDEX"));
							SetFilePath(&strIndexFilePath, theApp.m_strEqpId);
							CreateFolders(strIndexFilePath);

							// Link 파일 생성
							SetFilePath(&strLinkFilePath, _T("LINK"));
							SetFilePath(&strLinkFilePath, theApp.m_strEqpId);
							SetFilePath(&strLinkFilePath, DfsInfo.m_PanelDataBegin.strPanel_ID.Left(5));
							SetFilePath(&strLinkFilePath, DfsInfo.m_PanelDataBegin.strPanel_ID.Left(8));
							SetFilePath(&strLinkFilePath, DfsInfo.m_PanelDataBegin.strPanel_ID);
							CreateFolders(strLinkFilePath);

							// ProcessID는 설비별로 따로 있고 GOOD,NG 일때 또 따로 있는데 
							// 담당자가 DFS파일 이름은 설비 ProcessID로 하라고 해서 이렇게 했음
							// 만약에 결과값에 따라 바뀐다고 하면 strProcessID -> DfsInfo.m_PanelDataBegin.strProcess_ID 이걸로 하면됩니다.
							CString strProcessID = _T("");
							if (!DfsInfo.m_HeaderInfo.strEQP_Type.CompareNoCase(_T("MFBAP")))
								strProcessID = _T("1700");
							else if (!DfsInfo.m_HeaderInfo.strEQP_Type.CompareNoCase(_T("MFGAP")))
								strProcessID = _T("1L00");

							strLinkFilePath = strLinkFilePath + _T("\\") + strProcessID + _T("_") + DfsInfo.m_PanelDataBegin.strPanel_ID + _T("_") + GetNowSystemTimeMillisecondsSirius() + _T(".csv");
							strCsvFilePath = strCsvFilePath + _T("\\") + strProcessID + _T("_") + DfsInfo.m_PanelDataBegin.strPanel_ID + _T("_") + GetNowSystemTimeMillisecondsSirius() + _T(".csv");

							theApp.m_pFTPLog->Info(_T("[DFS] Link File Path=%s"), strLinkFilePath);
							theApp.m_pFTPLog->Info(_T("[DFS] CSV File Path=%s"), strCsvFilePath);

							if (!::CopyFile(strSumPath, strLinkFilePath, FALSE))
							{
								theApp.m_pFTPLog->Error(_T("[DFS] CopyFile FAILED: source=%s, dest=%s, error=%d"), 
									strSumPath, strLinkFilePath, GetLastError());
							}
							else
							{
								theApp.m_pFTPLog->Info(_T("[DFS] Link File copied SUCCESS, exist=%d"), FileExists(strLinkFilePath));
							}
							if (!::MoveFile(strSumPath, strCsvFilePath))
							{
								theApp.m_pFTPLog->Error(_T("[DFS] MoveFile FAILED: source=%s, dest=%s, error=%d"), 
									strSumPath, strCsvFilePath, GetLastError());
							}
							else
							{
								theApp.m_pFTPLog->Info(_T("[DFS] CSV File moved SUCCESS, exist=%d"), FileExists(strCsvFilePath));
							}

							if (theApp.m_bDFSTestMode == TRUE)
								strCsvFilePath.Replace(_T("D:\\TEST\\"), _T("/MODULE/"));
							else
								strCsvFilePath.Replace(_T("\\\\172.18.3.110\\module"), _T("/MODULE"));

							strCsvFilePath.Replace(_T("\\"), _T("/"));
							m_vecIndexValue.push_back(strCsvFilePath);

							if (!DfsInfo.VisionLoadPanelDFSInfo(strPanelID, Machine_ULD))
								theApp.m_pFTPLog->Info(_T("OPV Vision Dfs File Path Error : %s,"), strPanelID);

							for (int i = 0; i < DfsInfo.m_OpvDataList[Machine_ULD].size() && bTransfer; i++)
							{

								strSrc = strSumImagePath + _T("\\") + DfsInfo.m_OpvDataList[Machine_ULD].at(i).strImage;
								strDest = strImageFilePath + _T("\\") + DfsInfo.m_OpvDataList[Machine_ULD].at(i).strImage;

								if (FileExists(strSrc))
								{
									// 目标文件存在时先删除
									if (FileExists(strDest))
									{
										theApp.m_pFTPLog->Debug(_T("[DFS] OPV Image: dest exists, delete first: %s"), strDest);
										::DeleteFile(strDest);
									}

									// 使用 CopyFile + DeleteFile 替代 MoveFile（更稳定的跨网络操作）
									BOOL bCopySuccess = FALSE;
									for (int nRetry = 0; nRetry < 3; nRetry++)
									{
										if (::CopyFile(strSrc, strDest, FALSE))
										{
											bCopySuccess = TRUE;
											break;
										}
										int nError = GetLastError();
										if (nError == 80)  // 文件被占用，等待后重试
										{
											theApp.m_pFTPLog->Debug(_T("[DFS] OPV Image CopyFile retry %d: error=%d"), nRetry + 1, nError);
											Sleep(100);
										}
										else
										{
											break;  // 其他错误不再重试
										}
									}

									if (!bCopySuccess)
									{
										theApp.m_pFTPLog->Error(_T("[DFS] OPV Image CopyFile FAILED: src=%s, dest=%s, error=%d"), 
											strSrc, strDest, GetLastError());
									}
									else
									{
										theApp.m_pFTPLog->Info(_T("[DFS] OPV Image copied: %s -> %s"), strSrc, strDest);
									}
									if (theApp.m_bDFSTestMode == TRUE)
										strDest.Replace(_T("D:\\TEST\\"), _T("/MODULE/"));
									else
										strDest.Replace(_T("\\\\172.18.3.110\\module"), _T("/MODULE"));

									//strDest.Replace(theApp.m_strEqpId, strUploadEQPID);

									strDest.Replace(_T("\\"), _T("/"));
									m_vecIndexValue.push_back(strDest);
								}
								else
									theApp.m_pFTPLog->Info2(_T("Vision Not exist image file"));
							}

							// ===== 额外上传 L255.bmp 和 AddsrcImageADD.jpg =====
							CString strExtraImgList[2] = { _T("L255.bmp"), _T("AddsrcImageADD.jpg") };
							for (int n = 0; n < 2; n++)
							{
								CString strImgName = strExtraImgList[n];
								strSrc = strSumImagePath + _T("\\") + strImgName;
								strDest = strImageFilePath + _T("\\") + strImgName;

								if (FileExists(strSrc))
								{
									// 目标文件存在时先删除
									if (FileExists(strDest))
									{
										theApp.m_pFTPLog->Debug(_T("[DFS] Extra Image: dest exists, delete first: %s"), strDest);
										::DeleteFile(strDest);
									}

									// 使用 CopyFile + DeleteFile 替代 MoveFile
									BOOL bCopySuccess = FALSE;
									for (int nRetry = 0; nRetry < 3; nRetry++)
									{
										if (::CopyFile(strSrc, strDest, FALSE))
										{
											bCopySuccess = TRUE;
											break;
										}
										int nError = GetLastError();
										if (nError == 80)  // 文件被占用，等待后重试
										{
											theApp.m_pFTPLog->Debug(_T("[DFS] Extra Image CopyFile retry %d: %s, error=%d"), nRetry + 1, strImgName, nError);
											Sleep(100);
										}
										else
										{
											break;
										}
									}

									if (!bCopySuccess)
									{
										theApp.m_pFTPLog->Error(_T("[DFS] Extra Image CopyFile FAILED: %s, error=%d"), strImgName, GetLastError());
									}
									else
									{
										theApp.m_pFTPLog->Info(_T("[DFS] Extra Image copied: %s -> %s"), strSrc, strDest);
									}
								}
								else
								{
									theApp.m_pFTPLog->Debug(_T("[DFS] Extra Image not found (ignored): %s"), strSrc);
								}
							}

							//전체Image File Name 항상 통일
							strSrc = strSumImagePath + _T("\\") + _T("AddsrcImageADD.jpg");
							strDest = strImageFilePath + _T("\\") + strProcessID + _T("_") + DfsInfo.m_PanelDataBegin.strPanel_ID + GetNowSystemTimeMillisecondsSirius4() + _T(".jpg");

							//CString strDest2 = strSumImagePath + _T("\\") + strProcessID + _T("_") + DfsInfo.m_PanelDataBegin.strPanel_ID + GetNowSystemTimeMillisecondsSirius4() + _T(".jpg");
							COleDateTime now;
							now = COleDateTime::GetCurrentTime();
							CString strDest2;
							strDest2.Format(_T("%s\\%s_%s_%s_Layout_%s_%02d%02d%02d.jpg"), strImageFilePath, strProcessID, DfsInfo.m_PanelDataBegin.strPanel_ID, theApp.m_strEqpId + theApp.m_strEqpNum, GetDateString2(), now.GetHour(), now.GetMinute(), now.GetSecond());
							if (FileExists(strSrc))
							{
								// 目标文件存在时先删除
								if (FileExists(strDest2))
								{
									theApp.m_pFTPLog->Debug(_T("[DFS] Layout Image: dest exists, delete first: %s"), strDest2);
									::DeleteFile(strDest2);
								}

								// 使用 CopyFile + DeleteFile 替代 MoveFile（更稳定的跨网络操作）
								BOOL bCopySuccess = FALSE;
								for (int nRetry = 0; nRetry < 3; nRetry++)
								{
									if (::CopyFile(strSrc, strDest2, FALSE))
									{
										bCopySuccess = TRUE;
										break;
									}
									int nError = GetLastError();
									if (nError == 80)  // 文件被占用，等待后重试
									{
										theApp.m_pFTPLog->Debug(_T("[DFS] Layout Image CopyFile retry %d: error=%d"), nRetry + 1, nError);
										Sleep(100);
									}
									else
									{
										break;  // 其他错误不再重试
									}
								}

								if (!bCopySuccess)
								{
									theApp.m_pFTPLog->Error(_T("[DFS] Layout Image CopyFile FAILED: src=%s, dest=%s, error=%d"), 
										strSrc, strDest2, GetLastError());
								}
								else
								{
									theApp.m_pFTPLog->Info(_T("[DFS] Layout Image copied: %s -> %s"), strSrc, strDest2);
								}
								

							strSrc = strSumImagePath;
								//strDest = strImageFilePath;

								DfsInfo.CopyImage2(strSrc, strDest);
								if (theApp.m_bDFSTestMode == TRUE)
									strDest2.Replace(_T("D:\\TEST\\"), _T("/MODULE/"));
								else
									strDest2.Replace(_T("\\\\172.18.3.110\\module"), _T("/MODULE"));

								//strDest2.Replace(theApp.m_strEqpId, strUploadEQPID);

								strDest2.Replace(_T("\\"), _T("/"));
								m_vecIndexValue.push_back(strDest2);
							}
							else
								theApp.m_pFTPLog->Info2(_T("Not exist Layout image file"));

							DfsIDXFileCreate(strUploadEQPID, &strIndexFile);
							strDest = strIndexFilePath + _T("\\") + GetDateString2() + _T("_") + strUploadEQPID + _T(".csv");

							theApp.m_pFTPLog->Info(_T("[DFS] INDEX File created=%s, CSV=%s, Link=%s"), 
								strIndexFile, strCsvFilePath, strLinkFilePath);

							if (!::CopyFile(strIndexFile, strDest, FALSE))
							{
								theApp.m_pFTPLog->Error(_T("[DFS] INDEX CopyFile FAILED: src=%s, dest=%s, error=%d"), 
									strIndexFile, strDest, GetLastError());
							}
							else
							{
								theApp.m_pFTPLog->Info(_T("[DFS] INDEX File copied SUCCESS: %s -> %s"), strIndexFile, strDest);
							}
							CString strDelete;
							strDelete = DFS_SHARE_PATH + GetDateString2() + _T("\\") + strPanelID;
							//DeleteFolderAndFile(strDelete,0);

							if (theApp.m_bDFSTestMode == TRUE)
								strAlramFilePath = _T("D:\\TEST");
							else
								strAlramFilePath = _T("\\\\172.18.3.110\\module");

							SetFilePath(&strAlramFilePath, theApp.m_strEqpId);
							SetFilePath(&strAlramFilePath, _T("Alram"));

							CString FileName, strString, strTemp, strShift;
						//	strShift = theApp.m_lastShiftIndex == 0 ? _T("DY") : _T("NT");
						//	strOriAlramPath.Format(_T("%s%s_AlarmLog_%s.csv"), DATA_ALARM_PATH, theApp.m_strCurrentToday, strShift);
						//	strAlramFilePath.Format(_T("%s%s_AlarmLog_%s.csv"), strAlramFilePath, theApp.m_strCurrentToday, strShift);
						//	::CopyFile(strOriAlramPath, strAlramFilePath, FALSE);			//index 파일 업로드 221027 yb
						}
						else
						{
							theApp.m_pFTPLog->Info2(_T("Panel ID length is short."));
						}

					}
					// ===== DFS 处理完成日志 =====
					theApp.m_pFTPLog->Info(_T("[DFS] <<< PanelID=%s DFS Process COMPLETE, Queue remaining=%d"), 
						strPanelID, m_DfsUploadtransferFileList.size());
				}

				Delay(10, TRUE);

				m_csDfsUploadLock.Lock();
				m_DfsUploadtransferFileList.pop();
				m_csDfsUploadLock.Unlock();
#else
				if (strPanelID.IsEmpty() == FALSE)
				{
					DfsInfo.m_EQPDataInfo.strRecipe_No = Int2String(theApp.m_CurrentModel.m_AlignPcCurrentModelNum);
					DfsInfo.m_EQPDataInfo.strRecipe_Name = theApp.m_CurrentModel.m_AlignPcCurrentModelName;
					DfsInfo.m_EQPDataInfo.strStart_Time = dfsData.m_StartTime;
					DfsInfo.m_EQPDataInfo.strEnd_Time = dfsData.m_EndTime;
					//DfsInfo.m_EQPDataInfo.strLD_Time = dfsData.m_LoadHandlerTime;
					//DfsInfo.m_EQPDataInfo.strUld_Time = dfsData.m_UnloadHandlerTime;
					DfsInfo.m_EQPDataInfo.strPre_Gamma_Time = dfsData.m_PreGammaTime;
					DfsInfo.m_EQPDataInfo.strTact_Time = dfsData.m_TactTime;
					DfsInfo.m_EQPDataInfo.strOperator_ID = theApp.m_strEqpId + theApp.m_strEqpNum;
					DfsInfo.m_EQPDataInfo.strUnit_ID = dfsData.m_IndexNum;
					DfsInfo.m_EQPDataInfo.strStage_ID = dfsData.m_ChNum; // 20200401 kty
					DfsInfo.m_EQPDataInfo.strProbe_Contact_Cnt = _T("1");
				
					strTemp1 = DFS_SHARE_PATH + GetDateString2() + _T("\\") + strPanelID;
					CreateFolders(strTemp1);
					strSumPath = strTemp1 + _T("\\") + strPanelID + _T(".csv");
					DfsInfo.GammaSavePanelDFS_SUM(strPanelID, strFpcID, strSumPath);
				
					BOOL bTransfer = TRUE;
				
					if (!FileExists(strSumPath)){
						theApp.m_pFTPLog->Info2(CStringSupport::FormatString(_T("[%s][%s] Inspect Not exist csv File"), strPanelID, strFpcID));
					}
					else
					{
						if (strPanelID.GetLength() >= DFS_CHECK_PANEL_SIZE)
						{
							if (theApp.m_bDFSTestMode == TRUE)
								strCsvFilePath = _T("D:\\TEST");
							else
								strCsvFilePath = _T("\\\\172.18.3.110\\module");
				
							SetFilePath(&strCsvFilePath, theApp.m_strEqpId);
							SetFilePath(&strCsvFilePath, GetDateString2());
							SetFilePath(&strCsvFilePath, DfsInfo.m_PanelDataBegin.strPanel_ID.Left(5));
							SetFilePath(&strCsvFilePath, DfsInfo.m_PanelDataBegin.strPanel_ID.Left(8));
							SetFilePath(&strCsvFilePath, DfsInfo.m_PanelDataBegin.strPanel_ID);
							SetFilePath(&strCsvFilePath, _T("Data"));
							CreateFolders(strCsvFilePath);
				
							if (theApp.m_bDFSTestMode == TRUE)
								strLinkFilePath = strIndexFilePath = _T("D:\\TEST");
							else
								strLinkFilePath = strIndexFilePath = _T("\\\\172.18.3.110\\module");
							
							SetFilePath(&strIndexFilePath, _T("INDEX"));
							SetFilePath(&strIndexFilePath, theApp.m_strEqpId);
							
							SetFilePath(&strLinkFilePath, _T("LINK"));
							SetFilePath(&strLinkFilePath, theApp.m_strEqpId);
							SetFilePath(&strLinkFilePath, DfsInfo.m_PanelDataBegin.strPanel_ID.Left(5));
							SetFilePath(&strLinkFilePath, DfsInfo.m_PanelDataBegin.strPanel_ID.Left(8));
							SetFilePath(&strLinkFilePath, DfsInfo.m_PanelDataBegin.strPanel_ID);
							
							CreateFolders(strLinkFilePath);
							CreateFolders(strIndexFilePath);
				
							strLinkFilePath = strLinkFilePath + _T("\\") + DfsInfo.m_PanelDataBegin.strProcess_ID + _T("_") + DfsInfo.m_PanelDataBegin.strPanel_ID + _T("_") + GetNowSystemTimeMillisecondsSirius() + _T(".csv");
							strCsvFilePath = strCsvFilePath + _T("\\") + DfsInfo.m_PanelDataBegin.strProcess_ID + _T("_") + DfsInfo.m_PanelDataBegin.strPanel_ID + _T("_") + GetNowSystemTimeMillisecondsSirius() + _T(".csv");

							if (!::CopyFile(strSumPath, strLinkFilePath, FALSE))
							{
								theApp.m_pFTPLog->Error(_T("[DFS][Gamma] CopyFile FAILED: source=%s, dest=%s, error=%d"), 
									strSumPath, strLinkFilePath, GetLastError());
							}
							else
							{
								theApp.m_pFTPLog->Info(_T("[DFS][Gamma] Link File copied: %s -> %s"), strSumPath, strLinkFilePath);
							}
							if (!::CopyFile(strSumPath, strCsvFilePath, FALSE))
							{
								theApp.m_pFTPLog->Error(_T("[DFS][Gamma] CopyFile FAILED: source=%s, dest=%s, error=%d"), 
									strSumPath, strCsvFilePath, GetLastError());
							}
							else
							{
								theApp.m_pFTPLog->Info(_T("[DFS][Gamma] CSV File copied: %s -> %s"), strSumPath, strCsvFilePath);
							}
				
							if (theApp.m_bDFSTestMode == TRUE)
								strDest.Replace(_T("D:\\TEST\\"), _T("/MODULE/"));
							else
								strDest.Replace(_T("\\\\172.18.3.110\\module"), _T("/MODULE"));
							
							strCsvFilePath.Replace(_T("\\"), _T("/"));
							m_vecIndexValue.push_back(strCsvFilePath);
							
							DfsIDXFileCreate(DfsInfo.m_HeaderInfo.strEQP_ID, &strIndexFile);
							strDest = strIndexFilePath + _T("\\") + GetDateString2() + _T("_") + DfsInfo.m_HeaderInfo.strEQP_ID + _T(".csv");

							if (!::CopyFile(strIndexFile, strDest, FALSE))
							{
								theApp.m_pFTPLog->Error(_T("[DFS][Gamma] INDEX CopyFile FAILED: src=%s, dest=%s, error=%d"), 
									strIndexFile, strDest, GetLastError());
							}
							else
							{
								theApp.m_pFTPLog->Info(_T("[DFS][Gamma] INDEX File copied: %s -> %s"), strIndexFile, strDest);
							}
						}
						else
							theApp.m_pFTPLog->Info2(_T("Panel ID length is short."));
					}
				}
				Delay(10, TRUE);
				
				m_csDfsUploadLock.Lock();
				m_DfsUploadtransferFileList.pop();
				m_csDfsUploadLock.Unlock();
#endif
			}
		}
	}
}

#if _SYSTEM_AMTAFT_
void CDFSClient::AddTransferFile(DfsDataValue strTransferFile)
{
	if (strTransferFile.m_PanelID.IsEmpty())
		theApp.m_pFTPLog->Info(_T("PanelID Error"));

	m_csLock.Lock();
	m_transferFileList.push(strTransferFile);
	m_csLock.Unlock();
}

void CDFSClient::RunFtpUploadThread()
{
	CDFSInfo DfsInfo;
	CDataInfo OpvInfo, DataInfo;
	DfsDataValue dfsData;

	CString strFpcID, strPanelID;
	CString strSumImagePath, strTemp1;

	theApp.m_pFTPLog->Info(_T("[FTP] RunFtpUploadThread START"));

	while (::WaitForSingleObject(m_hQuit, 1000) != WAIT_OBJECT_0)
	{
		if (!m_transferFileList.empty())
		{
			while (!m_transferFileList.empty())
			{
				m_csLock.Lock();

				dfsData.Reset();
				OpvInfo.Clear();
				DfsInfo.m_strVisionResult = "";
				DfsInfo.m_strViewingResult = "";
				DfsInfo.m_strLumitopResult = "";
				DfsInfo.m_OpvDataList[Machine_AOI].clear();

				dfsData = m_transferFileList.front();
				strPanelID = dfsData.m_PanelID;
				strFpcID = dfsData.m_FpcID;
				theApp.m_pFTPLog->Debug(_T("[FTP] Queue front - PanelID : %s, FPCID : %s, TypeNum=%d, StageNum=%d, ChNum=%s, Lumitop=%s"),
					strPanelID, strFpcID, dfsData.m_TypeNum, dfsData.m_StageNum, dfsData.m_ChNum, dfsData.m_Lumitop);
				m_csLock.Unlock();

				if (strPanelID.IsEmpty() == FALSE)
				{
					theApp.m_pFTPLog->Info(_T("[FTP] Processing PanelID: %s, FPCID: %s"), strPanelID, strFpcID);

					DfsInfo.IndexZoneInspResultInfo(strPanelID);
					theApp.m_pFTPLog->Debug(_T("[FTP] IndexZoneInspResultInfo completed for PanelID: %s"), strPanelID);

					if (!DfsInfo.VisionLoadPanelDFSInfo(strPanelID, Machine_AOI)) // 이건 OPV .txt 파일 용입니다.
						theApp.m_pFTPLog->Info(_T("[FTP] OPV Vision Dfs File Path Error : %s,"), strPanelID);
					else
						theApp.m_pFTPLog->Debug(_T("[FTP] VisionLoadPanelDFSInfo SUCCESS for PanelID: %s"), strPanelID);

					if (theApp.m_bSameDefectMode == TRUE)
					{
						for (auto AoiDefect : DfsInfo.m_OpvDataList[Machine_AOI])
						{
							VisionSameDefect DefectInfo;
							DefectInfo.m_strDefectCode = AoiDefect.strDefect_code;
							DefectInfo.m_strPanelID = AoiDefect.strPanel_ID;
							DefectInfo.m_strChNum = dfsData.m_ChNum;
							DefectInfo.m_strFpcID = AoiDefect.strFpc_ID;

							if (!CheckSameAOIDefect(DefectInfo.m_strChNum, DefectInfo))
								theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionSameDefectAlarmStart, OffSet_0, TRUE);
						}
						theApp.m_pFTPLog->Debug(_T("[FTP] SameDefectMode check completed, defect count=%d"), 
							(int)DfsInfo.m_OpvDataList[Machine_AOI].size());
					}

					OpvInfo.m_Panel_Info.strTime = GetDateString4();
					OpvInfo.m_Panel_Info.strPanel_ID = DfsInfo.m_PanelDataBegin.strPanel_ID;
					OpvInfo.m_Panel_Info.strFpc_ID = strFpcID;
					OpvInfo.m_Panel_Info.strPanel_Width = theApp.m_strOpvImageWidth;
					OpvInfo.m_Panel_Info.strPanel_Hegiht = theApp.m_strOpvImageHeight;
					OpvInfo.m_Panel_Info.strPreGammaContactStatus = dfsData.m_PreGammaContactStatus;
					OpvInfo.m_Panel_Info.strModel_ID = dfsData.m_ModelID;
					OpvInfo.m_Panel_Info.strIndexNum = dfsData.m_IndexNum;
					OpvInfo.m_Panel_Info.strChNum = dfsData.m_ChNum;
					OpvInfo.m_Panel_Info.strVisionResult = DfsInfo.m_strVisionResult;
					OpvInfo.m_Panel_Info.strViewingResult = DfsInfo.m_strViewingResult;
					OpvInfo.m_Panel_Info.strTpResult = dfsData.m_TpResult;
					OpvInfo.m_Panel_Info.strLumitopResult = DfsInfo.m_strLumitopResult;
					theApp.m_pFTPLog->Debug(_T("[FTP] OpvInfo Panel_Info set: VisionResult=%s, ViewingResult=%s, LumitopResult=%s"),
						OpvInfo.m_Panel_Info.strVisionResult, OpvInfo.m_Panel_Info.strViewingResult, OpvInfo.m_Panel_Info.strLumitopResult);

					//>> Summery_data 220112 psh
					CString strFilePath;
					strTemp1 = DFS_SHARE_PATH + GetDateString2() + _T("\\") + strPanelID;
					CreateFolders(strTemp1);
					strFilePath = CStringSupport::FormatString(_T("%s\\%s.txt"), strTemp1, strPanelID);
					EZIni ini(strFilePath);
					ini[_T("Sumeery_data")][_T("AOI")] = dfsData.m_AOIInpsect;
					ini[_T("Sumeery_data")][_T("CONTACT")] = dfsData.m_Contact;
					ini[_T("Sumeery_data")][_T("PREGAMMA")] = dfsData.m_PreGamma;
					ini[_T("Sumeery_data")][_T("TP")] = dfsData.m_TpResult;
					ini[_T("Sumeery_data")][_T("LUMITOP")] = dfsData.m_Lumitop;
					theApp.m_pFTPLog->Debug(_T("[FTP] Summary data written: AOI=%s, TP=%s, LUMITOP=%s"), 
						dfsData.m_AOIInpsect, dfsData.m_TpResult, dfsData.m_Lumitop);

					if (theApp.m_iMachineType == SetAMT)
					{
						if (_ttoi(OpvInfo.m_Panel_Info.strPreGammaContactStatus) == m_dfsContactNG)
						{
							OpvInfo.m_Panel_Info.strDefect_Result = _T("N");
						}
						else if (_ttoi(OpvInfo.m_Panel_Info.strPreGammaContactStatus) == m_dfsPreGammaNG
							|| OpvInfo.m_Panel_Info.strVisionResult == _T("N")
							|| OpvInfo.m_Panel_Info.strViewingResult == _T("N"))
						{
							OpvInfo.m_Panel_Info.strDefect_Result = _T("N");
						}
						else
							OpvInfo.m_Panel_Info.strDefect_Result = _T("G");
					}
					else
					{
						if (_ttoi(OpvInfo.m_Panel_Info.strPreGammaContactStatus) == m_dfsContactNG)
						{
							OpvInfo.m_Panel_Info.strDefect_Result = _T("N");
						}
						else if (OpvInfo.m_Panel_Info.strVisionResult == _T("N") ||
							OpvInfo.m_Panel_Info.strViewingResult == _T("N") ||
							OpvInfo.m_Panel_Info.strLumitopResult == _T("N"))
						{
							OpvInfo.m_Panel_Info.strDefect_Result = _T("N");
						}
						else
							OpvInfo.m_Panel_Info.strDefect_Result = _T("G");
					}
					theApp.m_pFTPLog->Debug(_T("[FTP] Defect_Result=%s, PreGammaContactStatus=%s"), 
						OpvInfo.m_Panel_Info.strDefect_Result, OpvInfo.m_Panel_Info.strPreGammaContactStatus);

					if (_ttoi(OpvInfo.m_Panel_Info.strPreGammaContactStatus) == m_dfsPreGammaNG || _ttoi(OpvInfo.m_Panel_Info.strPreGammaContactStatus) == m_dfsContactNG || _ttoi(OpvInfo.m_Panel_Info.strTpResult) == m_dfsTpNG)
						DfsInfo.AddDefectCodeResult(strPanelID, _ttoi(OpvInfo.m_Panel_Info.strPreGammaContactStatus), _ttoi(OpvInfo.m_Panel_Info.strTpResult), Machine_AOI);

					int ii = 1;
					for (auto defect : DfsInfo.m_OpvDataList[Machine_AOI])
					{
						SDataDefectInfo defectInfo;
						defectInfo.strNo = CStringSupport::FormatString(_T("%d"), ii);
						defectInfo.strInspName = defect.strInspName;
						defectInfo.strDefect_Code = defect.strDefect_code;
						defectInfo.strDefect_Pattern = defect.strDefect_Ptn;
						defectInfo.strDefect_StartX = defect.strData_X1;
						defectInfo.strDefect_StartY = defect.strGate_Y1;
						defectInfo.strDefect_EndX = defect.strData_X2;
						defectInfo.strDefect_EndY = defect.strGate_Y2;
						defectInfo.strDefect_Grade = defect.strDefect_Grade;
						ii++;
						OpvInfo.m_Panel_Defect.push_back(defectInfo);
					}
					theApp.m_pFTPLog->Debug(_T("[FTP] Defect list built, count=%d"), 
						(int)DfsInfo.m_OpvDataList[Machine_AOI].size());

					strTemp1 = DFS_SHARE_OPV_PATH + GetDateString2() + _T("\\") + strPanelID;
					//strTemp1 = DFS_SHARE_OPV_PATH + GetDateString2() + strPanelID;
					CreateFolders(strTemp1);
					theApp.m_pFTPLog->Debug(_T("[FTP] OPV folder created: %s"), strTemp1);

					strSumImagePath = DFS_SHARE_PATH + GetDateString2() + _T("\\") + strPanelID + _T("\\AOI\\Image");
					strOpvSrc = strSumImagePath + _T("\\") + _T("AddsrcImageADD.jpg");
					strOpvDest = strTemp1 + _T("\\") + _T("AddsrcImageADD.jpg");
					theApp.m_pFTPLog->Debug(_T("[FTP] Image copy path: src=%s, dest=%s"), strOpvSrc, strOpvDest);

					strTemp1 = strTemp1 + _T("\\") + strPanelID + _T(".txt");
					OpvInfo.SetSaveFile(strTemp1);
					theApp.m_pFTPLog->Debug(_T("[FTP] OPV txt file path: %s"), strTemp1);

					//::CopyFile(strOpvSrc, strOpvDest, FALSE); //image 업로드

					// 优先从 D:\Data\Share\...\AOI\Image\AddsrcImageADD.jpg 获取（DFS线程已生成的）
					// 如果不存在，则从 AOI 检测服务器 D:\MEMS_DFS_Data\MainAOI\... 直接获取 MarkImg.jpg 并重命名
					BOOL bImageCopied = FALSE;
					if (FileExists(strOpvSrc))
					{
						if (::CopyFile(strOpvSrc, strOpvDest, FALSE))
						{
							theApp.m_pFTPLog->Info(_T("[FTP] AddsrcImageADD copied from Share: %s -> %s"), strOpvSrc, strOpvDest);
							bImageCopied = TRUE;
						}
						else
						{
							theApp.m_pFTPLog->Error(_T("[FTP] AddsrcImageADD CopyFile FAILED (Share): src=%s, dest=%s, error=%d"),
								strOpvSrc, strOpvDest, GetLastError());
						}
					}

					// Share路径没有图片，则直接从AOI检测服务器获取
					if (!bImageCopied && GetDBInterface().IsConnected())
					{
						CInspectionResultList results;
						if (GetDBInterface().QueryByBarcode(strPanelID, results) && !results.empty())
						{
							CInspectionResult& insp = results.front();
							CDefectInfoList defectList;
							CString strAoiImageDir;

							// 方式1：从缺陷表 ImagePath 提取目录
							if (GetDBInterface().QueryDefectsByParentGUID(insp.GUID, defectList))
							{
								for (const auto& defect : defectList)
								{
									if (!defect.ImagePath.IsEmpty())
									{
										CString strSrcPath = defect.ImagePath;
										// 判断绝对路径
										BOOL bAbs = (strSrcPath.GetLength() >= 3 && strSrcPath.GetAt(1) == ':' && strSrcPath.GetAt(2) == '\\');
										if (!bAbs)
											strSrcPath = DEFAULT_MAIN_AOI_IMAGE_ROOT + defect.ImagePath;
										int nSlash = max(strSrcPath.ReverseFind('\\'), strSrcPath.ReverseFind('/'));
										if (nSlash >= 0)
											strAoiImageDir = strSrcPath.Left(nSlash);
										if (!strAoiImageDir.IsEmpty() && PathIsDirectory(strAoiImageDir))
											break;
										strAoiImageDir.Empty();
									}
								}
							}

							// 方式2：从数据库字段构建路径
							if (strAoiImageDir.IsEmpty() && !insp.LocalIP.IsEmpty() && insp.PlatformID >= 0)
							{
								CString strIdx, strDate;
								strIdx.Format(_T("%d"), insp.PlatformID + 1);
								strDate = insp.StartTime.Format(_T("%Y-%m-%d"));
								strAoiImageDir.Format(_T("%sMainAOI\\%s\\%s\\%s\\%s"),
									DEFAULT_MAIN_AOI_IMAGE_ROOT, (LPCTSTR)insp.LocalIP,
									(LPCTSTR)strIdx, (LPCTSTR)strDate, (LPCTSTR)strPanelID);
								theApp.m_pFTPLog->Debug(_T("[FTP] Built AoiImageDir from DB: %s"), strAoiImageDir);
							}

							// 从 AOI 服务器复制 MarkImg.jpg → AddsrcImageADD.jpg
							if (!strAoiImageDir.IsEmpty())
							{
								CString strMarkSrc = strAoiImageDir + _T("\\MarkImg.jpg");
								theApp.m_pFTPLog->Debug(_T("[FTP] Trying MarkImg from AOI server: %s"), strMarkSrc);
								if (FileExists(strMarkSrc))
								{
									if (::CopyFile(strMarkSrc, strOpvDest, FALSE))
									{
										theApp.m_pFTPLog->Info(_T("[FTP] AddsrcImageADD copied from AOI server: %s -> %s"), strMarkSrc, strOpvDest);
										bImageCopied = TRUE;
									}
									else
									{
										theApp.m_pFTPLog->Error(_T("[FTP] AddsrcImageADD CopyFile FAILED (AOI server): src=%s, dest=%s, error=%d"),
											strMarkSrc, strOpvDest, GetLastError());
									}
								}
								else
								{
									theApp.m_pFTPLog->Info(_T("[FTP] MarkImg.jpg not found on AOI server: %s"), strMarkSrc);
								}
							}
						}
						else
						{
							theApp.m_pFTPLog->Info(_T("[FTP] QueryByBarcode failed for OPV image: PanelID=%s"), strPanelID);
						}
					}

					if (!bImageCopied)
					{
						theApp.m_pFTPLog->Error(_T("[FTP] AddsrcImageADD copy SKIPPED - image not available: PanelID=%s"), strPanelID);
					}
					//theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_VisionSameDefectAlarmStart, OffSet_0, FALSE);
				}

				Delay(10, TRUE);

				m_csLock.Lock();
				m_transferFileList.pop();
				m_csLock.Unlock();
			}
		}
	}
}

UINT CDFSClient::FtpUploadTask(LPVOID pParam)
{
	//>> 161201 jwan
	CDFSClient* pThis = reinterpret_cast<CDFSClient*>(pParam);
	_ASSERTE(pThis != NULL);
	pThis->RunFtpUploadThread();
	return 1L;
}


BOOL CDFSClient::CreateTask() {
	//>> 161201 jwan
	BOOL bRet = TRUE;
	m_pThreadFtpUpload = ::AfxBeginThread(FtpUploadTask, this, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
	if (!m_pThreadFtpUpload)
		bRet = FALSE;
	m_pThreadFtpUpload->m_bAutoDelete = FALSE;	/// ¾²·¹µå Á¾·á½Ã WaitForSingleObject Àû¿ëÀ§ÇØ...
	m_pThreadFtpUpload->ResumeThread();
	return bRet;
}

void CDFSClient::CloseTask()
{
	//>> 161201 jwan
	if (m_pThreadFtpUpload != NULL)
	{
		SetEvent(m_hQuit);
		Delay(100, TRUE);
		if (::WaitForSingleObject(m_pThreadFtpUpload->m_hThread, 6000) == WAIT_TIMEOUT)
		{
			SetEvent(m_hQuit);
			Delay(100, TRUE);
			if (::WaitForSingleObject(m_pThreadFtpUpload->m_hThread, 10000) == WAIT_TIMEOUT) {
				::TerminateThread(m_pThreadFtpUpload->m_hThread, 1L);
				TRACE(_T("Terminate FTP Upload Thread\n"));
			}
		}
		delete m_pThreadFtpUpload;
		m_pThreadFtpUpload = NULL;
	}
	if (m_hQuit)
	{
		CloseHandle(m_hQuit);
		m_hQuit = NULL;
	}
}
#endif
BOOL CDFSClient::CheckSameAOIDefect(CString strChNum, VisionSameDefect defectList)
{
	BOOL bOverDefectCount(TRUE);
	map<CString, VisionSameDefect>::iterator iter;

	CString strKey = CStringSupport::FormatString(_T("%s^%s"), defectList.m_strChNum, defectList.m_strDefectCode);
	CString strCode, strCh;
	CStringArray responseTokens;
	CStringSupport::GetTokenArray(strKey, _T('^'), responseTokens);

	if (responseTokens.GetSize() < 2)
	{
		theApp.m_pFTPLog->Warn(_T("[SameDefectCheck] strKey=%s TokenCount=%d 不足2个，跳过"),
			strKey, responseTokens.GetSize());
		return FALSE;
	}
	strCh = responseTokens[0];
	strCode = responseTokens[1];

	if (!strCode.IsEmpty())
	{
		theApp.m_iTotalCompareCount++;

		iter = m_mapSameDefect.find(strKey);
		if (iter != m_mapSameDefect.end())
		{
			if (theApp.m_bSameDefectChCheckMode == TRUE)
			{
				if (!iter->second.m_strChNum.CompareNoCase(strCh) && !theApp.m_strSameDefectCode.CompareNoCase(strCode) &&
					iter->second.m_strPanelID.CompareNoCase(defectList.m_strPanelID))
				{
					iter->second.m_iSameDefectCount++;
					m_mapSameDefect.insert(make_pair(strKey, iter->second));

					if (iter->second.m_iSameDefectCount >= _ttoi(theApp.m_strSameDefectAlarmMaxCount))
					{
						CString strMsg = CStringSupport::FormatString(_T("Panel [%s][%s] Same NG Over %s Count, DefectCode : %s, ChNum : %s"),
							iter->second.m_strPanelID, iter->second.m_strFpcID, theApp.m_strSameDefectAlarmMaxCount, iter->second.m_strDefectCode, iter->second.m_strChNum);

						theApp.m_pTraceLog->Debug(strMsg);

						theApp.m_pMsgBoxAlarm->WaitShowHide(SW_SHOW, strMsg);
						//theApp.getMsgBox(MS_OK, strMsg, strMsg, strMsg);
						m_mapSameDefect.clear();
						theApp.m_iTotalCompareCount = 0;
						bOverDefectCount = FALSE;
					}
				}
			}
			else
			{
				if (!theApp.m_strSameDefectCode.CompareNoCase(strCode) && iter->second.m_strPanelID.CompareNoCase(defectList.m_strPanelID))
				{
					iter->second.m_iSameDefectCount++;
					m_mapSameDefect.insert(make_pair(strKey, iter->second));

					if (iter->second.m_iSameDefectCount >= _ttoi(theApp.m_strSameDefectAlarmMaxCount))
					{
						CString strMsg = CStringSupport::FormatString(_T("Panel [%s][%s] Same NG Over %s Count, DefectCode : %s"),
							iter->second.m_strPanelID, iter->second.m_strFpcID, theApp.m_strSameDefectAlarmMaxCount, iter->second.m_strDefectCode);
						theApp.m_pTraceLog->Debug(strMsg);
						theApp.m_pMsgBoxAlarm->WaitShowHide(SW_SHOW, strMsg);
						//theApp.getMsgBox(MS_OK, strMsg, strMsg, strMsg);
						m_mapSameDefect.clear();
						theApp.m_iTotalCompareCount = 0;
						bOverDefectCount = FALSE;
					}
				}
			}
		}
		else
		{
			if (!theApp.m_strSameDefectCode.CompareNoCase(strCode))
			{
				defectList.m_iSameDefectCount = 1;
				m_mapSameDefect.insert(make_pair(strKey, defectList));
			}
		}

		if (theApp.m_iTotalCompareCount >= _ttoi(theApp.m_strSameDefectMaxCount))
		{
			theApp.m_iTotalCompareCount = 0;
			m_mapSameDefect.clear();
		}
	}

	return bOverDefectCount;
}

BOOL CDFSClient::DfsIDXFileCreate(CString strEqpName, CString *strIdxFileName)
{
	CString strDfsFilePath;
	CString strString;
	CStdioFile File;

	strDfsFilePath = DFS_SHARE_PATH + GetDateString2() + _T("\\") + GetDateString2() + _T("_") + strEqpName + _T(".csv");

	/*if (File.Open(strDfsFilePath, CFile::modeCreate | CFile::modeWrite) == FALSE)
		return FALSE;

	File.Close();

	FILE* fOutFile1;
	char* fileName = StringToChar(strDfsFilePath);
	fOutFile1 = fopen(fileName, "wt");

	strString.Format(_T("Date_Time,Path\n"));
	fputs(CStringToUtf8(strString), fOutFile1);

	for (CString IndexValue : m_vecIndexValue)
	{
		strString.Format(_T("%s,%s\n"), GetDateString6(), IndexValue);
		fputs(CStringToUtf8(strString), fOutFile1);
	}

	fclose(fOutFile1);*/
	BOOL bOpen = FALSE;
	if (!File.Open(strDfsFilePath, CFile::modeReadWrite | CFile::shareDenyNone))
	{
		if (File.Open(strDfsFilePath, CFile::modeCreate | CFile::modeWrite))
		{
			bOpen = TRUE;

			strString.Format(_T("Date_Time,Path\n"));
			File.WriteString(strString);
		}

	}
	else bOpen = TRUE;

	if (bOpen){
		File.SeekToEnd();
		for (CString IndexValue : m_vecIndexValue)
		{
			strString.Format(_T("%s,%s\n"), GetDateString6(), IndexValue);
			File.WriteString(strString);
		}
		File.Close();
	}

	*strIdxFileName = strDfsFilePath;

	return TRUE;
}

UINT CDFSClient::DfsDeleatTask(LPVOID pParam)
{
	CDFSClient* pThis = reinterpret_cast<CDFSClient*>(pParam);
	_ASSERTE(pThis != NULL);
	pThis->RunDfsDeleatThread();
	return 1L;
}

UINT CDFSClient::DfsUploadTask(LPVOID pParam)
{
	// 中文说明：
	//   **功能：** 线程包装函数，供 `AfxBeginThread` 调用。将 `pParam` 转回 `CDFSClient*`，
	//             并调用成员函数 `RunDfsUploadThread` 执行实际的 DFS 上传循环。
	//   **说明：** 之所以使用静态/全局样式的回调，是为了兼容 MFC 线程创建接口。
	CDFSClient* pThis = reinterpret_cast<CDFSClient*>(pParam);
	_ASSERTE(pThis != NULL);
	pThis->RunDfsUploadThread();
	return 1L;
}

BOOL CDFSClient::CreateDfsTask() 
{
	// 中文说明：
	//   **功能：** 创建并启动 DFS 相关后台线程，包括：
	//     - `m_pThreadDfsUpload`：负责 DFS 文件整理与共享文件夹上传（`RunDfsUploadThread`）。
	//     - `m_pThreadDfsDeleat`：负责按日期删除旧 DFS 目录（`RunDfsDeleatThread`）。
	//   **使用时机：** 一般在设备程序初始化阶段（如设备启动时）调用一次，之后通过
	//                 `DfsAddTransferFile` 不断投递上传任务即可。
	BOOL bRet = TRUE;

	// 初始化退出事件
	m_hDfsUploadQuit = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_hDfsDeleatQuit = CreateEvent(NULL, TRUE, FALSE, NULL);

	m_pThreadDfsUpload = ::AfxBeginThread(DfsUploadTask, this, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
	if (!m_pThreadDfsUpload)
		bRet = FALSE;
	m_pThreadDfsUpload->m_bAutoDelete = FALSE;	/// ¾²·¹µå Á¾·á½Ã WaitForSingleObject Àû¿ëÀ§ÇØ...
	m_pThreadDfsUpload->ResumeThread();




	m_pThreadDfsDeleat = ::AfxBeginThread(DfsDeleatTask, this, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
	if (!m_pThreadDfsDeleat)
		bRet = FALSE;
	m_pThreadDfsDeleat->m_bAutoDelete = FALSE;	/// ¾²·¹µå Á¾·á½Ã WaitForSingleObject Àû¿ëÀ§ÇØ...
	m_pThreadDfsDeleat->ResumeThread();

	return bRet;
}

void CDFSClient::CloseDfsTask()
{
	if (m_pThreadDfsUpload != NULL)
	{
		SetEvent(m_hDfsUploadQuit);
		Delay(100, TRUE);
		if (::WaitForSingleObject(m_pThreadDfsUpload->m_hThread, 6000) == WAIT_TIMEOUT)
		{
			SetEvent(m_hDfsUploadQuit);
			Delay(100, TRUE);
			if (::WaitForSingleObject(m_pThreadDfsUpload->m_hThread, 10000) == WAIT_TIMEOUT) {
				::TerminateThread(m_pThreadDfsUpload->m_hThread, 1L);
				TRACE(_T("Terminate DFS Upload Thread\n"));
			}
		}
		delete m_pThreadDfsUpload;
		m_pThreadDfsUpload = NULL;
	}
	if (m_hDfsUploadQuit)
	{
		CloseHandle(m_hDfsUploadQuit);
		m_hDfsUploadQuit = NULL;
	}

	if (m_pThreadDfsDeleat != NULL)
	{
		SetEvent(m_hDfsDeleatQuit);
		Delay(100, TRUE);
		if (::WaitForSingleObject(m_pThreadDfsDeleat->m_hThread, 6000) == WAIT_TIMEOUT)
		{
			SetEvent(m_hDfsDeleatQuit);
			Delay(100, TRUE);
			if (::WaitForSingleObject(m_pThreadDfsDeleat->m_hThread, 10000) == WAIT_TIMEOUT) {
				::TerminateThread(m_pThreadDfsDeleat->m_hThread, 1L);
				TRACE(_T("Terminate DFS Upload Thread\n"));
			}
		}
		delete m_pThreadDfsDeleat;
		m_pThreadDfsDeleat = NULL;
	}
	if (m_hDfsDeleatQuit)
	{
		CloseHandle(m_hDfsDeleatQuit);
		m_hDfsDeleatQuit = NULL;
	}



	
}