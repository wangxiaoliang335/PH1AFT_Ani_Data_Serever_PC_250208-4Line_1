#pragma once
///////////////////////////////////////////////////////////////////////////////
// FILE : DBInterface.h
// Database operations interface
// Used for reading and writing inspection results and defect information
// Uses ODBC (MySQL ODBC 5.3 Driver) to connect to MySQL database
// Architecture: Thread Local Storage (TLS) - each thread has independent connection
///////////////////////////////////////////////////////////////////////////////

#ifndef _DB_INTERFACE_H_
#define _DB_INTERFACE_H_

#include "DataModels.h"
#include <sql.h>
#include <sqlext.h>

///////////////////////////////////////////////////////////////////////////////
// Database interface class
///////////////////////////////////////////////////////////////////////////////
class CDBInterface
{
public:
    CDBInterface();
    virtual ~CDBInterface();

    // ===== Connection Management =====

    // Connect to database
    // strConnString: ODBC connection string, format like "DRIVER={MySQL ODBC 5.3 Driver};SERVER=localhost;PORT=3306;DATABASE=ivs_lcd;USER=root;PASSWORD=123456;"
    BOOL Connect(const CString& strConnString);

    // Disconnect from database
    void Disconnect();

    // Check if connected
    BOOL IsConnected() const { return m_bConnected; }

    // ===== Inspection Result Operations =====

    // Insert inspection result
    BOOL InsertInspectionResult(const CInspectionResult& result);

    // Update inspection result
    BOOL UpdateInspectionResult(const CInspectionResult& result);

    // Update manual review result
    BOOL UpdateManualReviewResult(const CString& strGUID,
                                   const CString& strResult,
                                   const CString& strCode,
                                   const CString& strGrade,
                                   const CString& strOperator);

    // Update auto review result
    BOOL UpdateAutoReviewResult(const CString& strGUID,
                                 const CString& strResult,
                                 const CString& strCode,
                                 const CString& strGrade);

    // Query by GUID
    BOOL QueryByGUID(const CString& strGUID, CInspectionResult& result);

    // Query by panel barcode (using Barcode field)
    BOOL QueryByBarcode(const CString& strBarcode, CInspectionResultList& results);

    // Query defect code by panel barcode (for SetLoadResultCode)
    // strBarcode: Panel barcode (Barcode)
    // strCode: Output defect code
    // strGrade: Output grade
    // Returns: Whether query was successful
    BOOL QueryDefectCodeByBarcode(const CString& strBarcode, CString& strCode, CString& strGrade);

    // Query by UniqueID
    BOOL QueryByUniqueID(const CString& strUniqueID, CInspectionResult& result);

    // Query inspection result by UniqueID and convert to DFS data format (for FTP upload)
    // Returned DfsDataValue can be directly passed to CDFSClient::AddTransferFile or DfsAddTransferFile
    BOOL QueryInspectionResultForDFS(const CString& strUniqueID, DfsDataValue& dfsData);

    // Query by date range
    BOOL QueryByDateRange(const COleDateTime& dtStart,
                          const COleDateTime& dtEnd,
                          CInspectionResultList& results);

    // ===== Defect Operations =====

    // Insert defect record
    BOOL InsertDefectInfo(const CDefectInfo& defect);

    // Batch insert defect records
    BOOL InsertDefectInfoBatch(const CDefectInfoList& defects);

    // Update defect review result
    BOOL UpdateDefectReviewResult(int nSysID,
                                   const CString& strResult,
                                   const CString& strCode,
                                   const CString& strGrade);

    // Query all defects for specified panel
    BOOL QueryDefectsByParentGUID(const CString& strParentGUID, CDefectInfoList& defects);

    // Delete all defects for specified panel
    BOOL DeleteDefectsByParentGUID(const CString& strParentGUID);

    // ===== Statistics Queries =====

    // Query inspection count by date
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

    // Get defect type distribution
    struct DefectTypeCount
    {
        CString Type;
        int Count;
    };
    BOOL GetDefectTypeDistribution(const CString& strParentGUID,
                                    std::vector<DefectTypeCount>& distribution);

    // ===== Utility Methods =====

    // Get last error message
    CString GetLastError() const { return m_strLastError; }

    // Generate new GUID
    static CString GenerateGUID();

