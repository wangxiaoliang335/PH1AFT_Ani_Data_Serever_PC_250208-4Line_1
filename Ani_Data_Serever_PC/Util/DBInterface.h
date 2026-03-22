#pragma once
///////////////////////////////////////////////////////////////////////////////
// FILE : DBInterface.h
// 数据库操作接口
// 用于检测结果和缺陷信息的数据库读写
// 使用 MySQL Connector/C++ 连接 MySQL 数据库
///////////////////////////////////////////////////////////////////////////////

#ifndef _DB_INTERFACE_H_
#define _DB_INTERFACE_H_

#include "DataModels.h"
#include <mysql/jdbc.h>

///////////////////////////////////////////////////////////////////////////////
// 数据库接口类
///////////////////////////////////////////////////////////////////////////////
class CDBInterface
{
public:
    CDBInterface();
    virtual ~CDBInterface();

    // ===== 连接管理 =====

    // 连接数据库
    // strConnString: MySQL连接字符串，格式如 "tcp://localhost:3306/database"
    // 或者 "tcp://127.0.0.1:3306/ivs_lcd?user=root&password=123456"
    BOOL Connect(const CString& strConnString);

    // 断开连接
    void Disconnect();

    // 是否已连接
    BOOL IsConnected() const { return m_bConnected; }

    // ===== 检测结果操作 =====

    // 插入检测结果
    BOOL InsertInspectionResult(const CInspectionResult& result);

    // 更新检测结果
    BOOL UpdateInspectionResult(const CInspectionResult& result);

    // 更新人工复判结果
    BOOL UpdateManualReviewResult(const CString& strGUID,
                                   const CString& strResult,
                                   const CString& strCode,
                                   const CString& strGrade,
                                   const CString& strOperator);

    // 更新自动复检结果
    BOOL UpdateAutoReviewResult(const CString& strGUID,
                                 const CString& strResult,
                                 const CString& strCode,
                                 const CString& strGrade);

    // 按GUID查询
    BOOL QueryByGUID(const CString& strGUID, CInspectionResult& result);

    // 按屏二维码查询
    BOOL QueryByScreenID(const CString& strScreenID, CInspectionResultList& results);

    // 按屏二维码查询缺陷码 (用于SetLoadResultCode)
    // strScreenID: 屏二维码(FpcID)
    // strCode: 输出缺陷码
    // strGrade: 输出等级
    // 返回: 是否查询成功
    BOOL QueryDefectCodeByScreenID(const CString& strScreenID, CString& strCode, CString& strGrade);

    // 按UniqueID查询
    BOOL QueryByUniqueID(const CString& strUniqueID, CInspectionResult& result);

    // 按UniqueID查询检测结果并转换为DFS数据格式（用于FTP上传）
    // 返回的 DfsDataValue 可直接传给 CDFSClient::AddTransferFile 或 DfsAddTransferFile
    BOOL QueryInspectionResultForDFS(const CString& strUniqueID, DfsDataValue& dfsData);

    // 按日期范围查询
    BOOL QueryByDateRange(const COleDateTime& dtStart,
                          const COleDateTime& dtEnd,
                          CInspectionResultList& results);

    // ===== 缺陷操作 =====

    // 插入缺陷记录
    BOOL InsertDefectInfo(const CDefectInfo& defect);

    // 批量插入缺陷记录
    BOOL InsertDefectInfoBatch(const CDefectInfoList& defects);

    // 更新缺陷复判结果
    BOOL UpdateDefectReviewResult(int nSysID,
                                   const CString& strResult,
                                   const CString& strCode,
                                   const CString& strGrade);

    // 查询指定屏的所有缺陷
    BOOL QueryDefectsByParentGUID(const CString& strParentGUID, CDefectInfoList& defects);

    // 删除指定屏的所有缺陷
    BOOL DeleteDefectsByParentGUID(const CString& strParentGUID);

    // ===== 统计查询 =====

    // 按日期统计检测数量
    struct DailyStatistics
    {
        CString Date;
        int TotalCount;
        int OKCount;
        int NGCount;
    };
    BOOL GetDailyStatistics(const COleDateTime& dtStart,
                            const COleDateTime& dtEnd,
                            std::vector<DailyStatistics>& stats);

    // 获取缺陷类型分布
    struct DefectTypeCount
    {
        CString Type;
        int Count;
    };
    BOOL GetDefectTypeDistribution(const CString& strParentGUID,
                                    std::vector<DefectTypeCount>& distribution);

    // ===== 工具方法 =====

    // 获取最后错误信息
    CString GetLastError() const { return m_strLastError; }

    // 生成新GUID
    static CString GenerateGUID();

    // 生成检测唯一ID（用于 ivs_lcd_idmap，格式 YYYY_MM_DD_HH_MM_SS_fff_JJ，保证不重复）
    // jigNum: 治具号 0~3 对应 "01"~"04"
    static CString GenerateUniqueIDForJig(int jigNum);

    // 发送开始检测前更新 ivs_lcd_idmap（供检测软件使用）
    // markID/mainAoiFixID: 治具号 "01"~"04", posID: 位置号 0~3, uniqueID: 不重复唯一ID, barcode: 产品码
    BOOL UpsertIDMapBeforeStart(const CString& markID, int posID, const CString& uniqueID,
                                const CString& barcode, const CString& mainAoiFixID);

    // 根据 PanelID/Barcode 查询 ivs_lcd_idmap 获取 UniqueID
    BOOL QueryIDMapByPanelID(const CString& strPanelID, CIDMapInfo& idMapInfo);

    // 转义SQL字符串
    static CString EscapeString(const CString& str);

protected:
    // 执行SQL语句
    BOOL ExecuteSQL(const CString& strSQL);

    // 获取当前时间SQL函数
    CString GetNowFunctionSQL() const;

    // 生成按UniqueID查询的SQL
    CString GetSelectLatestByUniqueIDSQL(const CString& strUniqueID) const;

    // 插入或更新ID映射
    BOOL UpsertIDMap(const CInspectionResult& result);

    // 插入缺陷到指定表
    BOOL InsertDefectToTable(const CString& strTableName, const CDefectInfo& defect);

    // 从指定表查询缺陷
    BOOL QueryDefectsByParentGUIDFromTable(const CString& strTableName, const CString& strParentGUID, CDefectInfoList& defects);

    // 从指定表删除缺陷
    BOOL DeleteDefectsByParentGUIDFromTable(const CString& strTableName, const CString& strParentGUID);

private:
    sql::Connection* m_pConnection;
    BOOL m_bConnected;
    CString m_strLastError;
    CRITICAL_SECTION m_csDB;    // 数据库操作锁
};

///////////////////////////////////////////////////////////////////////////////
// 全局数据库接口实例
///////////////////////////////////////////////////////////////////////////////
CDBInterface& GetDBInterface();

#endif // _DB_INTERFACE_H_
