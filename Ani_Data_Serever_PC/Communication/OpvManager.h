#pragma once

#if _SYSTEM_AMTAFT_
#include "Ani_Data_Serever_PC.h"
#include "SocketComm.h"
#include "EZini.h"
#include "StringSupport.h"
//
// COpvManager
// ---------------------------------------------------------------------------
// - 角色：MC <-> OPV（Open Panel / ULD 视觉设备）之间的 Socket 通讯管理类，继承自 `CSocketComm`
// - 物理连接：MC 作为 TCP Server，OPV 端作为 Client 连接
// - 发送/接收数据形态：
//     * 和 TP/PG 不同，OPV 侧主要以 CSV/行字符串形式传递 Panel 缺陷结果及 DFS 相关信息
//     * 典型字段（`ULD_SDFSDefectDataBegin2`）：
//         - strPanel_ID   : Panel ID（与线体/PLC/PG 使用的 Panel ID 对应）
//         - strFpc_ID     : FPC ID / 条码
//         - strDefect_Grade : 判定等级（OK/NG、A/B/C 等）
//         - strTP_Function  : 触摸相关功能信息（与 TP 结果联动）
//         - strDefect_code  : 缺陷代码
//         - strDefect_Ptn   : 缺陷 Pattern
//         - strProcessID    : OPV Process ID（DFS 工程追溯用，机台/工序识别）
// - 主要函数职责：
//     * `SendOpvMessage`      : MC -> OPV 指令/请求发送（含 PanelNum、Command 区分）
//     * `OpvInspectionResult` : 解析 OPV 返回的检出结果，映射到内部 Defect List 与 PLC Rank
//     * `GetOPID`             : 解析并保存 OPV 端的 OPID / ProcessID（生产履历使用）
//     * `OpvModelRequest`     : 向 OPV 请求/下发 Model/Program ID（与 PG/TP 机种匹配）
//     * `OpvPcTimeRequest`    : 同步 PC/OPV 时间（时间戳追溯）
//     * `OpvDefectCountSave`  : 将当前 Panel 的缺陷统计信息持久化（DB/Log）
// - 状态/缓存：
//     * `m_lastContent/m_lastResult/m_lastCommand/m_lastRequest` 记录各通道最后一次通讯内容/结果
//     * 多处 `CCriticalSection` 保证多线程环境下 Socket 发送与缺陷数据访问安全
//
struct ULD_SDFSDefectDataBegin2 {
	CString strPanel_ID;
	CString strFpc_ID;
	CString strDefect_Grade;
	CString strTP_Function;
	CString strDefect_code;
	CString strDefect_Ptn;
	CString strProcessID; // 用于与 DFS 联动的 OPV ProcessID（工序/机台识别与追溯）
};

class COpvManager:  public CSocketComm
{
public:
	COpvManager();
	virtual ~COpvManager();
	// 中文说明：
	//   **功能：** 打开 OPV Socket Server 端口并开始监听 OPV 端连接。
	//   **参数：** `strServerPort` 为监听端口号（字符串形式，例如 "9103"）。
	//   **返回：** `true` 表示监听线程启动成功。
	bool SocketServerOpen(CString strServerPort);
	// 中文说明：
	//   **功能：** 查询当前是否已有 OPV Client 成功连接。
	//   **返回：** `TRUE` 已连接；`FALSE` 未连接。
	BOOL getConectCheck();

	// 中文说明：
	//   **功能：** Socket 接收回调函数，收到 OPV 一帧数据时由 `CSocketComm` 调用。
	//   **说明：** 内部会解析 OPV 发送的 CSV/行字符串，并分发到 `OpvInspectionResult`、`GetOPID` 等函数中处理。
	virtual void OnDataReceived(const LPBYTE lpBuffer, DWORD dwCount);
	// 中文说明：
	//   **功能：** Socket 事件回调（连接成功/断开/错误），用于记录日志和状态。
	virtual void OnEvent(UINT uEvent, LPVOID lpvData);

