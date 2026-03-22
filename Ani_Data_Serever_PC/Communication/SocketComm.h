///////////////////////////////////////////////////////////////////////////////
// FILE : SocketComm.h
// Header file for CSocketComm class
// CSocketComm
//     Generic class for Socket Communication
//
// 中文说明：
// 本文件定义了通用的 Socket 通信基类 `CSocketComm`，所有和外部设备
//（PG、TP、OPV、上位机 / 下位机等）的 TCP/UDP 通讯类都继承自它。
// 主要职责：
//   - 封装 socket 创建 / 绑定 / 连接 / 监听 等底层细节
//   - 提供统一的收发接口 `ReadComm` / `WriteComm`
//   - 通过线程 `Run()` 循环接收数据，并回调虚函数
//       `OnDataReceived`（收到一帧数据时回调）
//       `OnEvent`       （连接建立 / 断开 / 错误时回调）
// 各业务模块只需继承并重写上述虚函数，就可以按各自协议解析数据。
///////////////////////////////////////////////////////////////////////////////

#ifndef _SOCKETCOMM_H_
#define _SOCKETCOMM_H_
#include <list>

#include <stdlib.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32")

// Event value（套接字事件类型，用于 OnEvent 回调）
#define EVT_CONSUCCESS      0x0000  // Connection established
#define EVT_CONFAILURE      0x0001  // General failure - Wait Connection failed
#define EVT_CONDROP         0x0002  // Connection dropped
#define EVT_ZEROLENGTH      0x0003  // Zero length message

#ifndef BUFFER_SIZE
#define BUFFER_SIZE     8192
#endif
#define HOSTNAME_SIZE   8192
#define STRING_LENGTH   40

// 对 WinSock 的 `SOCKADDR_IN` 做了一层封装，用于比较 / 拷贝 / 记录来源地址。
// 在 UDP 广播、Smart Addressing 模式下，`CSocketComm` 会维护一个
// `CSockAddrList`，保存当前已知的对端 IP/Port 列表。
struct SockAddrIn : public SOCKADDR_IN {
public:
	SockAddrIn() { Clear(); }
	SockAddrIn(const SockAddrIn& sin) { Copy(sin); }
	~SockAddrIn() { }
	SockAddrIn& Copy(const SockAddrIn& sin);
	void    Clear() { memset(this, 0, sizeof(SOCKADDR_IN)); }
	bool    IsEqual(const SockAddrIn& sin) const;
	bool    IsGreater(const SockAddrIn& sin) const;
	bool    IsLower(const SockAddrIn& pm) const;
	bool    IsNull() const { return ((sin_addr.s_addr == 0L) && (sin_port == 0)); }
	ULONG   GetIPAddr() const { return sin_addr.s_addr; }
	short   GetPort() const { return sin_port; }
	bool    CreateFrom(LPCTSTR sAddr, LPCTSTR sService, int nFamily = AF_INET);
	SockAddrIn& operator=(const SockAddrIn& sin) { return Copy(sin); }
	bool    operator==(const SockAddrIn& sin) const { return IsEqual(sin); }
	bool    operator!=(const SockAddrIn& sin) const { return !IsEqual(sin); }
	bool    operator<(const SockAddrIn& sin)  const { return IsLower(sin); }
	bool    operator>(const SockAddrIn& sin)  const { return IsGreater(sin); }
	bool    operator<=(const SockAddrIn& sin) const { return !IsGreater(sin); }
	bool    operator>=(const SockAddrIn& sin) const { return !IsLower(sin); }
	operator LPSOCKADDR() const { return (LPSOCKADDR)(this); }
	size_t  Size() const { return sizeof(SOCKADDR_IN); }
	void    SetAddr(SOCKADDR_IN* psin) { memcpy(this, psin, Size()); }
};

typedef std::list<SockAddrIn> CSockAddrList;   // 广播 / 多播模式下的对端地址列表

// `stMessageProxy` 是 `Run()` 线程里用于接收数据的缓冲结构：
// - address : 当前报文来源地址（仅 SmartAddressing 模式有效）
// - byData  : 实际收到的业务数据，由上层协议类解析
struct stMessageProxy
{
	SockAddrIn address;
	BYTE byData[BUFFER_SIZE];
};

// 通用 Socket 通信基类
// 使用流程（典型服务器端）：
//   1) 业务类继承 `CSocketComm`
//   2) 在子类中重写 `OnDataReceived` 与 `OnEvent`
//   3) 调用 `SetServerState(true)` → `CreateSocket` → `WatchComm`
//   4) 通过 `WriteComm` 发送业务数据
// 客户端模式则通过 `ConnectTo` 建立连接后再调用 `WatchComm`。
class CSocketComm
{
public:
	// 三菱 PLC 模拟模式标志（某些 socket 创建逻辑会根据该标志选择不同的 bind/connect 方式）
	bool m_bMelsecSimulaion;


	CSocketComm();
	virtual ~CSocketComm();

