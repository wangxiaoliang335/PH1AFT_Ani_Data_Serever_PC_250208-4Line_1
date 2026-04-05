///////////////////////////////////////////////////////////////////////////////
// FILE : ICWProtocol.cpp
// ICW通信协议解析与序列化实现
// 基于: 点灯检软件与ICW通信接口变更设计V1.1.docx
///////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "ICWProtocol.h"

///////////////////////////////////////////////////////////////////////////////
// 构造/析构
///////////////////////////////////////////////////////////////////////////////
CICWProtocol::CICWProtocol()
{
}

CICWProtocol::~CICWProtocol()
{
}

///////////////////////////////////////////////////////////////////////////////
// 消息类型判断
///////////////////////////////////////////////////////////////////////////////
BOOL CICWProtocol::IsSnapFN(const CString& strMsg)
{
    return strMsg.CompareNoCase(_T("SnapFN")) == 0 ||
           strMsg.CompareNoCase(_T("SnapFN@")) == 0;
}

///////////////////////////////////////////////////////////////////////////////
// 判断旧版检测结束报文: FN$xxxx...@
///////////////////////////////////////////////////////////////////////////////
BOOL CICWProtocol::IsLegacyFinishFN(const CString& strMsg)
{
    CString s = strMsg;
    s.Trim();
    return s.GetLength() >= 3 && s.Left(3).CompareNoCase(_T("FN$")) == 0;
}

BOOL CICWProtocol::IsHeartBeat(const CString& strMsg)
{
    return strMsg.CompareNoCase(_T("HeartBeat")) == 0 || 
           strMsg.CompareNoCase(_T("HeartBeat@")) == 0;
}

BOOL CICWProtocol::IsGetVersion(const CString& strMsg)
{
    return strMsg.CompareNoCase(_T("GetVersion")) == 0 || 
           strMsg.CompareNoCase(_T("GetVersion@")) == 0;
}

BOOL CICWProtocol::IsJsonMessage(const CString& strMsg)
{
    CString strTrim = strMsg;
    strTrim.Trim();
    return strTrim.GetLength() > 0 && strTrim[0] == _T('{');
}

BOOL CICWProtocol::IsLegacyStart(const CString& strMsg)
{
    CString s = strMsg;
    s.Trim();
    // 兼容: "Start$..." 或 "start$..."
    return s.GetLength() >= 6 && s.Left(6).CompareNoCase(_T("Start$")) == 0;
}

///////////////////////////////////////////////////////////////////////////////
// JSON解析辅助方法 (简易实现，无需外部库)
///////////////////////////////////////////////////////////////////////////////
CString CICWProtocol::ExtractJsonValue(const CString& strJson, const CString& strKey)
{
    CString strSearch;
    strSearch.Format(_T("\"%s\""), strKey);
    
    int nPos = strJson.Find(strSearch);
    if (nPos == -1) return _T("");
    
    // 找到冒号
    nPos = strJson.Find(_T(':'), nPos + strSearch.GetLength());
    if (nPos == -1) return _T("");
    
    nPos++; // 跳过冒号
    
    // 跳过空白
    while (nPos < strJson.GetLength() && (strJson[nPos] == _T(' ') || strJson[nPos] == _T('\t')))
        nPos++;
    
    if (nPos >= strJson.GetLength()) return _T("");
    
    CString strValue;
    TCHAR chStart = strJson[nPos];
    
    if (chStart == _T('"'))
    {
        // 字符串值
        nPos++; // 跳过开始引号
        int nEnd = strJson.Find(_T('"'), nPos);
        if (nEnd != -1)
            strValue = strJson.Mid(nPos, nEnd - nPos);
    }
    else if (chStart == _T('['))
    {
        // 数组 - 返回整个数组内容包括括号
        int nDepth = 1;
        int nStart = nPos;
        nPos++;
        while (nPos < strJson.GetLength() && nDepth > 0)
        {
            if (strJson[nPos] == _T('[')) nDepth++;
            else if (strJson[nPos] == _T(']')) nDepth--;
            nPos++;
        }
        strValue = strJson.Mid(nStart, nPos - nStart);
    }
    else if (chStart == _T('{'))
    {
        // 对象
        int nDepth = 1;
        int nStart = nPos;
        nPos++;
        while (nPos < strJson.GetLength() && nDepth > 0)
        {
            if (strJson[nPos] == _T('{')) nDepth++;
            else if (strJson[nPos] == _T('}')) nDepth--;
            nPos++;
        }
        strValue = strJson.Mid(nStart, nPos - nStart);
    }
    else
    {
        // 数字或bool值
        int nEnd = nPos;
        while (nEnd < strJson.GetLength())
        {
            TCHAR ch = strJson[nEnd];
            if (ch == _T(',') || ch == _T('}') || ch == _T(']') || ch == _T('\r') || ch == _T('\n'))
                break;
            nEnd++;
        }
        strValue = strJson.Mid(nPos, nEnd - nPos);
        strValue.Trim();
    }
    
    return strValue;
}

