#pragma once
///////////////////////////////////////////////////////////////////////////////
// FILE : ICWProtocol.h
// ICW通信协议数据结构与解析器
// 基于: 点灯检软件与ICW通信接口变更设计V1.1.docx
///////////////////////////////////////////////////////////////////////////////

#ifndef _ICW_PROTOCOL_H_
#define _ICW_PROTOCOL_H_

#include <vector>
#include <map>
#include <afxstr.h>

// 报文分隔符
#define ICW_MSG_DELIMITER       "@"
#define ICW_MSG_SNAPFN          "SnapFN@"
#define ICW_MSG_HEARTBEAT       "HeartBeat@"
#define ICW_MSG_GETVERSION      "GetVersion@"

// Action类型
#define ICW_ACTION_START        "Start"
#define ICW_ACTION_JUDGE        "Judge"

// 旧版协议关键字 (兼容)
#define ICW_LEGACY_MSG_START_PREFIX   "Start$"
#define ICW_LEGACY_MSG_RUNNING        "Running@"
// 旧版结束报文前缀: FN$xxxx...@ (xxxx按工位顺序的2位结果码拼接)
#define ICW_LEGACY_MSG_FINISH_PREFIX  "FN$"

// 旧版检测结束信息
struct ICW_LegacyFinishInfo
{
    // One entry per 2-digit field in FN$ payload, in station order (fixture 1, 2, 3, 4...).
    // 0 = that slot not finished (00); non-zero = that slot finished (01/02/03/04 = completion codes, not OK/NG).
    std::vector<int> Results;
    CString RawMessage;
};

// 检测结果
#define ICW_RESULT_OK           1
#define ICW_RESULT_NG           2
#define ICW_RESULT_ERROR        3   // 异常(点灯异常/超时等)

///////////////////////////////////////////////////////////////////////////////
// 产品信息 (ICW -> 主检)
///////////////////////////////////////////////////////////////////////////////
struct ICW_Product
{
    int Position;           // 在治具的位置 (1,2,3,4)
    CString Barcode;        // 产品二维码
    CString UniqueId;       // 本次检测唯一码(ICW生成)
    
    ICW_Product() : Position(0) {}
};

///////////////////////////////////////////////////////////////////////////////
// 开始检测信息 (ICW -> 主检)
///////////////////////////////////////////////////////////////////////////////
struct ICW_StartInfo
{
    CString Action;                     // "Start" 或 "Judge"
    int MaxProductCountPerJig;          // 每个治具最多产品数
    int JigNumber;                      // 治具号
    std::vector<ICW_Product> Products;  // 产品列表
    
    ICW_StartInfo() : MaxProductCountPerJig(0), JigNumber(0) {}
    
    // 是否为开始检测
    BOOL IsStart() const { return Action.CompareNoCase(_T("Start")) == 0; }
    // 是否为需要复核
    BOOL IsJudge() const { return Action.CompareNoCase(_T("Judge")) == 0; }
};

///////////////////////////////////////////////////////////////////////////////
// 产品检测结果 (主检 -> ICW)
///////////////////////////////////////////////////////////////////////////////
struct ICW_ProductResult
{
    int Position;       // 在治具的位置 (1,2,3,4)
    int Result;         // 1=OK, 2=NG, 3=异常
    
    ICW_ProductResult() : Position(0), Result(0) {}
    ICW_ProductResult(int pos, int res) : Position(pos), Result(res) {}
};

///////////////////////////////////////////////////////////////////////////////
// 检测结束信息 (主检 -> ICW)
///////////////////////////////////////////////////////////////////////////////
struct ICW_FinishInfo
{
    CString Action;                             // "Start" 或 "Judge"
    std::vector<ICW_ProductResult> ProducResults;   // 产品结果列表
    
    ICW_FinishInfo() {}
};

///////////////////////////////////////////////////////////////////////////////
// 版本信息 (主检 -> ICW)
///////////////////////////////////////////////////////////////////////////////
struct ICW_VersionInfo
{
    CString AoiVersion;         // 点灯检软件版本
    CString CurrentRecipe;      // 当前模板名称
    CString RecipeVersion;      // 当前模板版本
    CString JudgeGrade;         // 出货等级
    
    ICW_VersionInfo() {}
};

///////////////////////////////////////////////////////////////////////////////
// ICW协议解析器
///////////////////////////////////////////////////////////////////////////////
class CICWProtocol
{
public:
    CICWProtocol();
    virtual ~CICWProtocol();
    
    // ===== 消息解析 =====
    
    // 解析开始检测JSON
    static BOOL ParseStartInfo(const CString& strJson, ICW_StartInfo& info);

    // 解析旧版开始检测报文: Start$... (字段格式以现场为准，这里做容错解析)
    static BOOL ParseLegacyStartInfo(const CString& strMsg, ICW_StartInfo& info);

    // 解析旧版检测结束报文: FN$xxxx@ (xxxx按工位顺序的2位结果码拼接)
    static BOOL ParseLegacyFinishFN(const CString& strMsg, ICW_LegacyFinishInfo& info);

    // 判断消息类型
    static BOOL IsSnapFN(const CString& strMsg);
    static BOOL IsLegacyFinishFN(const CString& strMsg);
    static BOOL IsHeartBeat(const CString& strMsg);
    static BOOL IsGetVersion(const CString& strMsg);
    static BOOL IsJsonMessage(const CString& strMsg);
    static BOOL IsLegacyStart(const CString& strMsg);
    
    // ===== 消息序列化 =====
    
    // 序列化检测结束信息
    static CString SerializeFinishInfo(const ICW_FinishInfo& info);
    
    // 序列化版本信息
    static CString SerializeVersionInfo(const ICW_VersionInfo& info);
    
    // 生成SnapFN消息
    static CString MakeSnapFNMessage();
    
    // 生成HeartBeat响应
    static CString MakeHeartBeatResponse();

    // 生成旧版Running消息
    static CString MakeLegacyRunningMessage();

    // 序列化旧版检测结束信息: FN$... (不含@，由SendMessage统一补@)
    // maxPositions: 默认4工位；若 info.ProducResults 里出现更大 Position，会自动扩展
    static CString SerializeLegacyFinishFN(const ICW_FinishInfo& info, int maxPositions = 4);
    
    // ===== 工具方法 =====
    
    // 分割接收缓冲区中的消息 (以@为分隔符)
    static int SplitMessages(const CString& strBuffer, std::vector<CString>& messages, CString& remaining);
    
private:
    // JSON解析辅助方法
    static CString ExtractJsonValue(const CString& strJson, const CString& strKey);
    static int ExtractJsonInt(const CString& strJson, const CString& strKey);
    static std::vector<CString> ExtractJsonArray(const CString& strJson, const CString& strArrayKey);
};

#endif // _ICW_PROTOCOL_H_

