#pragma once
///////////////////////////////////////////////////////////////////////////////
// FILE : ICWCommManager.h
// ICW通信管理器 - TCP服务器端，管理与ICW客户端的通信
// 继承自 CSocketComm
///////////////////////////////////////////////////////////////////////////////

#ifndef _ICW_COMM_MANAGER_H_
#define _ICW_COMM_MANAGER_H_

#include "SocketComm.h"
#include "ICWProtocol.h"
#include <functional>

// 消息回调类型
typedef std::function<void(const ICW_StartInfo&)> ICW_StartCallback;
typedef std::function<void()> ICW_SnapFNCallback;
typedef std::function<void(const ICW_LegacyFinishInfo&)> ICW_FinishFNCallback;
typedef std::function<void()> ICW_HeartBeatCallback;
typedef std::function<ICW_VersionInfo()> ICW_GetVersionCallback;

///////////////////////////////////////////////////////////////////////////////
// ICW通信管理器类
///////////////////////////////////////////////////////////////////////////////
class CICWCommManager : public CSocketComm
{
public:
    CICWCommManager();
    virtual ~CICWCommManager();
    
    // ===== 服务器控制 =====
    
    // 启动TCP服务器
    // strPort: 监听端口号 (如 "5000")
    BOOL StartServer(LPCTSTR strPort);
    
    // 停止服务器
    void StopServer();
    
    // 服务器是否运行中
    BOOL IsRunning() const { return m_bRunning; }
    
    // ===== 客户端控制 (连接点灯检软件) =====
    
    // 作为TCP客户端连接点灯检软件
    // strIP: 点灯检软件IP地址
    // strPort: 点灯检软件端口号 (默认6501)
    BOOL ConnectToServer(LPCTSTR strIP, LPCTSTR strPort);
    
    // 断开连接
    void Disconnect();
    
    // 是否已连接
    BOOL IsConnected() const { return m_bConnected; }
    
    // 服务器是否运行中
    BOOL IsServerRunning() const { return m_bRunning; }
    
    // ===== 消息发送 =====
    
    // 发送采图完成通知
    BOOL SendSnapFN();
    
    // 发送检测结束信息
    BOOL SendFinishInfo(const ICW_FinishInfo& info);

    // 按最后一次收到的Start协议自动选择发送格式:
    // - 若最后一次Start为旧版Start$...，则发送 FN$...@
    // - 否则发送 V1.1 JSON FinishInfo@
    BOOL SendFinishInfoAuto(const ICW_FinishInfo& info);
    
    // 发送版本信息
    BOOL SendVersionInfo(const ICW_VersionInfo& info);
    
    // 发送原始字符串消息 (自动添加@分隔符)
    BOOL SendMessage(const CString& strMsg);
    
    // ===== 回调设置 =====
    
    // 设置开始检测回调
    void SetStartCallback(ICW_StartCallback callback) { m_cbStart = callback; }
    
    // 设置采图完成回调 (如果ICW也发送此消息)
    void SetSnapFNCallback(ICW_SnapFNCallback callback) { m_cbSnapFN = callback; }

    // 设置检测完成回调 (FN$ 消息)
    void SetFinishFNCallback(ICW_FinishFNCallback callback) { m_cbFinishFN = callback; }

    // 设置心跳回调
    void SetHeartBeatCallback(ICW_HeartBeatCallback callback) { m_cbHeartBeat = callback; }
    
    // 设置版本查询回调
    void SetGetVersionCallback(ICW_GetVersionCallback callback) { m_cbGetVersion = callback; }
    
    // ===== 父窗口通知 =====
    
    // 设置父窗口句柄(用于发送Windows消息通知)
    void SetParentWnd(HWND hWnd) { m_hParentWnd = hWnd; }
    
    // Windows消息ID定义
    static const UINT WM_ICW_CONNECTED = WM_USER + 100;
    static const UINT WM_ICW_DISCONNECTED = WM_USER + 101;
    static const UINT WM_ICW_START_INFO = WM_USER + 102;
    static const UINT WM_ICW_ERROR = WM_USER + 103;

protected:
    // 重写数据接收处理
    virtual void OnDataReceived(const LPBYTE lpBuffer, DWORD dwCount);
    
    // 重写事件处理
    virtual void OnEvent(UINT uEvent, LPVOID lpvData);
    
private:
    // 处理接收到的完整消息
    void ProcessMessage(const CString& strMsg);

    enum class StartProtocol
    {
        Unknown = 0,
        JsonV11,
        LegacyStartDollar,
    };
    
    // 成员变量
    BOOL m_bRunning;                    // 服务器运行状态
    BOOL m_bClientMode;                  // 是否为客户端模式
    BOOL m_bConnected;                  // 客户端连接状态
    CString m_strServerIP;               // 服务器IP地址
    CString m_strServerPort;             // 服务器端口
    CString m_strRecvBuffer;            // 接收缓冲区
    HWND m_hParentWnd;                  // 父窗口句柄
    CRITICAL_SECTION m_csRecv;          // 接收缓冲区锁
    CRITICAL_SECTION m_csSend;          // 发送锁
    
    // 回调函数
    ICW_StartCallback m_cbStart;
    ICW_SnapFNCallback m_cbSnapFN;
    ICW_FinishFNCallback m_cbFinishFN;
    ICW_HeartBeatCallback m_cbHeartBeat;
    ICW_GetVersionCallback m_cbGetVersion;

    // 最近一次Start报文协议类型，用于结束回包自动选择格式
    StartProtocol m_lastStartProtocol = StartProtocol::Unknown;
};

#endif // _ICW_COMM_MANAGER_H_