    // Generate unique inspection ID (for ivs_lcd_idmap, format YYYY_MM_DD_HH_MM_SS_fff_JJ, guaranteed unique)
    // jigNum: Jig number 0~3 corresponding to "01"~"04"
    static CString GenerateUniqueIDForJig(int jigNum);

    // Update ivs_lcd_idmap before inspection starts (for inspection software)
    // markID/mainAoiFixID: Jig number "01"~"04", posID: Position number 0~3, uniqueID: Unique ID, barcode: Product code
    BOOL UpsertIDMapBeforeStart(const CString& markID, int posID, const CString& uniqueID,
                                const CString& barcode, const CString& mainAoiFixID);

    // UPDATE IVS_LCD_IDMap based on Start$ prefix jig pattern (each jig generates new GUID)
    // Example "01020304" updates MainAoiFixID 1~4 for 4 records; "01020000" only updates 1, 2. Used before AUTO_TEST / sending Start$.
    BOOL UpdateIDMapForStartPattern(const CString& strCurrentJigs);

    // Query ivs_lcd_idmap by MainAoiFixID (jig number 1~4) to get UniqueID/Barcode
    BOOL QueryIDMapByFixtureNo(int nFixtureNo, CIDMapInfo& idMapInfo);

    // Query ivs_lcd_idmap by PanelID/Barcode to get UniqueID
    BOOL QueryIDMapByPanelID(const CString& strPanelID, CIDMapInfo& idMapInfo);

    // Escape SQL string
    static CString EscapeString(const CString& str);

protected:
    // Ensure thread has a valid connection (call before each DB operation)
    BOOL EnsureThreadConnection();

    // Execute SQL statement
    BOOL ExecuteSQL(const CString& strSQL);

    // Execute SQL query and return result set (caller must call SQLFreeStmt)
    BOOL ExecuteQuery(const CString& strSQL, SQLHSTMT& hStmt);

    // Get current time SQL function
    CString GetNowFunctionSQL() const;

    // Generate SQL for querying by UniqueID
    CString GetSelectLatestByUniqueIDSQL(const CString& strUniqueID) const;

    // Insert or update ID mapping
    BOOL UpsertIDMap(const CInspectionResult& result);

    // Insert defect to specified table
    BOOL InsertDefectToTable(const CString& strTableName, const CDefectInfo& defect);

    // Query defects from specified table
    BOOL QueryDefectsByParentGUIDFromTable(const CString& strTableName, const CString& strParentGUID, CDefectInfoList& defects);

    // Delete defects from specified table
    BOOL DeleteDefectsByParentGUIDFromTable(const CString& strTableName, const CString& strParentGUID);

    // Get ODBC error message
    CString GetODBCError(SQLSMALLINT hType, SQLHANDLE hHandle);

private:
    // ===== Thread Local Storage (TLS) Connection Management =====
    // Maintain independent database connection for each thread to avoid thread competition

    // Get database connection for current thread (thread-safe)
    SQLHENV GetThreadEnv();
    SQLHDBC GetThreadConnection();

    // Initialize environment and connection for current thread
    BOOL InitThreadConnection();

    // Release environment and connection for current thread
    void ReleaseThreadConnection();

    // Release all thread connections (call when shutting down)
    void ReleaseAllThreadConnections();

    // Thread local storage index
    static DWORD sm_nTlsIndex;

    // Main connection string (used to create thread connections)
    CString m_strMainConnString;

    // Main connection state (for backward compatibility with old interfaces)
    SQLHENV m_hEnv;
    SQLHDBC m_hConnection;
    BOOL m_bConnected;
    CString m_strLastError;
    CRITICAL_SECTION m_csDB;    // Database operation lock (protects main connection and config)
};

// Thread connection structure for TLS
struct ThreadDBConnection
{
    SQLHENV hEnv;
    SQLHDBC hConnection;
    BOOL bConnected;
    CString strLastError;

    ThreadDBConnection() : hEnv(SQL_NULL_HENV), hConnection(SQL_NULL_HDBC), bConnected(FALSE) {}
};

///////////////////////////////////////////////////////////////////////////////
// Global database interface instance
///////////////////////////////////////////////////////////////////////////////
CDBInterface& GetDBInterface();

#endif // _DB_INTERFACE_H_
