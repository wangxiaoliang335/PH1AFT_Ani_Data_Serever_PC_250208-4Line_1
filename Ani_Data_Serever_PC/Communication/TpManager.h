#pragma once

#include "Ani_Data_Serever_PC.h"
//#include "ClientSocket.h"
#include "SocketComm.h"
#include "EZini.h"
#include "StringSupport.h"
//
// CTpManager
// ---------------------------------------------------------------------------
// - 角色：MC <-> TP（Touch Panel / 触摸测试机）之间的 Socket 通讯管理类，继承自 `CSocketComm`
// - 物理连接：MC 作为 TCP Server，TP 端作为 Client 连接
// - 发送帧格式（MC -> TP，通过 `SendTPMessage` 统一发送，字符串本体示例）：
//     #CK*<Ch>#                : TP_CheckConnect      -> 通信/心跳确认
//     #JZ*<Ch>*<ProductID>#    : TP_ProductID         -> 下发机种或 TP Program ID
//     #PD*<Ch>*<PanelID>#      : TP_SendPanelID       -> 下发当前 Panel ID
//     #ST*<Ch>#                : TP_InspStart         -> 触摸检开始指令
//     #RT*<Ch>*OK#             : TP_InspResult(OK)    -> MC 将 TP 结果反馈 PLC（OK/NG 具体状态由 PLC Word/Rank 决定）
//     #RE*<Ch>#                : TP_CodeRequest       -> 请求 TP 端发送 TP Code（PG Code 校验使用）
//   其中 <Ch> 为 TP 通道号（1~18，对应 AOI:1~16, ULD:17~18），在 MC 内部会通过 Ch 映射到 Panel/Zone。
// - 接收帧格式（TP -> MC，在 `OnDataReceived` 中解析，典型字符串示例）：
//     #RT*<Ch>*OK# / #RT*<Ch>*NG#     : 触摸检结果，MC 将结果翻译成 PLC Rank / Result Word
//     #TP*<Ch>*<Code...>#            : TP Code 通知，用于同 PG 的机种/PG-Code 校验逻辑
// - 主要职责：
//     * 负责 TP 指令的拼接、发送及日志记录（`SendTPMessage` / `TpLogMessage`）
//     * 解析 TP 返回结果，根据通道号写入 PLC Bit/Word（触摸 OK/NG、Retry、Bypass 等）
//     * 与 `CPgManager`/`COpvManager`、PLC Rank、PG TURNON 流程协同，决定 Panel 的后续 Flow。
//
class CTpManager : public CSocketComm
{
public:
	CTpManager();
	virtual ~CTpManager();
	// 中文说明：
	//   **功能：** 打开 TP Socket Server 端口，开始监听 TP 测试机的连接。
	//   **参数：** `strServerPort` 为监听端口号字符串（例如 "9102"）。
	//   **返回：** `true` 监听成功并开始接收；`false` 打开失败。
	bool SocketServerOpen(CString strServerPort);
	// 中文说明：
	//   **功能：** 查询当前 TP Socket 是否已建立连接。
	//   **返回：** `TRUE` 已连接；`FALSE` 未连接。
	BOOL getConectCheck();

	// 中文说明：
	//   **功能：** 组装并发送 TP 命令帧（MC -> TP）。
	//   **典型参数/用法：**
	//     - `iChNum`   : TP 通道号（1~18）。
	//     - `iCommand` : 命令枚举（如 TP_CheckConnect / TP_InspStart / TP_SendPanelID 等）。
	//     - `strContent` : 可选业务内容（如 PanelID / ProductID / 机种等），为空时使用默认模板。
	void SendTPMessage(int iChNum, int iCommand, CString strContent = NULL);
	// 中文说明：
	//   **功能：** 记录 TP 通信相关日志（发送/接收/错误），显示在 UI 并写入文件。
	void TpLogMessage(CString strContents);

	// 中文说明：
	//   **功能：** TP Socket 收到数据后的回调入口，负责把原始字符串拆帧解析，
	//             并根据命令类型更新 PLC、内部状态（TP 结果 / Code 等）。
	virtual void OnDataReceived(const LPBYTE lpBuffer, DWORD dwCount);
	// 中文说明：
	//   **功能：** TP Socket 连接事件回调（连接成功/断开/错误），用于更新连接状态和打印日志。
	virtual void OnEvent(UINT uEvent, LPVOID lpvData);

	int m_iTpSocketCheckCount;

	std::vector<CString> m_lastContent;
	std::vector<CString> m_lastResult;
	std::vector<CString> m_lastRequest;

	CString GetLastContents(int iChNum) { return m_lastContent[iChNum]; }
	CString GetLastResult(int iChNum) { return m_lastResult[iChNum]; }
	CString GetLastRequest(int iChNum) { return m_lastRequest[iChNum]; }

	CCriticalSection m_csTpData;
	CString m_strDummyContents;
};