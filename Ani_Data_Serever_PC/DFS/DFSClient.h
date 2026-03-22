// FTPClient.h: interface for the CDFSClient class.
//
//////////////////////////////////////////////////////////////////////
//
// 中文说明：
//   `CDFSClient` 为设备与厂内 DFS/FTP 服务器之间的数据上传客户端类。
//   主要通过 MFC 的 `CInternetSession` / `CFtpConnection` 完成文件上传、
//   目录创建与维护，并与 `CDFSSession`、`CDFSInfo` 结构体配合生成符合
//   CSOT 规范的 DFS 报文文件。
//
//   - 主要职责：
//       * 维护与 DFS/FTP 的连接、重试、超时等（`Connect` / `Disconnect`）
//       * 将 AOI/OPV 等检测结果文件加入上传队列并在独立线程中传输
//         （`CreateDfsTask`、`DfsAddTransferFile`、`DfsUploadTask` 等）
//       * 生成 DFS Index 文件、目标路径字符串等
//       * 在 _SYSTEM_AMTAFT_ 条件下，额外支持通用 FTP 上传线程
//
//   - 线程与队列：
//       * `m_DfsUploadtransferFileList` / `m_transferFileList`：上传任务队列
//       * `RunDfsUploadThread` / `RunFtpUploadThread`：上传线程入口函数
//       * 使用 `CCriticalSection` 保证队列在多线程环境下访问安全
//
//////////////////////////////////////////////////////////////////////

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <afxinet.h>
#include "FtpSite.h"
#include "DFSSession.h"
#include <queue>
using namespace std;

//#define BUF_SIZE 4096
#define BUF_SIZE 20480
const CString IP_ADDRESS_MCC = _T("172.18.3.110");
//const CString IP_ADDRESS_MCC = _T("127.0.0.1");


class CDFSClient
{
public:
	CDFSClient();
	virtual ~CDFSClient();

	BOOL bActive;
	CDFSSession *m_pDFSSession;
	CFtpConnection *m_pFtpConnection;
	CFtpSite m_ftpSite;

	CString m_strCurrentDirectory;
	CString m_strLastDirectory;
	CString m_strRootDirectory;

	CString m_strOldName;
	CString m_strServerName;
	BOOL    m_bConnectState;

	void Connect(LPCTSTR lpszSiteName, LPCTSTR lpszUserName, LPCTSTR lpszPassword);
	void UploadFile(CString &source, CString &dest, BOOL &bSend);

	BOOL CheckSameAOIDefect(CString strChNum, VisionSameDefect defectList);
	map<CString, VisionSameDefect> m_mapSameDefect;

	queue<DfsDataValue> m_DfsUploadtransferFileList;

	BOOL CreateDfsTask();
	void CloseDfsTask();
	void DfsAddTransferFile(DfsDataValue strTransferFile);
	static UINT DfsUploadTask(LPVOID pParam);
	static UINT DfsDeleatTask(LPVOID pParam);
	void RunDfsUploadThread();
	void RunDfsDeleatThread();
	void DeleteFolderAndFile(LPCTSTR szFolderPath,BOOL flag);
	BOOL m_DeleteStart;

#if _SYSTEM_AMTAFT_
	queue<DfsDataValue> m_transferFileList;

	BOOL CreateTask();
	void CloseTask();
	void AddTransferFile(DfsDataValue strTransferFile);
	static UINT FtpUploadTask(LPVOID pParam);
	void RunFtpUploadThread();
#endif

	BOOL CheckDirectory(CString strPath); //140607 JSLee
	BOOL WaitWithMessageLoop(HANDLE hEvent, int nTimeout);
	void Disconnect();
	void SetDfsFilePath(CString *strPath, CString strNextPath){ *strPath = *strPath + _T("/") + strNextPath; };
	void SetFilePath(CString *strPath, CString strNextPath){ *strPath = *strPath + _T("\\") + strNextPath; };

	BOOL DfsIDXFileCreate(CString strEqpName, CString *strIdxFileName);

	BOOL TransferFile(LPCTSTR fileName);

	DWORD m_dwFileLength;
	CFile m_File;
	CString m_strResult;
	TCHAR m_szStatus[1024];
	CInternetFile* m_pInternetFile;
	int		m_nRetries;
	int		m_nRetryDelay;

	CString m_strLocalName;
	CString m_strRemoteName;
	CString m_strPassword;
	CString m_strUserName;
	DWORD m_nConnectionTimeout;
	DWORD m_dwTransferType;
	int		m_nPort;
	int		m_bUsePASVMode;
	CString m_strAddress;
	CString m_strDescription;
	CString m_strLocalPath;
	CString m_strLogin;
	CString m_strName;
	CString m_strRemotePath;
	BOOL m_bComviewFlag;
	CString m_bComViewLoginId;
	CString m_bComViewLoginPassWord;

	std::vector<CString> m_vecIndexValue;

	CString strPanelID, strModelName, strYearMonth, strSerialNo, strGlassNo, strPanelNo, strEQUType, strEQUImg, strEQCode, strLine;

	CString strOpvDest, strOpvSrc;
	CString strDest, strSrc;

	HANDLE m_hQuit;
	CWinThread *m_pThreadFtpUpload;
	CCriticalSection m_csLock;

	HANDLE m_hDfsUploadQuit;
	HANDLE m_hDfsDeleatQuit;
	CWinThread *m_pThreadDfsUpload;
	CWinThread *m_pThreadDfsDeleat;
	CCriticalSection m_csDfsUploadLock;
};