	// 状态查询接口
	bool IsOpen() const;        // Socket 是否有效
	bool IsStart() const;       // 通信线程是否已启动
	bool IsServer() const;      // 是否服务器模式（listen/accept）
	bool IsBroadcast() const;   // 是否 UDP 广播模式
	bool IsSmartAddressing() const; // 是否 Smart Addressing 模式
	SOCKET GetSocket() const;   // 返回底层 socket 句柄

	// 模式设置（需在 WatchComm 之前调用）
	void SetServerState(bool bServer);               // 设置是否服务器模式
	void SetSmartAddressing(bool bSmartAddressing);  // 设置 Smart Addressing 模式

	// 地址 / 连接相关
	bool GetSockName(SockAddrIn& saddr_in);  // 获取本端地址
	bool GetPeerName(SockAddrIn& saddr_in);  // 获取对端地址
	bool AddMembership(LPCTSTR strAddress);  // UDP 多播：加入组
	bool DropMembership(LPCTSTR strAddress); // UDP 多播：退出组
	void AddToList(const SockAddrIn& saddr_in);      // 将客户端地址加入列表
	void RemoveFromList(const SockAddrIn& saddr_in); // 从客户端地址列表中删除

	// 通信控制
	void CloseComm();       // 关闭 Socket
	bool WatchComm();       // 启动通信线程（内部调用 Run）
	void StopComm();        // 停止通信线程并清理相关资源

	// Create a socket - Server side (support for multiple adapters)
	bool CreateSocketEx(LPCTSTR strHost, LPCTSTR strServiceName, int nFamily, int nType, UINT uOptions /* = 0 */);
	// Create a Socket - Server side
	bool CreateSocket(LPCTSTR strServiceName, int nProtocol, int nType, UINT uOptions = 0);
	// Create a socket, connect to (Client side)
	bool ConnectTo(LPCTSTR strDestination, LPCTSTR strServiceName, int nProtocol, int nType);

	// Event function - 事件回调函数（子类应根据各自协议重写）
	// lpBuffer : 收到的数据缓冲区（已经是一帧，TCP 粘包拆包逻辑由上层保证）
	// dwCount  : 有效字节数
	virtual void OnDataReceived(const LPBYTE lpBuffer, DWORD dwCount);
	// uEvent : 连接事件（EVT_CONSUCCESS / EVT_CONDROP / EVT_ZEROLENGTH / EVT_CONFAILURE 等）
	virtual void OnEvent(UINT uEvent, LPVOID lpvData);

	// Run function - 线程主循环，内部循环调用 ReadComm 并触发 OnDataReceived
	// 如需完全自定义循环行为，可以在子类中重写本函数。
	virtual void Run();

	// Data function - 通用收发接口
	DWORD ReadComm(LPBYTE lpBuffer, DWORD dwSize, DWORD dwTimeout);          // 读取一帧数据（阻塞到超时或有数据）
	DWORD WriteComm(const LPBYTE lpBuffer, DWORD dwCount, DWORD dwTimeout);  // 发送一帧数据

	// Utility functions - 与 Socket / 主机信息相关的工具函数
	static SOCKET WaitForConnection(SOCKET sock);        // 服务器端阻塞等待新连接（accept）
	static bool ShutdownConnection(SOCKET sock);         // 主动关闭连接
	static USHORT GetPortNumber(LPCTSTR strServiceName); // 根据服务名 / 端口字符串解析端口
	static ULONG GetIPAddress(LPCTSTR strHostName);      // 根据主机名 / IP 字符串解析 IP
	static bool GetLocalName(LPTSTR strName, UINT nSize);     // 获取本机主机名
	static bool GetLocalAddress(LPTSTR strAddress, UINT nSize); // 获取本机 IP 地址
	// SocketComm - data

protected:
	HANDLE      m_hComm;        // 实际 socket 句柄（内部强制转换为 SOCKET 使用）
	HANDLE      m_hThread;      // 通信线程句柄（与 m_pWinThread 对应）
	bool        m_bServer;      // 是否服务器模式
	bool        m_bSmartAddressing; // 是否 Smart Addressing 模式（多监听者）
	bool        m_bBroadcast;   // 是否广播模式
	bool		m_bClient;		// 是否客户端模式
	CSockAddrList m_AddrList;   // 广播 / 多播模式下维护的对端地址列表
	HANDLE      m_hMutex;       // 保护 m_AddrList 的互斥量

	HANDLE      m_hMutex2;       // 保护地址列表（接收路径）使用的互斥量		//>>191109 hjjang
	HANDLE      m_hMutex3;       // 保护地址列表（发送路径）使用的互斥量		//>>191109 hjjang

	CWinThread *m_pWinThread;    // MFC 工作线程对象指针
	// SocketComm - function
protected:
	// Synchronization function
	void LockList();            // Lock the object
	void UnlockList();          // Unlock the object

	void LockList2();            // Lock the object		//>>191109 hjjang
	void UnlockList2();          // Unlock the object	//>>191109 hjjang

	void LockList3();            // Lock the object		//>>191109 hjjang
	void UnlockList3();          // Unlock the object	//>>191109 hjjang

	static UINT SocketThreadProc(LPVOID pParam);

private:
};

#endif // _SOCKETCOMM_H_