int CICWProtocol::ExtractJsonInt(const CString& strJson, const CString& strKey)
{
    CString strValue = ExtractJsonValue(strJson, strKey);
    if (strValue.IsEmpty()) return 0;
    return _ttoi(strValue);
}

std::vector<CString> CICWProtocol::ExtractJsonArray(const CString& strJson, const CString& strArrayKey)
{
    std::vector<CString> items;
    
    CString strArray = ExtractJsonValue(strJson, strArrayKey);
    if (strArray.IsEmpty() || strArray[0] != _T('['))
        return items;
    
    // 解析数组中的每个对象
    int nPos = 1; // 跳过 '['
    while (nPos < strArray.GetLength())
    {
        // 找到对象开始
        int nObjStart = strArray.Find(_T('{'), nPos);
        if (nObjStart == -1) break;
        
        // 找到对象结束
        int nDepth = 1;
        int nObjEnd = nObjStart + 1;
        while (nObjEnd < strArray.GetLength() && nDepth > 0)
        {
            if (strArray[nObjEnd] == _T('{')) nDepth++;
            else if (strArray[nObjEnd] == _T('}')) nDepth--;
            nObjEnd++;
        }
        
        CString strObj = strArray.Mid(nObjStart, nObjEnd - nObjStart);
        items.push_back(strObj);
        nPos = nObjEnd;
    }
    
    return items;
}