	// 中文说明：
	//   **功能：** 按约定协议组包并发送 MC -> OPV 的指令字符串。
	//   **典型参数：**
	//     - `strMsg`    : 业务内容本体（不含通道和命令时的辅助字段）。
	//     - `iPanelNum` : 面板索引/通道号，用于多通道 OPV 区分。
	//     - `iCommand`  : 指令种类（如 MC_NG_PANEL / MC_OK_PANEL 等，和 OPV 协议表对应）。
	void SendOpvMessage(CString strMsg, int iPanelNum, int iCommand);
	// 中文说明：
	//   **功能：** 将 OPV 收发的关键内容写入运行日志窗口和 Log 文件，便于事后追溯。
	void OpvLogMessage(CString strContents);
	// 中文说明：
	//   **功能：** 检查 OPV 返回的 CSV 标题行是否与预期格式匹配（列数/字段名）。
	BOOL GetTitleCheck(CStdioFile& sFile, int iSize);
	// 中文说明：
	//   **功能：** 统计指定字符串中（通常为一行 CSV）包含的字段个数，按分隔符切分后得到列数。
	int GetItemCount(CString strInfo);
	// 中文说明：
	//   **功能：** 从 OPV 原始消息中提取一条有效的业务内容（去掉 STX/ETX 或多余控制字符）。
	CString GetExtractionMsg(CString& strMsg);
	// 中文说明：
	//   **功能：** 从缓存中提取上一条解析过的 OPV 消息，便于 UI 或其他线程查看。
	CString GetLastExtractionMsg(CString& strMsg);
	// 中文说明：
	//   **功能：** 按 PanelID 加载并解析 OPV 缺陷列表，将结果填充到 `m_ULD_DefectDataList2` 中。
	//   **参数：** `iCount` 为预期缺陷数量或需要读取的记录行数。
	BOOL LoadDefectListInfo(CString strPanel, int iCount);
	// 中文说明：
	//   **功能：** 解析 OPV 返回的缺陷结果字符串（按通道/Panel），并更新内部缺陷列表／PLC Rank。
	void OpvInspectionResult(int Num, CString strContents);
	// 中文说明：
	//   **功能：** 从 OPV 返回的数据中提取 OPID/ProcessID 等生产履历信息并保存到 `m_strOPID`。
	void GetOPID(int Num, CString strContents);
	// 中文说明：
	//   **功能：** 向 OPV 请求或下发机种/Model ID 信息，用于保证 PG/TP/OPV 的机种一致性。
	void OpvModelRequest(int Num, CString strModelID);
	// 中文说明：
	//   **功能：** 触发 PC 与 OPV 之间的时间同步请求，以保证 DFS/履历时间戳一致。
	void OpvPcTimeRequest(int Num);

	// 中文说明：
	//   **功能：** 将当前 Panel 的缺陷数量、等级等统计结果保存到指定介质（文件/DB）。
	void OpvDefectCountSave();

	vector <ULD_SDFSDefectDataBegin2> m_ULD_DefectDataList2;

	std::vector<CString> m_lastContent;
	std::vector<CString> m_lastResult;
	std::vector<CString> m_lastCommand;
	std::vector<CString> m_lastRequest;

	CString GetLastContents(int iChNum) { return m_lastContent[iChNum]; }
	CString GetLastResult(int iChNum) { return m_lastResult[iChNum]; }
	CString GetLastCommand(int iChNum) { return m_lastCommand[iChNum]; }
	CString GetLastRequest(int iChNum) { return m_lastRequest[iChNum]; }
	CCriticalSection m_csSocketSend;
	CCriticalSection m_csData;
	CCriticalSection m_csOpvMsg;

	int m_OpvCheckCount;
	int m_iOpvNum;
	CString m_strOPID;
};
#endif