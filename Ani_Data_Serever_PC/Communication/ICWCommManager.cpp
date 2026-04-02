///////////////////////////////////////////////////////////////////////////////
// FILE : ICWCommManager.cpp
// ICW通信管理器实现
///////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "ICWCommManager.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

///////////////////////////////////////////////////////////////////////////////
// 构造/析构
///////////////////////////////////////////////////////////////////////////////
CICWCommManager::CICWCommManager()
    : m_bRunning(FALSE)
    , m_bClientMode(FALSE)
    , m_bConnected(FALSE)
    , m_hParentWnd(NULL)
    , m_cbStart(nullptr)
    , m_cbSnapFN(nullptr)
    , m_cbFinishFN(nullptr)
    , m_cbHeartBeat(nullptr)
    , m_cbGetVersion(nullptr)
    , m_bAutoReconnectThreadRunning(FALSE)
    , m_nReconnectAttempts(0)
    , m_hReconnectThread(NULL)
    , m_hReconnectQuitEvent(NULL)
{
    InitializeCriticalSection(&m_csRecv);
    InitializeCriticalSection(&m_csSend);
    InitializeCriticalSection(&m_csReconnect);
    m_hReconnectQuitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
}

CICWCommManager::~CICWCommManager()
{
    StopAutoReconnect();  // 先停止自动重连
    Disconnect();         // 断开客户端连接
    StopServer();         // 再停止服务器
    DeleteCriticalSection(&m_csRecv);
    DeleteCriticalSection(&m_csSend);
    DeleteCriticalSection(&m_csReconnect);
    if (m_hReconnectQuitEvent != NULL)
    {
        CloseHandle(m_hReconnectQuitEvent);
        m_hReconnectQuitEvent = NULL;
    }
}

