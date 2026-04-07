#pragma once

///////////////////////////////////////////////////////////////////////////////
// FILE : PgManager.h
// Class : CPgManager
//
// 中文说明：
//   PG = Pattern / Gamma 装置，本类负责与 PG 设备之间的 TCP Socket 通信，
//   继承自通用通信基类 `CSocketComm`。
//
//   主要职责：
//     - 建立 PG Socket Server（`SocketServerOpen`）
//     - 发送控制命令到 PG（`SendPGMessage`）
//     - 接收 PG 返回结果并解析（`OnDataReceived` -> AOI / ULD / Gamma 3 种路径）
//     - 将 PG 结果写入 PLC（Melsec）并更新内部队列（`theApp.m_lastIndexPgVec` 等）
//
//   典型发送帧格式（ASCII，字段间逗号',' 分隔）：
//     - 接触 ON : "Ch,<通道号>,CONTACT,<PanelID>"
//     - 接触 OFF: "Ch,<通道号>,KEY,RESET"
//     - Pre-Gamma: "Ch,<通道号>,PREGAMMA,<START/END>,<PanelID/TEST名>,<GOOD/NG>"
//     - Pattern 切换: "Ch,<通道号>,PTRN,<Pattern 编号>"
//
//   TCP 发送时会再封装一层：
//     [STX][DEST][LEN(4位16进制)][ASCII 文本][ETX]
//   这些帧由 `SendPGMessage` 组包并通过 `CSocketComm::WriteComm` 发送。
///////////////////////////////////////////////////////////////////////////////

#include "Ani_Data_Serever_PC.h"
#include "SocketServerBase.h"
#include "EZini.h"
#include "StringSupport.h"

class CPgManager: public CSocketServerBase
{
public:
	CPgManager();
	virtual ~CPgManager();
	// 中文说明：
	//   **功能：** 打开 PG Socket Server 端口，并记录当前 PC 编号（Line 内可能有多台 PG-PC）。
	//   **参数：**
	//     - `strServerPort` : 监听端口号（字符串形式，如 "9101"）。
	//     - `iPcNum`        : 本机在整线中的 PC 编号，用于区分多台 PG-PC 的索引。
	//   **返回：** `true` 表示监听线程启动成功。
	bool SocketServerOpen(CString strServerPort, int iPcNum);
	// 中文说明：
	//   **功能：** 查询 PG Socket 是否已经建立连接（至少有一个 PG Client 连上）。
	//   **返回：** `TRUE` 已连接；`FALSE` 未连接。
	BOOL getConectCheck();

	// 中文说明：
	//   **功能：** PG 数据接收回调函数，由 `CSocketComm` 的接收线程在有数据时调用。
	//   **说明：**
	//     - 负责拆解 STX/ETX 帧头帧尾，按逗号分隔字段，判断属于 AOI / ULD / Gamma 哪一类。
	//     - 解析出的结果会写入 `m_lastContent / m_lastResult` 等缓存数组，
	//       并通过 Melsec 写入 PLC，驱动后续 Flow 判定 / Rank 判定 / DFS 报工。
	virtual void OnDataReceived(const LPBYTE lpBuffer, DWORD dwCount);
	// 中文说明：
	//   **功能：** PG Socket 连接事件回调（连接成功 / 断开 / 错误）。
	//   **典型用途：**
	//     - 连接成功时在运行画面显示“PG Connected”，并清空上一次缓存。
	//     - 连接断开时通知 Flow / PLC 停止取片，防止无 PG 的情况下继续生产。
	virtual void OnEvent(UINT uEvent, LPVOID lpvData);