///////////////////////////////////////////////////////////////////////////////
// 解析开始检测JSON
///////////////////////////////////////////////////////////////////////////////
BOOL CICWProtocol::ParseStartInfo(const CString& strJson, ICW_StartInfo& info)
{
    if (!IsJsonMessage(strJson))
        return FALSE;
    
    // 解析基本字段
    info.Action = ExtractJsonValue(strJson, _T("Action"));
    info.MaxProductCountPerJig = ExtractJsonInt(strJson, _T("MaxProductCountPerJig"));
    info.JigNumber = ExtractJsonInt(strJson, _T("JigNumber"));
    
    // 解析Products数组
    std::vector<CString> productObjs = ExtractJsonArray(strJson, _T("Products"));
    for (size_t i = 0; i < productObjs.size(); i++)
    {
        ICW_Product product;
        product.Position = ExtractJsonInt(productObjs[i], _T("Position"));
        product.Barcode = ExtractJsonValue(productObjs[i], _T("Barcode"));
        product.UniqueId = ExtractJsonValue(productObjs[i], _T("UniqueId"));
        info.Products.push_back(product);
    }
    
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// 解析旧版开始检测报文 (Start$...)
// 说明:
// - 旧协议的字段在不同项目/版本可能有差异，这里采用“尽量解析”的策略:
//   - 以 '$' 分割 token
//   - token 中如果是纯数字且在合理范围(1..64)，可能是 JigNumber/MaxCount/Position
//   - 其余非空 token 当作条码(Barcode)
//   - Position 默认按条码出现顺序 1..N
///////////////////////////////////////////////////////////////////////////////
BOOL CICWProtocol::ParseLegacyStartInfo(const CString& strMsg, ICW_StartInfo& info)
{
    if (!IsLegacyStart(strMsg))
        return FALSE;

    info = ICW_StartInfo();
    info.Action = _T(ICW_ACTION_START);

    CString s = strMsg;
    s.Trim();

    // 分割
    std::vector<CString> tokens;
    int cur = 0;
    while (cur <= s.GetLength())
    {
        int next = s.Find(_T('$'), cur);
        CString t;
        if (next == -1)
        {
            t = s.Mid(cur);
            cur = s.GetLength() + 1;
        }
        else
        {
            t = s.Mid(cur, next - cur);
            cur = next + 1;
        }
        t.Trim();
        tokens.push_back(t);
    }

    // 诊断日志：打印拆分后的token，便于现场确认旧协议字段格式
    {
        CString dbg;
        dbg.Format(_T("ICWProtocol: Legacy Start tokens=%d, raw=%s\n"), (int)tokens.size(), s);
        TRACE(dbg);
        for (size_t i = 0; i < tokens.size(); i++)
        {
            CString line;
            line.Format(_T("ICWProtocol:   token[%d]=%s\n"), (int)i, tokens[i]);
            TRACE(line);
        }
    }

    // tokens[0] 应为 "Start"
    // 从 tokens[1] 开始尝试提取数字字段与条码字段
    int inferredPos = 1;
    for (size_t i = 1; i < tokens.size(); i++)
    {
        CString t = tokens[i];
        if (t.IsEmpty())
            continue;

        // 是否纯数字
        bool isNumber = true;
        for (int k = 0; k < t.GetLength(); k++)
        {
            if (t[k] < _T('0') || t[k] > _T('9'))
            {
                isNumber = false;
                break;
            }
        }

        if (isNumber)
        {
            int v = _ttoi(t);
            // 很多现场会把 JigNumber/MaxProductCount 放在前面；这里按“先填未填的”策略
            if (info.JigNumber == 0 && v >= 1 && v <= 64)
            {
                info.JigNumber = v;
                TRACE(_T("ICWProtocol: Legacy inferred JigNumber=%d (from token=%s)\n"), v, t);
                continue;
            }
            if (info.MaxProductCountPerJig == 0 && v >= 1 && v <= 64)
            {
                info.MaxProductCountPerJig = v;
                TRACE(_T("ICWProtocol: Legacy inferred MaxProductCountPerJig=%d (from token=%s)\n"), v, t);
                continue;
            }
            // 如果出现像 1/2/3/4 这种，可能是 Position；但旧协议常常不带Position，这里不强行绑定
        }

        // 其余作为条码
        ICW_Product p;
        p.Position = inferredPos++;
        p.Barcode = t;
        p.UniqueId = _T("");
        info.Products.push_back(p);
    }

    // 如果未提供 MaxProductCount，则用解析到的条码数兜底
    if (info.MaxProductCountPerJig == 0)
        info.MaxProductCountPerJig = (int)info.Products.size();

    TRACE(_T("ICWProtocol: Legacy Start parsed: JigNumber=%d, MaxProductCountPerJig=%d, Products=%d\n"),
        info.JigNumber, info.MaxProductCountPerJig, (int)info.Products.size());

    return (info.Products.size() > 0);
}

///////////////////////////////////////////////////////////////////////////////
// 序列化检测结束信息
///////////////////////////////////////////////////////////////////////////////
CString CICWProtocol::SerializeFinishInfo(const ICW_FinishInfo& info)
{
    CString strJson;
    strJson.Format(_T("{\r\n  \"Action\": \"%s\",\r\n  \"ProducResults\": [\r\n"), 
        info.Action);
    
    for (size_t i = 0; i < info.ProducResults.size(); i++)
    {
        CString strItem;
        strItem.Format(_T("    {\r\n      \"Position\": %d,\r\n      \"Result\": %d\r\n    }"),
            info.ProducResults[i].Position,
            info.ProducResults[i].Result);
        
        strJson += strItem;
        if (i < info.ProducResults.size() - 1)
            strJson += _T(",");
        strJson += _T("\r\n");
    }
    
    strJson += _T("  ]\r\n}");
    
    return strJson;
}

///////////////////////////////////////////////////////////////////////////////
// 序列化版本信息
///////////////////////////////////////////////////////////////////////////////
CString CICWProtocol::SerializeVersionInfo(const ICW_VersionInfo& info)
{
    CString strJson;
    strJson.Format(
        _T("{\r\n")
        _T("  \"AoiVersion\": \"%s\",\r\n")
        _T("  \"CurrentRecipe\": \"%s\",\r\n")
        _T("  \"RecipeVersion\": \"%s\",\r\n")
        _T("  \"JudgeGrade\": \"%s\"\r\n")
        _T("}"),
        info.AoiVersion,
        info.CurrentRecipe,
        info.RecipeVersion,
        info.JudgeGrade);
    
    return strJson;
}

///////////////////////////////////////////////////////////////////////////////
// 生成SnapFN消息
///////////////////////////////////////////////////////////////////////////////
CString CICWProtocol::MakeSnapFNMessage()
{
    return CString(ICW_MSG_SNAPFN);
}

///////////////////////////////////////////////////////////////////////////////
// 生成HeartBeat响应
///////////////////////////////////////////////////////////////////////////////
CString CICWProtocol::MakeHeartBeatResponse()
{
    return CString(_T("HeartBeatAck@"));
}

CString CICWProtocol::MakeLegacyRunningMessage()
{
    return CString(_T("Running@"));
}

///////////////////////////////////////////////////////////////////////////////
// 序列化旧版检测结束信息: FN$01020304 (每工位2位结果码拼接)
// - Result=1(OK)    -> "01"
// - Result=2(NG)    -> "02"
// - Result=3(ERROR) -> "03"
// - 其它/缺省        -> "00"
///////////////////////////////////////////////////////////////////////////////
CString CICWProtocol::SerializeLegacyFinishFN(const ICW_FinishInfo& info, int maxPositions /*=4*/)
{
    int maxPos = maxPositions;
    for (size_t i = 0; i < info.ProducResults.size(); i++)
    {
        if (info.ProducResults[i].Position > maxPos)
            maxPos = info.ProducResults[i].Position;
    }
    if (maxPos <= 0) maxPos = maxPositions;

    // 初始化全部为00
    std::vector<CString> codes;
    codes.resize(maxPos);
    for (int i = 0; i < maxPos; i++) codes[i] = _T("00");

    for (size_t i = 0; i < info.ProducResults.size(); i++)
    {
        int pos = info.ProducResults[i].Position;
        if (pos <= 0 || pos > maxPos) continue;

        CString code = _T("00");
        switch (info.ProducResults[i].Result)
        {
        case ICW_RESULT_OK:    code = _T("01"); break;
        case ICW_RESULT_NG:    code = _T("02"); break;
        case ICW_RESULT_ERROR: code = _T("03"); break;
        default:               code = _T("00"); break;
        }
        codes[pos - 1] = code;
    }

    CString payload;
    for (int i = 0; i < maxPos; i++)
        payload += codes[i];

    return CString(_T("FN$")) + payload;
}

///////////////////////////////////////////////////////////////////////////////
// Parse legacy finish message: FN$xxxx@
// Payload is concatenated 2-digit fields per station in order (e.g. fixture 1..4).
// 00 = that station not completed; 01/02/03/04 = completion codes for finished stations
// (same digit meaning as Start$; not PLC OK/NG). Main OK/NG comes from DB AOIResult.
///////////////////////////////////////////////////////////////////////////////
BOOL CICWProtocol::ParseLegacyFinishFN(const CString& strMsg, ICW_LegacyFinishInfo& info)
{
    if (!IsLegacyFinishFN(strMsg))
        return FALSE;

    info = ICW_LegacyFinishInfo();
    info.RawMessage = strMsg;

    CString payload = strMsg;
    payload.Trim();
    if (payload.Right(1) == _T("@"))
        payload = payload.Left(payload.GetLength() - 1);
    if (payload.Left(3).CompareNoCase(_T("FN$")) == 0)
        payload = payload.Mid(3);

    int nLen = payload.GetLength();
    for (int i = 0; i < nLen; i += 2)
    {
        CString code = payload.Mid(i, 2);
        int slotCode = _ttoi(code);
        info.Results.push_back(slotCode);  // keep 0 so index matches fixture slot
    }

    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// 分割接收缓冲区中的消息 (以@为分隔符)
// 返回: 完整消息数量
// remaining: 未完成的消息内容(没有分隔符的部分)
///////////////////////////////////////////////////////////////////////////////
int CICWProtocol::SplitMessages(const CString& strBuffer, std::vector<CString>& messages, CString& remaining)
{
    messages.clear();
    remaining.Empty();
    
    CString strTemp = strBuffer;
    int nPos = 0;
    
    while (TRUE)
    {
        int nDelimiter = strTemp.Find(_T('@'), nPos);
        if (nDelimiter == -1)
        {
            // 没有找到分隔符，剩余部分保留
            remaining = strTemp.Mid(nPos);
            break;
        }
        
        // 提取完整消息 (不包括@)
        CString strMsg = strTemp.Mid(nPos, nDelimiter - nPos);
        strMsg.Trim();
        
        if (!strMsg.IsEmpty())
        {
            messages.push_back(strMsg);
        }
        
        nPos = nDelimiter + 1; // 跳过@
    }
    
    return (int)messages.size();
}