///////////////////////////////////////////////////////////////////////////////
// 启动TCP服务器
///////////////////////////////////////////////////////////////////////////////
BOOL CICWCommManager::StartServer(LPCTSTR strPort)
{
    if (m_bRunning)
    {
        TRACE(_T("ICWCommManager: Server already running\n"));
        return TRUE;
    }
    
    // 创建TCP服务器Socket
    SetServerState(TRUE);
    
    if (!CreateSocket(strPort, AF_INET, SOCK_STREAM, 0))
    {
        TRACE(_T("ICWCommManager: Failed to create server socket on port %s\n"), strPort);
        return FALSE;
    }
    
    // 启动监听线程
    if (!WatchComm())
    {
        TRACE(_T("ICWCommManager: Failed to start watch thread\n"));
        CloseComm();
        return FALSE;
    }
    
    m_bRunning = TRUE;
    m_strRecvBuffer.Empty();
    
    TRACE(_T("ICWCommManager: Server started on port %s\n"), strPort);
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// 停止服务器
///////////////////////////////////////////////////////////////////////////////
void CICWCommManager::StopServer()
{
    if (!m_bRunning)
        return;
    
    m_bRunning = FALSE;
    StopComm();
    CloseComm();
    
    EnterCriticalSection(&m_csRecv);
    m_strRecvBuffer.Empty();
    LeaveCriticalSection(&m_csRecv);
    
    TRACE(_T("ICWCommManager: Server stopped\n"));
}

///////////////////////////////////////////////////////////////////////////////
// 作为TCP客户端连接点灯检软件
///////////////////////////////////////////////////////////////////////////////
BOOL CICWCommManager::ConnectToServer(LPCTSTR strIP, LPCTSTR strPort)
{
    if (m_bConnected)
    {
        TRACE(_T("ICWCommManager: Already connected to %s:%s\n"), strIP, strPort);
        return TRUE;
    }
    
    // 保存服务器信息
    m_strServerIP = strIP;
    m_strServerPort = strPort;
    m_bClientMode = TRUE;
    
    // 启用 Melsec 模拟模式，确保 Socket 连接正常
    m_bMelsecSimulaion = TRUE;
    
    // 创建客户端Socket并连接
    // ConnectTo(strDestination, strServiceName, nFamily, nType)
    // - strDestination: 服务器IP地址
    // - strServiceName: 服务器端口
    // - nFamily: AF_INET
    // - nType: SOCK_STREAM (TCP)
    if (!ConnectTo(strIP, strPort, AF_INET, SOCK_STREAM))
    {
        TRACE(_T("ICWCommManager: Failed to connect to %s:%s\n"), strIP, strPort);
        m_bConnected = FALSE;
        
        // 连接失败时也启动自动重连
        TRACE(_T("ICWCommManager: 连接失败，启动自动重连机制\n"));
        StartAutoReconnect();
        return FALSE;
    }
    
    // 启动通信监听
    if (!WatchComm())
    {
        TRACE(_T("ICWCommManager: Failed to start communication thread\n"));
        ShutdownConnection((SOCKET)m_hComm);
        m_hComm = NULL;
        m_bConnected = FALSE;
        return FALSE;
    }
    
    m_bConnected = TRUE;
    m_strRecvBuffer.Empty();
    
    TRACE(_T("ICWCommManager: Connected to server %s:%s\n"), strIP, strPort);
    
    // 通知父窗口
    if (m_hParentWnd)
    {
        ::PostMessage(m_hParentWnd, WM_ICW_CONNECTED, 0, 0);
    }
    
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// 断开连接
///////////////////////////////////////////////////////////////////////////////
void CICWCommManager::Disconnect()
{
    if (!m_bConnected)
        return;
    
    m_bConnected = FALSE;
    StopComm();
    CloseComm();
    
    EnterCriticalSection(&m_csRecv);
    m_strRecvBuffer.Empty();
    LeaveCriticalSection(&m_csRecv);
    
    TRACE(_T("ICWCommManager: Disconnected from server\n"));
    
    // 通知父窗口
    if (m_hParentWnd)
    {
        ::PostMessage(m_hParentWnd, WM_ICW_DISCONNECTED, 0, 0);
    }
}

///////////////////////////////////////////////////////////////////////////////
// 发送采图完成通知
///////////////////////////////////////////////////////////////////////////////
BOOL CICWCommManager::SendSnapFN()
{
    return SendMessage(CICWProtocol::MakeSnapFNMessage());
}

///////////////////////////////////////////////////////////////////////////////
// 发送检测结束信息
///////////////////////////////////////////////////////////////////////////////
BOOL CICWCommManager::SendFinishInfo(const ICW_FinishInfo& info)
{
    CString strJson = CICWProtocol::SerializeFinishInfo(info);
    return SendMessage(strJson);
}

BOOL CICWCommManager::SendFinishInfoAuto(const ICW_FinishInfo& info)
{
    if (m_lastStartProtocol == StartProtocol::LegacyStartDollar)
    {
        TRACE(_T("ICWCommManager: SendFinishInfoAuto -> LEGACY FN$...@\n"));
        CString strFn = CICWProtocol::SerializeLegacyFinishFN(info);
        TRACE(_T("ICWCommManager: Legacy FN payload=%s\n"), strFn);
        return SendMessage(strFn);
    }
    // 默认走JSON(V1.1)
    TRACE(_T("ICWCommManager: SendFinishInfoAuto -> JSON V1.1 FinishInfo@\n"));
    return SendFinishInfo(info);
}

///////////////////////////////////////////////////////////////////////////////
// 发送版本信息
///////////////////////////////////////////////////////////////////////////////
BOOL CICWCommManager::SendVersionInfo(const ICW_VersionInfo& info)
{
    CString strJson = CICWProtocol::SerializeVersionInfo(info);
    return SendMessage(strJson);
}

///////////////////////////////////////////////////////////////////////////////
// 发送原始字符串消息
///////////////////////////////////////////////////////////////////////////////
BOOL CICWCommManager::SendMessage(const CString& strMsg)
{
    if (!IsOpen())
    {
        TRACE(_T("ICWCommManager: Socket not open\n"));
        return FALSE;
    }
    
    EnterCriticalSection(&m_csSend);
    
    CString strSend = strMsg;
    // 确保消息以@结尾
    if (!strSend.IsEmpty() && strSend.Right(1) != _T("@"))
    {
        strSend += _T("@");
    }

    // 发送前日志（避免过长日志影响性能，这里只截断打印）
    {
        CString preview = strSend;
        const int kMaxPreview = 512;
        if (preview.GetLength() > kMaxPreview)
        {
            preview = preview.Left(kMaxPreview) + _T("...<truncated>");
        }
        TRACE(_T("ICWCommManager: Sending (%d chars): %s\n"), strSend.GetLength(), preview);
    }
    
    // 转换为ANSI发送 (ICW可能期望ANSI编码)
    CStringA strSendA(strSend);
    DWORD dwWritten = WriteComm((LPBYTE)(LPCSTR)strSendA, strSendA.GetLength(), 5000);
    
    LeaveCriticalSection(&m_csSend);
    
    BOOL bSuccess = (dwWritten == (DWORD)strSendA.GetLength());
    if (bSuccess)
    {
        TRACE(_T("ICWCommManager: Sent message: %s\n"), strSend);
    }
    else
    {
        TRACE(_T("ICWCommManager: Failed to send message\n"));
    }
    
    return bSuccess;
}

///////////////////////////////////////////////////////////////////////////////
// 数据接收处理 (被CSocketComm调用)
///////////////////////////////////////////////////////////////////////////////
void CICWCommManager::OnDataReceived(const LPBYTE lpBuffer, DWORD dwCount)
{
    if (dwCount == 0 || lpBuffer == NULL)
        return;
    
    EnterCriticalSection(&m_csRecv);
    
    // 将接收的数据追加到缓冲区
    CStringA strDataA((LPCSTR)lpBuffer, dwCount);
    CString strData(strDataA);
    m_strRecvBuffer += strData;
    
    // 原始接收日志（用于现场确认ICW实际协议：JSON vs Start$）
    {
        CString preview = strData;
        const int kMaxPreview = 512;
        if (preview.GetLength() > kMaxPreview)
        {
            preview = preview.Left(kMaxPreview) + _T("...<truncated>");
        }
        TRACE(_T("ICWCommManager: Received %d bytes: %s\n"), dwCount, preview);
    }
    
    // 分割消息
    std::vector<CString> messages;
    CString remaining;
    CICWProtocol::SplitMessages(m_strRecvBuffer, messages, remaining);
    m_strRecvBuffer = remaining;
    
    LeaveCriticalSection(&m_csRecv);
    
    // 处理每个完整消息
    for (size_t i = 0; i < messages.size(); i++)
    {
        TRACE(_T("ICWCommManager: Complete message[%d]=%s\n"), (int)i, messages[i]);
        ProcessMessage(messages[i]);
    }
}

///////////////////////////////////////////////////////////////////////////////
// 事件处理
///////////////////////////////////////////////////////////////////////////////
void CICWCommManager::OnEvent(UINT uEvent, LPVOID lpvData)
{
    switch (uEvent)
    {
    case EVT_CONSUCCESS:
        TRACE(_T("ICWCommManager: Client connected\n"));
        if (m_hParentWnd)
        {
            ::PostMessage(m_hParentWnd, WM_ICW_CONNECTED, 0, 0);
        }
        break;
        
    case EVT_CONDROP:
        TRACE(_T("ICWCommManager: Client disconnected\n"));
        m_bConnected = FALSE;
        if (m_hParentWnd)
        {
            ::PostMessage(m_hParentWnd, WM_ICW_DISCONNECTED, 0, 0);
        }
        // 清空接收缓冲区
        EnterCriticalSection(&m_csRecv);
        m_strRecvBuffer.Empty();
        LeaveCriticalSection(&m_csRecv);
        
        // 自动重连（仅在客户端模式下）
        if (m_bClientMode && !m_strServerIP.IsEmpty() && !m_strServerPort.IsEmpty())
        {
            TRACE(_T("ICWCommManager: 客户端模式断线，启动自动重连\n"));
            StartAutoReconnect();
        }
        break;
        
    case EVT_CONFAILURE:
        TRACE(_T("ICWCommManager: Connection failure\n"));
        if (m_hParentWnd)
        {
            ::PostMessage(m_hParentWnd, WM_ICW_ERROR, 0, 0);
        }
        break;
    }
}

///////////////////////////////////////////////////////////////////////////////
// 处理接收到的完整消息
///////////////////////////////////////////////////////////////////////////////
void CICWCommManager::ProcessMessage(const CString& strMsg)
{
    CString strTrimmed = strMsg;
    strTrimmed.Trim();
    
    if (strTrimmed.IsEmpty())
        return;
    
    TRACE(_T("ICWCommManager: Processing message: %s\n"), strTrimmed);
    
    // 心跳消息
    if (CICWProtocol::IsHeartBeat(strTrimmed))
    {
        TRACE(_T("ICWCommManager: HeartBeat received\n"));
        // 发送心跳响应
        SendMessage(CICWProtocol::MakeHeartBeatResponse());
        if (m_cbHeartBeat)
        {
            m_cbHeartBeat();
        }
        return;
    }
    
    // 版本查询
    if (CICWProtocol::IsGetVersion(strTrimmed))
    {
        TRACE(_T("ICWCommManager: GetVersion received\n"));
        if (m_cbGetVersion)
        {
            ICW_VersionInfo versionInfo = m_cbGetVersion();
            SendVersionInfo(versionInfo);
        }
        return;
    }
    
    // SnapFN消息 (如果ICW也发送此消息)
    if (CICWProtocol::IsSnapFN(strTrimmed))
    {
        TRACE(_T("ICWCommManager: SnapFN received\n"));
        if (m_cbSnapFN)
        {
            m_cbSnapFN();
        }
        return;
    }

    // 旧版检测结束报文: FN$xxxx...@ (检测结果)
    if (CICWProtocol::IsLegacyFinishFN(strTrimmed))
    {
        ICW_LegacyFinishInfo finishInfo;
        if (CICWProtocol::ParseLegacyFinishFN(strTrimmed, finishInfo))
        {
            TRACE(_T("ICWCommManager: FN$ received - %d results\n"), (int)finishInfo.Results.size());
            if (m_cbFinishFN)
            {
                m_cbFinishFN(finishInfo);
            }
            if (m_hParentWnd)
            {
                ::PostMessage(m_hParentWnd, WM_ICW_START_INFO, 0, 0);
            }
        }
        else
        {
            TRACE(_T("ICWCommManager: FN$ detected but parse failed: %s\n"), strTrimmed);
        }
        return;
    }

    // 旧版协议: Start$... (开始检测)
    if (CICWProtocol::IsLegacyStart(strTrimmed))
    {
        ICW_StartInfo startInfo;
        if (CICWProtocol::ParseLegacyStartInfo(strTrimmed, startInfo))
        {
            m_lastStartProtocol = StartProtocol::LegacyStartDollar;

            TRACE(_T("ICWCommManager: Detected LEGACY protocol (Start$). Parsed - JigNumber: %d, Products: %d\n"),
                startInfo.JigNumber, (int)startInfo.Products.size());

            // 旧协议要求主检回 Running@ 表示进入检测流程
            TRACE(_T("ICWCommManager: Legacy requires Running@ ack -> sending now\n"));
            SendMessage(CICWProtocol::MakeLegacyRunningMessage());

            if (m_cbStart)
            {
                m_cbStart(startInfo);
            }

            if (m_hParentWnd)
            {
                ::PostMessage(m_hParentWnd, WM_ICW_START_INFO, 0, 0);
            }
        }
        else
        {
            TRACE(_T("ICWCommManager: Legacy Start detected but parse failed: %s\n"), strTrimmed);
        }
        return;
    }
    
    // JSON消息 - 开始检测
    if (CICWProtocol::IsJsonMessage(strTrimmed))
    {
        ICW_StartInfo startInfo;
        if (CICWProtocol::ParseStartInfo(strTrimmed, startInfo))
        {
            m_lastStartProtocol = StartProtocol::JsonV11;
            TRACE(_T("ICWCommManager: Detected JSON protocol (V1.1). Parsed - Action: %s, JigNumber: %d, Products: %d\n"),
                startInfo.Action, startInfo.JigNumber, (int)startInfo.Products.size());
            
            if (m_cbStart)
            {
                m_cbStart(startInfo);
            }
            
            if (m_hParentWnd)
            {
                // 通知父窗口 (可以传递指针或复制数据)
                ::PostMessage(m_hParentWnd, WM_ICW_START_INFO, 0, 0);
            }
        }
        else
        {
            TRACE(_T("ICWCommManager: Failed to parse JSON: %s\n"), strTrimmed);
        }
        return;
    }
    
    TRACE(_T("ICWCommManager: Unknown message: %s\n"), strTrimmed);
}

///////////////////////////////////////////////////////////////////////////////
// 自动重连线程函数
///////////////////////////////////////////////////////////////////////////////
unsigned int WINAPI CICWCommManager::ReconnectThreadProc(LPVOID lpParam)
{
    CICWCommManager* pThis = static_cast<CICWCommManager*>(lpParam);
    if (!pThis)
        return 0;

    TRACE(_T("ICWCommManager: Reconnect thread started\n"));
    
    while (TRUE)
    {
        // 检查退出事件
        DWORD dwWait = WaitForSingleObject(pThis->m_hReconnectQuitEvent, 0);
        if (dwWait == WAIT_OBJECT_0)
        {
            TRACE(_T("ICWCommManager: Reconnect thread收到退出信号\n"));
            break;
        }

        // 检查是否已连接
        if (pThis->m_bConnected)
        {
            TRACE(_T("ICWCommManager: 已连接到服务器，退出重连循环\n"));
            break;
        }

        // 检查最大重连次数
        if (ICW_MAX_RECONNECT_ATTEMPTS > 0 && pThis->m_nReconnectAttempts >= ICW_MAX_RECONNECT_ATTEMPTS)
        {
            TRACE(_T("ICWCommManager: 达到最大重连次数 %d，停止重连\n"), ICW_MAX_RECONNECT_ATTEMPTS);
            break;
        }

        pThis->m_nReconnectAttempts++;
        
        // 通知状态变化
        if (pThis->m_cbReconnectStatus)
        {
            pThis->m_cbReconnectStatus(TRUE, pThis->m_nReconnectAttempts);
        }

        TRACE(_T("ICWCommManager: 尝试重连 [%d] %s:%s\n"), 
            pThis->m_nReconnectAttempts, pThis->m_strServerIP, pThis->m_strServerPort);

        // 执行重连
        BOOL bReconnected = pThis->ReconnectNow();

        if (bReconnected)
        {
            TRACE(_T("ICWCommManager: 重连成功!\n"));
            if (pThis->m_cbReconnectStatus)
            {
                pThis->m_cbReconnectStatus(FALSE, 0);  // 重连成功，停止重连状态
            }
            break;
        }
        else
        {
            TRACE(_T("ICWCommManager: 重连失败，%d ms后再次尝试\n"), ICW_RECONNECT_INTERVAL_MS);
        }

        // 等待间隔
        dwWait = WaitForSingleObject(pThis->m_hReconnectQuitEvent, ICW_RECONNECT_INTERVAL_MS);
        if (dwWait == WAIT_OBJECT_0)
        {
            TRACE(_T("ICWCommManager: 重连等待期间收到退出信号\n"));
            break;
        }
    }

    pThis->m_bAutoReconnectThreadRunning = FALSE;
    TRACE(_T("ICWCommManager: Reconnect thread exited\n"));
    return 0;
}

///////////////////////////////////////////////////////////////////////////////
// 执行一次重连
///////////////////////////////////////////////////////////////////////////////
BOOL CICWCommManager::ReconnectNow()
{
    if (m_strServerIP.IsEmpty() || m_strServerPort.IsEmpty())
    {
        TRACE(_T("ICWCommManager: ReconnectNow - 服务器信息为空，跳过\n"));
        return FALSE;
    }

    // 确保之前的连接已清理
    Disconnect();

    // 重新连接
    if (!ConnectTo(m_strServerIP, m_strServerPort, AF_INET, SOCK_STREAM))
    {
        TRACE(_T("ICWCommManager: ReconnectNow - ConnectTo 失败\n"));
        return FALSE;
    }

    // 启动通信监听
    if (!WatchComm())
    {
        TRACE(_T("ICWCommManager: ReconnectNow - WatchComm 失败\n"));
        ShutdownConnection((SOCKET)m_hComm);
        m_hComm = NULL;
        m_bConnected = FALSE;
        return FALSE;
    }

    m_bConnected = TRUE;
    m_strRecvBuffer.Empty();

    TRACE(_T("ICWCommManager: ReconnectNow - 成功重连到 %s:%s\n"), m_strServerIP, m_strServerPort);

    // 通知父窗口
    if (m_hParentWnd)
    {
        ::PostMessage(m_hParentWnd, WM_ICW_CONNECTED, 0, 0);
    }

    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// 启动自动重连
///////////////////////////////////////////////////////////////////////////////
void CICWCommManager::StartAutoReconnect()
{
    if (!m_bClientMode)
    {
        TRACE(_T("ICWCommManager: StartAutoReconnect - 非客户端模式，不启动重连\n"));
        return;
    }

    if (m_bAutoReconnectThreadRunning)
    {
        TRACE(_T("ICWCommManager: StartAutoReconnect - 重连线程已在运行\n"));
        return;
    }

    EnterCriticalSection(&m_csReconnect);
    m_bAutoReconnectThreadRunning = TRUE;
    m_nReconnectAttempts = 0;
    ResetEvent(m_hReconnectQuitEvent);
    LeaveCriticalSection(&m_csReconnect);

    // 启动重连线程
    unsigned int nThreadID = 0;
    m_hReconnectThread = (HANDLE)_beginthreadex(NULL, 0, &ReconnectThreadProc, this, 0, &nThreadID);
    
    if (m_hReconnectThread == NULL)
    {
        TRACE(_T("ICWCommManager: StartAutoReconnect - _beginthreadex 失败\n"));
        m_bAutoReconnectThreadRunning = FALSE;
        return;
    }

    TRACE(_T("ICWCommManager: StartAutoReconnect - 重连线程已启动\n"));
}

///////////////////////////////////////////////////////////////////////////////
// 停止自动重连
///////////////////////////////////////////////////////////////////////////////
void CICWCommManager::StopAutoReconnect()
{
    if (!m_bAutoReconnectThreadRunning)
        return;

    TRACE(_T("ICWCommManager: StopAutoReconnect - 停止重连线程\n"));

    // 设置退出事件
    SetEvent(m_hReconnectQuitEvent);

    // 等待线程结束
    if (m_hReconnectThread != NULL)
    {
        WaitForSingleObject(m_hReconnectThread, 5000);  // 等待最多5秒
        CloseHandle(m_hReconnectThread);
        m_hReconnectThread = NULL;
    }

    m_bAutoReconnectThreadRunning = FALSE;
    m_nReconnectAttempts = 0;

    TRACE(_T("ICWCommManager: StopAutoReconnect - 完成\n"));
}