	// 中文说明：
	//   **功能：** 按照 PG 协议组装并发送一条命令帧。
	//   **参数：**
	//     - `strMsg`    : 业务内容（例如 "CONTACT,PanelID" / "PTRN,001" 等，不含通道号）。
	//     - `iChNum`    : 通道号（Ch1~ChN）。
	//     - `iStageNum` : Gamma 阶段号 / Flow 阶段（有的项目仅用 0 或 1）。
	//   **说明：**
	//     - 内部会自动加上 "Ch,<iChNum>," 前缀以及 STX/LEN/ETX 头尾，通过 `WriteComm` 发送。
	void SendPGMessage(CString strMsg, int iChNum, int iStageNum = NULL);
	// 中文说明：
	//   **功能：** 将 PG 相关的发送 / 接收 / 解析结果写入 Log（以及界面 Message 窗口）。
	//   **用途：** 主要用于调试现场 PG 通信异常、确认 Gamma 结果与 Rank 判定的一致性。
	void PgLogMessage(CString strContents);
	// 中文说明：
	//   **功能：** 主动断开当前所有 PG Client 连接，用于程序退出或异常恢复。
	void RemoveClient();
#if _SYSTEM_AMTAFT_
	std::vector<CString> m_lastContent;
	std::vector<CString> m_lastResult;
	std::vector<CString> m_lastCommand;
	std::vector<CString> m_lastRequest;
	std::vector<CString> m_lastGammaL;
	std::vector<CString> m_lastGammaX;
	std::vector<CString> m_lastGammaY;
	
	CString GetLastContents(int iChNum) { return m_lastContent[iChNum]; }
	CString GetLastResult(int iChNum) { return m_lastResult[iChNum]; }
	CString GetLastCommand(int iChNum) { return m_lastCommand[iChNum]; }
	CString GetLastRequest(int iChNum) { return m_lastRequest[iChNum]; }
	CString GetLastGammaL(int iChNum) { return m_lastGammaL[iChNum]; }
	CString GetLastGammaX(int iChNum) { return m_lastGammaX[iChNum]; }
	CString GetLastGammaY(int iChNum) { return m_lastGammaY[iChNum]; }
#else
	CString m_lastContent[MaxGammaStage][ChMaxCount];
	CString m_lastResult[MaxGammaStage][ChMaxCount];
	CString m_lastCommand[MaxGammaStage][ChMaxCount];
	CString m_lastRequest[MaxGammaStage][ChMaxCount];
	CString m_lastGammaVBAT[MaxGammaStage][ChMaxCount];
	CString m_lastGammaVDDI[MaxGammaStage][ChMaxCount];
	CString m_lastGammaVCI[MaxGammaStage][ChMaxCount];
	CString m_lastGammaPGCODE[MaxGammaStage][ChMaxCount];

	CString GetLastContents(int iStageNum, int iChNum) { return m_lastContent[iStageNum][iChNum]; }
	CString GetLastResult(int iStageNum, int iChNum) { return m_lastResult[iStageNum][iChNum]; }
	CString GetLastCommand(int iStageNum, int iChNum) { return m_lastCommand[iStageNum][iChNum]; }
	CString GetLastRequest(int iStageNum, int iChNum) { return m_lastRequest[iStageNum][iChNum]; }
	CString GetLastGammaVBAT(int iStageNum, int iChNum) { return m_lastGammaVBAT[iStageNum][iChNum]; }
	CString GetLastGammaVDDI(int iStageNum, int iChNum) { return m_lastGammaVDDI[iStageNum][iChNum]; }
	CString GetLastGammaVCI(int iStageNum, int iChNum) { return m_lastGammaVCI[iStageNum][iChNum]; }
	CString GetLastGammaPGCODEI(int iStageNum, int iChNum) { return m_lastGammaPGCODE[iStageNum][iChNum]; }
#endif
	CCriticalSection m_csPgData;
	int m_iPcNum;

#if _SYSTEM_AMTAFT_
	// 中文说明：
	//   **功能：** 处理 AOI 返回的数据字符串，解析 PanelID / 结果 / Code 等，
	//             并更新 AOI 相关的队列、PLC 信号以及 DFS 报工结构体。
	void AOIDataReceived(CString strContents);
	// 中文说明：
	//   **功能：** 处理 ULD（Unload / Taping 等）工位返回的数据，
	//             通常用于确认下游设备是否已正常取片 / 贴片完成。
	void ULDDataReceived(CString strContents);
#else
	// 中文说明：
	//   **功能：** 处理 Gamma 测试返回的数据（多 Stage、多 Channel），解析 Gamma 电压、
	//             PG Code 等信息，填充到 `m_lastGamma*` 阵列供 Flow / Rank / DFS 使用。
	void GammaDataReceived(CString strContents);
#endif

	CCriticalSection m_csSocketSend;

protected:
	// 实现基类的纯虚函数
	virtual LPCTSTR GetDeviceName() const override { return _T("PG"); }
	virtual void LogServerMsg(LPCTSTR szMsg) override;
};