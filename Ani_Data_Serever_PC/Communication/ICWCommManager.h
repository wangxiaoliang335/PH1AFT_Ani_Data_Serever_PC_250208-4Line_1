#pragma once
///////////////////////////////////////////////////////////////////////////////
// FILE : ICWCommManager.h
// ICW Communication Manager - TCP Client with Auto Reconnect
// Inherits from CSocketComm
///////////////////////////////////////////////////////////////////////////////

#ifndef _ICW_COMM_MANAGER_H_
#define _ICW_COMM_MANAGER_H_

#include "SocketComm.h"
#include "ICWProtocol.h"
#include <functional>

// Message callback types
typedef std::function<void(const ICW_StartInfo&)> ICW_StartCallback;
typedef std::function<void()> ICW_SnapFNCallback;
typedef std::function<void(const ICW_LegacyFinishInfo&)> ICW_FinishFNCallback;
typedef std::function<void()> ICW_HeartBeatCallback;
typedef std::function<ICW_VersionInfo()> ICW_GetVersionCallback;

// Reconnect configuration constants (default values, will be overridden by config file)
const int ICW_RECONNECT_INTERVAL_MS_DEFAULT = 10000;   // Reconnect interval 10 seconds
const int ICW_MAX_RECONNECT_ATTEMPTS = 0;       // 0 = unlimited reconnect

///////////////////////////////////////////////////////////////////////////////
// ICW Communication Manager Class
///////////////////////////////////////////////////////////////////////////////
class CICWCommManager : public CSocketComm
{
public:
    CICWCommManager();
    virtual ~CICWCommManager();

    // Load configuration from config file
    void LoadConfig();
    
    // ===== Server Control =====
    
    // Start TCP server
    // strPort: listening port number (e.g., "5000")
    BOOL StartServer(LPCTSTR strPort);
    
    // Stop server
    void StopServer();
    
    // Check if server is running
    BOOL IsRunning() const { return m_bRunning; }
    
    // ===== Client Control (connect to lighting inspection software) =====
    
    // Connect to lighting inspection software as TCP client
    // strIP: lighting inspection software IP address
    // strPort: lighting inspection software port (default 6501)
    BOOL ConnectToServer(LPCTSTR strIP, LPCTSTR strPort);
    
    // Disconnect
    void Disconnect();
    
    // Check if connected
    BOOL IsConnected() const { return m_bConnected; }
    
    // ===== Auto Reconnect Control =====
    
    // Start auto reconnect (only valid in client mode)
    void StartAutoReconnect();
    
    // Stop auto reconnect
    void StopAutoReconnect();
    
    // Check if auto reconnect is in progress
    BOOL IsAutoReconnecting() const { return m_bAutoReconnectThreadRunning; }
    
    // Reconnect status change callback (optional)
    void SetReconnectStatusCallback(std::function<void(BOOL bReconnecting, int nAttempts)> callback) { m_cbReconnectStatus = callback; }
    
    // ===== Message Sending =====
    
    // Send capture complete notification
    BOOL SendSnapFN();
    
    // Send inspection end information
    BOOL SendFinishInfo(const ICW_FinishInfo& info);

    // Auto select sending format based on last received Start protocol:
    // - If last Start is legacy Start$..., send FN$...@
    // - Otherwise send V1.1 JSON FinishInfo@
    BOOL SendFinishInfoAuto(const ICW_FinishInfo& info);
    
    // Send version information
    BOOL SendVersionInfo(const ICW_VersionInfo& info);
    
    // Send raw string message (automatically add @ delimiter)
    BOOL SendMessage(const CString& strMsg);
    
    // ===== Callback Settings =====
    
    // Set inspection start callback
    void SetStartCallback(ICW_StartCallback callback) { m_cbStart = callback; }
    
    // Set capture complete callback (if ICW also sends this message)
    void SetSnapFNCallback(ICW_SnapFNCallback callback) { m_cbSnapFN = callback; }

    // Set inspection complete callback (FN$ message)
    void SetFinishFNCallback(ICW_FinishFNCallback callback) { m_cbFinishFN = callback; }

    // Set heartbeat callback
    void SetHeartBeatCallback(ICW_HeartBeatCallback callback) { m_cbHeartBeat = callback; }
    
    // Set version query callback
    void SetGetVersionCallback(ICW_GetVersionCallback callback) { m_cbGetVersion = callback; }
    
    // ===== Parent Window Notification =====
    
    // Set parent window handle (for sending Windows messages)
    void SetParentWnd(HWND hWnd) { m_hParentWnd = hWnd; }
    
    // Windows message ID definitions
    static const UINT WM_ICW_CONNECTED = WM_USER + 100;
    static const UINT WM_ICW_DISCONNECTED = WM_USER + 101;
    static const UINT WM_ICW_START_INFO = WM_USER + 102;
    static const UINT WM_ICW_ERROR = WM_USER + 103;

protected:
    // Override data received handling
    virtual void OnDataReceived(const LPBYTE lpBuffer, DWORD dwCount);
    
    // Override event handling
    virtual void OnEvent(UINT uEvent, LPVOID lpvData);
    
private:
    // Process received complete message
    void ProcessMessage(const CString& strMsg);

    enum class StartProtocol
    {
        Unknown = 0,
        JsonV11,
        LegacyStartDollar,
    };
    
    // Member variables
    BOOL m_bRunning;                    // Server running status
    BOOL m_bClientMode;                 // Whether in client mode
    BOOL m_bConnected;                  // Client connection status
    CString m_strServerIP;              // Server IP address
    CString m_strServerPort;            // Server port
    CString m_strRecvBuffer;            // Receive buffer
    HWND m_hParentWnd;                 // Parent window handle
    CRITICAL_SECTION m_csRecv;          // Receive buffer lock
    CRITICAL_SECTION m_csSend;          // Send lock
    
    // Callback functions
    ICW_StartCallback m_cbStart;
    ICW_SnapFNCallback m_cbSnapFN;
    ICW_FinishFNCallback m_cbFinishFN;
    ICW_HeartBeatCallback m_cbHeartBeat;
    ICW_GetVersionCallback m_cbGetVersion;
    std::function<void(BOOL, int)> m_cbReconnectStatus;  // Reconnect status callback

    // Last received Start message protocol type, used for auto format selection in finish reply
    StartProtocol m_lastStartProtocol = StartProtocol::Unknown;
    
    // ===== Auto Reconnect Member Variables =====
    BOOL m_bAutoReconnectThreadRunning;    // Whether reconnect thread is running
    int m_nReconnectAttempts;              // Current reconnect attempt count
    int m_nReconnectIntervalMs;            // Reconnect interval (milliseconds, loaded from config file)
    HANDLE m_hReconnectQuitEvent;         // Reconnect thread quit event
    HANDLE m_hReconnectThread;             // Reconnect thread handle
    CRITICAL_SECTION m_csReconnect;        // Reconnect operation lock

    // Reconnect thread function
    static unsigned int WINAPI ReconnectThreadProc(LPVOID lpParam);
    
    // Perform one reconnect attempt
    BOOL ReconnectNow();

    // ===== Disconnect Debounce (防止网络抖动导致频繁断连) =====
    BOOL m_bPendingDisconnect;             // 是否有待处理的断开事件
    DWORD m_dwDisconnectTime;              // 触发断开的时间戳
    static const DWORD DISCONNECT_DEBOUNCE_MS = 2000; // 2秒内恢复则忽略断开
    CRITICAL_SECTION m_csDebounce;         // 防抖操作锁
};

#endif // _ICW_COMM_MANAGER_H_
