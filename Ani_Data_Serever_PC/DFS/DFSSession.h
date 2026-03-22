// DFSSession.h: interface for the CMyInternetSession class.
//
//////////////////////////////////////////////////////////////////////
//
// 中文说明：
//   `CDFSSession` 继承自 MFC 的 `CInternetSession`，是 DFS/FTP 访问使用的
//   专用会话类。主要用于统一管理网络会话状态，并在底层网络状态变化时向
//   上层（通常为 `CDFSClient`）回调当前进度或错误信息。
//
//   - 主要职责：
//       * 在 `OnStatusCallback` 中接收 WinInet 的状态通知（连接、发送、
//         接收、错误等），并根据需要更新 UI 或日志
//       * 通过 `GetErrorString` 将错误码转换为可读字符串，便于日志与调试
//       * 保存主窗口句柄 `m_pMainWnd`，在需要时向界面发送消息
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_DFSSESSION_H__254FC5C5_3B0E_11D6_AB38_00D0B70C3D79__INCLUDED_)
#define AFX_DFSSESSION_H__254FC5C5_3B0E_11D6_AB38_00D0B70C3D79__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <afxinet.h>
class CDFSSession : public CInternetSession
{
public:
	CDFSSession(LPCTSTR pstrAgent = NULL, DWORD dwContext = 1, DWORD dwAccessType = INTERNET_OPEN_TYPE_PRECONFIG, LPCTSTR pstrProxyName = NULL, LPCTSTR pstrProxyBypass = NULL, DWORD dwFlags = 0 ) 
		: CInternetSession(pstrAgent, dwContext, dwAccessType, pstrProxyName, pstrProxyBypass, dwFlags) 
		{ 	
			m_pMainWnd = NULL;	
		};

// Attributes
public:
	HWND m_pMainWnd;  // pointer to parent window
// Operations
public:
	CString GetErrorString(DWORD dwErrorCode);

	virtual void OnStatusCallback(DWORD dwContext, DWORD dwInternetStatus, LPVOID lpvStatusInfomration, DWORD dwStatusInformationLen);
};



#endif // !defined(AFX_DFSSESSION_H__254FC5C5_3B0E_11D6_AB38_00D0B70C3D79__INCLUDED_)

/*--- END OF DFSSession.h ---*/
