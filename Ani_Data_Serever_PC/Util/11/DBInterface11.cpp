///////////////////////////////////////////////////////////////////////////////
// FILE : DBInterface.cpp
// Database operations interface implementation
// Uses ODBC (MySQL ODBC 5.3 Driver) to connect to MySQL database
// Thread Local Storage (TLS): Each thread has independent database connection
///////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "DBInterface.h"
#include "DataModels.h"
#include "Migration.h"
#include "Ani_Data_Serever_PC.h"
#include <objbase.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

///////////////////////////////////////////////////////////////////////////////
// TLS Connection Wrapper Structure
// Each thread has an independent database connection
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// TLS Index Initialization
///////////////////////////////////////////////////////////////////////////////
DWORD CDBInterface::sm_nTlsIndex = TLS_OUT_OF_INDEXES;

///////////////////////////////////////////////////////////////////////////////
// Global Instance
///////////////////////////////////////////////////////////////////////////////
static CDBInterface g_DBInterface;

CDBInterface& GetDBInterface()
{
    return g_DBInterface;
}

///////////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
///////////////////////////////////////////////////////////////////////////////
CDBInterface::CDBInterface()
    : m_bConnected(FALSE)
    , m_hEnv(SQL_NULL_HENV)
    , m_hConnection(SQL_NULL_HDBC)
    , m_strMainConnString(_T(""))
{
    InitializeCriticalSection(&m_csDB);

    // Initialize TLS index (only first time)
    if (sm_nTlsIndex == TLS_OUT_OF_INDEXES)
    {
        sm_nTlsIndex = TlsAlloc();
        TRACE(_T("DBInterface: TLS index allocated = %d\n"), sm_nTlsIndex);
    }
}

CDBInterface::~CDBInterface()
{
    Disconnect();

    // Release current thread's TLS connection (if exists)
    ReleaseThreadConnection();

    // Release TLS index
    if (sm_nTlsIndex != TLS_OUT_OF_INDEXES)
    {
        TlsFree(sm_nTlsIndex);
        sm_nTlsIndex = TLS_OUT_OF_INDEXES;
        TRACE(_T("DBInterface: TLS index freed\n"));
    }

    DeleteCriticalSection(&m_csDB);
}

///////////////////////////////////////////////////////////////////////////////
// TLS: Get ODBC environment for current thread
///////////////////////////////////////////////////////////////////////////////
SQLHENV CDBInterface::GetThreadEnv()
{
    if (sm_nTlsIndex == TLS_OUT_OF_INDEXES)
    {
        TRACE(_T("DBInterface::GetThreadEnv - TLS not initialized\n"));
        return SQL_NULL_HENV;
    }

    ThreadDBConnection* pThreadDB = static_cast<ThreadDBConnection*>(TlsGetValue(sm_nTlsIndex));
    if (!pThreadDB)
    {
        TRACE(_T("DBInterface::GetThreadEnv - No connection for current thread\n"));
        return SQL_NULL_HENV;
    }

    return pThreadDB->hEnv;
}

///////////////////////////////////////////////////////////////////////////////
// TLS: Get ODBC connection for current thread
///////////////////////////////////////////////////////////////////////////////
SQLHDBC CDBInterface::GetThreadConnection()
{
    if (sm_nTlsIndex == TLS_OUT_OF_INDEXES)
    {
        TRACE(_T("DBInterface::GetThreadConnection - TLS not initialized\n"));
        return SQL_NULL_HDBC;
    }

    ThreadDBConnection* pThreadDB = static_cast<ThreadDBConnection*>(TlsGetValue(sm_nTlsIndex));
    if (!pThreadDB)
    {
        TRACE(_T("DBInterface::GetThreadConnection - No connection for current thread\n"));
        return SQL_NULL_HDBC;
    }

    if (!pThreadDB->bConnected)
    {
        TRACE(_T("DBInterface::GetThreadConnection - Thread connection not connected\n"));
        return SQL_NULL_HDBC;
    }

    return pThreadDB->hConnection;
}

///////////////////////////////////////////////////////////////////////////////
// TLS: Initialize ODBC environment and connection for current thread
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::InitThreadConnection()
{
    if (sm_nTlsIndex == TLS_OUT_OF_INDEXES)
    {
        TRACE(_T("DBInterface::InitThreadConnection - TLS not initialized\n"));
        return FALSE;
    }

    // Check if connection already exists
    ThreadDBConnection* pThreadDB = static_cast<ThreadDBConnection*>(TlsGetValue(sm_nTlsIndex));
    if (pThreadDB && pThreadDB->bConnected)
    {
        TRACE(_T("DBInterface::InitThreadConnection - Thread already has connection\n"));
        return TRUE;
    }

    // Get main connection string (protected by critical section)
    CString strConnString;
    {
        EnterCriticalSection(&m_csDB);
        if (m_strMainConnString.IsEmpty())
        {
            TRACE(_T("DBInterface::InitThreadConnection - No connection string available\n"));
            LeaveCriticalSection(&m_csDB);
            return FALSE;
        }
        strConnString = m_strMainConnString;
        LeaveCriticalSection(&m_csDB);
    }

    SQLHENV hEnv = SQL_NULL_HENV;
    SQLHDBC hConnection = SQL_NULL_HDBC;
    SQLRETURN ret;

    // Allocate environment handle
    ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HENV, &hEnv);
    if (!SQL_SUCCEEDED(ret))
    {
        TRACE(_T("DBInterface::InitThreadConnection - Failed to allocate environment handle\n"));
        return FALSE;
    }

    // Set ODBC version
    ret = SQLSetEnvAttr(hEnv, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);
    if (!SQL_SUCCEEDED(ret))
    {
        TRACE(_T("DBInterface::InitThreadConnection - Failed to set ODBC version\n"));
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
        return FALSE;
    }

    // Allocate connection handle
    ret = SQLAllocHandle(SQL_HANDLE_DBC, hEnv, &hConnection);
    if (!SQL_SUCCEEDED(ret))
    {
        TRACE(_T("DBInterface::InitThreadConnection - Failed to allocate connection handle\n"));
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
        return FALSE;
    }

    // Set connection timeout
    SQLSetConnectAttr(hConnection, SQL_LOGIN_TIMEOUT, (SQLPOINTER)10, 0);

    // Connect to database
    TRACE(_T("DBInterface::InitThreadConnection - Connection string: %s\n"), (LPCTSTR)strConnString);
    TRACE(_T("DBInterface::InitThreadConnection - Connecting to database...\n"));

    // Use Unicode ODBC function
    ret = SQLDriverConnect(hConnection, NULL,
        (SQLWCHAR*)strConnString.GetString(), SQL_NTS,
        NULL, 0, NULL, SQL_DRIVER_COMPLETE);

    if (!SQL_SUCCEEDED(ret))
    {
        CString strError = GetODBCError(SQL_HANDLE_DBC, hConnection);
        TRACE(_T("DBInterface::InitThreadConnection - Connection failed: %s\n"), strError);

        // Clean up
        SQLFreeHandle(SQL_HANDLE_DBC, hConnection);
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
        return FALSE;
    }

    // Set autocommit mode
    SQLSetConnectAttr(hConnection, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);

    // Allocate new TLS slot or reuse existing
    if (!pThreadDB)
    {
        pThreadDB = new ThreadDBConnection();
    }

    pThreadDB->hEnv = hEnv;
    pThreadDB->hConnection = hConnection;
    pThreadDB->bConnected = TRUE;

    if (!TlsSetValue(sm_nTlsIndex, pThreadDB))
    {
        TRACE(_T("DBInterface::InitThreadConnection - TlsSetValue failed\n"));
        SQLDisconnect(hConnection);
        SQLFreeHandle(SQL_HANDLE_DBC, hConnection);
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
        delete pThreadDB;
        return FALSE;
    }

    TRACE(_T("DBInterface::InitThreadConnection - SUCCESS for thread\n"));
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// TLS: Ensure thread has a valid connection (call before each DB operation)
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::EnsureThreadConnection()
{
    SQLHDBC hConn = GetThreadConnection();
    if (!hConn)
    {
        // Try to initialize thread connection
        if (!InitThreadConnection())
        {
            m_strLastError = _T("Failed to initialize thread database connection");
            return FALSE;
        }
        hConn = GetThreadConnection();
        if (!hConn)
        {
            m_strLastError = _T("Failed to get thread database connection");
            return FALSE;
        }
    }
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// TLS: Release ODBC environment and connection for current thread
///////////////////////////////////////////////////////////////////////////////
void CDBInterface::ReleaseThreadConnection()
{
    if (sm_nTlsIndex == TLS_OUT_OF_INDEXES)
        return;

    ThreadDBConnection* pThreadDB = static_cast<ThreadDBConnection*>(TlsGetValue(sm_nTlsIndex));
    if (!pThreadDB)
        return;

    if (pThreadDB->hConnection != SQL_NULL_HDBC)
    {
        SQLDisconnect(pThreadDB->hConnection);
        SQLFreeHandle(SQL_HANDLE_DBC, pThreadDB->hConnection);
        TRACE(_T("DBInterface::ReleaseThreadConnection - Thread connection closed\n"));
        pThreadDB->hConnection = SQL_NULL_HDBC;
    }

    if (pThreadDB->hEnv != SQL_NULL_HENV)
    {
        SQLFreeHandle(SQL_HANDLE_ENV, pThreadDB->hEnv);
        pThreadDB->hEnv = SQL_NULL_HENV;
    }

    pThreadDB->bConnected = FALSE;
    delete pThreadDB;
    TlsSetValue(sm_nTlsIndex, nullptr);
}

BOOL CDBInterface::KeepAlive()
{
    if (!EnsureThreadConnection())
    {
        return FALSE;
    }

    SQLHDBC hConn = GetThreadConnection();
    if (!hConn)
    {
        return FALSE;
    }

    SQLHSTMT hStmt = SQL_NULL_HSTMT;
    SQLRETURN ret;

    ret = SQLAllocHandle(SQL_HANDLE_STMT, hConn, &hStmt);
    if (!SQL_SUCCEEDED(ret))
    {
        return FALSE;
    }

    // 使用不带结果的查询，防止连接断开
    ret = SQLExecDirect(hStmt, (SQLWCHAR*)_T("SELECT 1"), SQL_NTS);

    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);

    if (!SQL_SUCCEEDED(ret))
    {
        TRACE(_T("[DB] KeepAlive failed, trying reconnect...\n"));
        //theApp.m_pTestLog->Info(_T("[DB] KeepAlive failed, reconnecting..."));

        if (ReconnectThreadConnection())
        {
            //theApp.m_pTestLog->Info(_T("[DB] KeepAlive reconnection successful"));
            return TRUE;
        }
        return FALSE;
    }

    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// TLS: Reconnect current thread's database connection (after connection lost)
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::ReconnectThreadConnection()
{
    if (sm_nTlsIndex == TLS_OUT_OF_INDEXES)
    {
        TRACE(_T("DBInterface::ReconnectThreadConnection - TLS not initialized\n"));
        return FALSE;
    }

    // Get existing thread connection data
    ThreadDBConnection* pThreadDB = static_cast<ThreadDBConnection*>(TlsGetValue(sm_nTlsIndex));
    if (!pThreadDB)
    {
        TRACE(_T("DBInterface::ReconnectThreadConnection - No thread data found, trying InitThreadConnection\n"));
        return InitThreadConnection();
    }

    // Close existing connection
    if (pThreadDB->hConnection != SQL_NULL_HDBC)
    {
        SQLDisconnect(pThreadDB->hConnection);
        SQLFreeHandle(SQL_HANDLE_DBC, pThreadDB->hConnection);
        pThreadDB->hConnection = SQL_NULL_HDBC;
        TRACE(_T("DBInterface::ReconnectThreadConnection - Old connection disconnected\n"));
    }

    if (pThreadDB->hEnv != SQL_NULL_HENV)
    {
        SQLFreeHandle(SQL_HANDLE_ENV, pThreadDB->hEnv);
        pThreadDB->hEnv = SQL_NULL_HENV;
    }

    // Get connection string
    CString strConnString;
    {
        EnterCriticalSection(&m_csDB);
        strConnString = m_strMainConnString;
        LeaveCriticalSection(&m_csDB);
    }

    if (strConnString.IsEmpty())
    {
        TRACE(_T("DBInterface::ReconnectThreadConnection - No connection string available\n"));
        return FALSE;
    }

    SQLRETURN ret;

    // Allocate new environment handle
    ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HENV, &pThreadDB->hEnv);
    if (!SQL_SUCCEEDED(ret))
    {
        TRACE(_T("DBInterface::ReconnectThreadConnection - Failed to allocate environment handle\n"));
        return FALSE;
    }

    ret = SQLSetEnvAttr(pThreadDB->hEnv, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);
    if (!SQL_SUCCEEDED(ret))
    {
        TRACE(_T("DBInterface::ReconnectThreadConnection - Failed to set ODBC version\n"));
        SQLFreeHandle(SQL_HANDLE_ENV, pThreadDB->hEnv);
        pThreadDB->hEnv = SQL_NULL_HENV;
        return FALSE;
    }

    ret = SQLAllocHandle(SQL_HANDLE_DBC, pThreadDB->hEnv, &pThreadDB->hConnection);
    if (!SQL_SUCCEEDED(ret))
    {
        TRACE(_T("DBInterface::ReconnectThreadConnection - Failed to allocate connection handle\n"));
        SQLFreeHandle(SQL_HANDLE_ENV, pThreadDB->hEnv);
        pThreadDB->hEnv = SQL_NULL_HENV;
        return FALSE;
    }

    SQLSetConnectAttr(pThreadDB->hConnection, SQL_LOGIN_TIMEOUT, (SQLPOINTER)10, 0);

    ret = SQLDriverConnect(pThreadDB->hConnection, NULL,
        (SQLWCHAR*)strConnString.GetString(), SQL_NTS,
        NULL, 0, NULL, SQL_DRIVER_COMPLETE);

    if (!SQL_SUCCEEDED(ret))
    {
        m_strLastError = GetODBCError(SQL_HANDLE_DBC, pThreadDB->hConnection);
        TRACE(_T("DBInterface::ReconnectThreadConnection - Connection failed - %s\n"), m_strLastError);
        SQLFreeHandle(SQL_HANDLE_DBC, pThreadDB->hConnection);
        SQLFreeHandle(SQL_HANDLE_ENV, pThreadDB->hEnv);
        pThreadDB->hConnection = SQL_NULL_HDBC;
        pThreadDB->hEnv = SQL_NULL_HENV;
        pThreadDB->bConnected = FALSE;
        return FALSE;
    }

    pThreadDB->bConnected = TRUE;
    TRACE(_T("DBInterface::ReconnectThreadConnection - Success!\n"));
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// Release all thread connections (call when shutting down)
///////////////////////////////////////////////////////////////////////////////
void CDBInterface::ReleaseAllThreadConnections()
{
    // Note: We can only release the current thread's connection here
    // Other threads' connections will be released when those threads exit
    ReleaseThreadConnection();
}

///////////////////////////////////////////////////////////////////////////////
// Get ODBC Error Message
///////////////////////////////////////////////////////////////////////////////
CString CDBInterface::GetODBCError(SQLSMALLINT hType, SQLHANDLE hHandle)
{
    SQLWCHAR szSqlState[SQL_SQLSTATE_SIZE + 1];
    SQLWCHAR szMessage[SQL_MAX_MESSAGE_LENGTH + 1];
    SQLINTEGER nNativeError;
    SQLSMALLINT nMsgLen;

    CString strError;
    for (int i = 1; i <= 8; i++)
    {
        SQLRETURN ret = SQLGetDiagRec(hType, hHandle, i,
            szSqlState, &nNativeError, szMessage, SQL_MAX_MESSAGE_LENGTH, &nMsgLen);
        if (SQL_SUCCEEDED(ret))
        {
            if (i > 1)
                strError += _T("; ");
            strError += CString(szMessage);
        }
        else
        {
            break;
        }
    }
    return strError;
}

///////////////////////////////////////////////////////////////////////////////
// Connect to database (establishes connection string, TLS will create actual connection)
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::Connect(const CString& strConnString)
{
    EnterCriticalSection(&m_csDB);

    // Clear any existing main connection (for compatibility)
    if (m_hConnection != SQL_NULL_HDBC)
    {
        SQLDisconnect(m_hConnection);
        SQLFreeHandle(SQL_HANDLE_DBC, m_hConnection);
        m_hConnection = SQL_NULL_HDBC;
    }
    if (m_hEnv != SQL_NULL_HENV)
    {
        SQLFreeHandle(SQL_HANDLE_ENV, m_hEnv);
        m_hEnv = SQL_NULL_HENV;
    }

    // Parse and validate connection string
    CString strConn;
    if (strConnString.Find(_T("DRIVER=")) >= 0 || strConnString.Find(_T("Driver=")) >= 0
        || strConnString.Find(_T("driver=")) >= 0)
    {
        strConn = strConnString;
    }
    else
    {
        // Simple format: tcp://host:port/database?user=xxx&password=xxx
        // Convert to ODBC format
        CString strHost = _T("localhost");
        int nPort = 3306;
        CString strDBName = _T("");
        CString strUser = _T("root");
        CString strPass = _T("");

        CString strConnTemp = strConnString;
        int nTcpPos = strConnTemp.Find(_T("tcp://"));
        CString strTemp;
        if (nTcpPos >= 0)
            strTemp = strConnTemp.Mid(nTcpPos + 6);
        else
            strTemp = strConnTemp;

        int nSlash = strTemp.Find(_T("/"));
        int nQues = strTemp.Find(_T("?"));

        CString strHostPort;
        if (nSlash > 0 && nQues > 0)
            strHostPort = strTemp.Left(min(nSlash, nQues));
        else if (nSlash > 0)
            strHostPort = strTemp.Left(nSlash);
        else if (nQues > 0)
            strHostPort = strTemp.Left(nQues);
        else
            strHostPort = strTemp;

        int nColon = strHostPort.ReverseFind(_T(':'));
        if (nColon > 0)
        {
            strHost = strHostPort.Left(nColon);
            CString strPortStr = strHostPort.Mid(nColon + 1);
            nPort = _ttoi(strPortStr);
        }

        if (nSlash > 0)
        {
            CString strAfterSlash;
            if (nQues > 0 && nQues > nSlash)
                strAfterSlash = strTemp.Mid(nSlash + 1, nQues - nSlash - 1);
            else
                strAfterSlash = strTemp.Mid(nSlash + 1);
            strDBName = strAfterSlash;
        }

        int nPos = strConnTemp.Find(_T("user="));
        if (nPos >= 0)
        {
            CString strAfterUser = strConnTemp.Mid(nPos + 5);
            int nAmp = strAfterUser.Find(_T("&"));
            if (nAmp > 0)
                strUser = strAfterUser.Left(nAmp);
            else
                strUser = strAfterUser;
        }

        nPos = strConnTemp.Find(_T("password="));
        if (nPos >= 0)
        {
            CString strAfterPass = strConnTemp.Mid(nPos + 9);
            int nAmp = strAfterPass.Find(_T("&"));
            if (nAmp > 0)
                strPass = strAfterPass.Left(nAmp);
            else
                strPass = strAfterPass;
        }

        // Try different MySQL ODBC driver names (ANSI version for compatibility)
        // Common names: "MySQL ODBC 5.3 Driver", "MySQL ODBC 5.3 ANSI Driver", etc.
        strConn.Format(_T("DRIVER={MySQL ODBC 5.3 ANSI Driver};SERVER=%s;PORT=%d;DATABASE=%s;UID=%s;PWD=%s;"),
            strHost, nPort, strDBName, strUser, strPass);
    }

    // Save ODBC format connection string for TLS connection creation
    m_strMainConnString = strConn;

    // Test connection by creating a temporary connection
    SQLHENV hTestEnv = SQL_NULL_HENV;
    SQLHDBC hTestConn = SQL_NULL_HDBC;
    SQLRETURN ret;

    ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HENV, &hTestEnv);
    if (!SQL_SUCCEEDED(ret))
    {
        m_strLastError = _T("Failed to allocate environment handle");
        LeaveCriticalSection(&m_csDB);
        return FALSE;
    }

    ret = SQLSetEnvAttr(hTestEnv, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);
    if (!SQL_SUCCEEDED(ret))
    {
        m_strLastError = _T("Failed to set ODBC version");
        SQLFreeHandle(SQL_HANDLE_ENV, hTestEnv);
        LeaveCriticalSection(&m_csDB);
        return FALSE;
    }

    ret = SQLAllocHandle(SQL_HANDLE_DBC, hTestEnv, &hTestConn);
    if (!SQL_SUCCEEDED(ret))
    {
        m_strLastError = _T("Failed to allocate connection handle");
        SQLFreeHandle(SQL_HANDLE_ENV, hTestEnv);
        LeaveCriticalSection(&m_csDB);
        return FALSE;
    }

    SQLSetConnectAttr(hTestConn, SQL_LOGIN_TIMEOUT, (SQLPOINTER)10, 0);

    // Debug: Show the actual connection string
    TRACE(_T("DBInterface: Connection string: %s\n"), (LPCTSTR)strConn);
    TRACE(_T("DBInterface: Testing connection to database...\n"));
    ret = SQLDriverConnect(hTestConn, NULL,
        (SQLWCHAR*)strConn.GetString(), SQL_NTS,
        NULL, 0, NULL, SQL_DRIVER_COMPLETE);

    if (!SQL_SUCCEEDED(ret))
    {
        m_strLastError = GetODBCError(SQL_HANDLE_DBC, hTestConn);
        TRACE(_T("DBInterface: Connection test failed - %s\n"), m_strLastError);

        SQLFreeHandle(SQL_HANDLE_DBC, hTestConn);
        SQLFreeHandle(SQL_HANDLE_ENV, hTestEnv);
        m_strMainConnString.Empty();
        m_bConnected = FALSE;

        LeaveCriticalSection(&m_csDB);
        return FALSE;
    }

    // Connection successful - keep test connection as main connection
    m_hEnv = hTestEnv;
    m_hConnection = hTestConn;
    m_bConnected = TRUE;

    TRACE(_T("DBInterface: Connected to MySQL database via ODBC (main connection OK)\n"));

    LeaveCriticalSection(&m_csDB);
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// Disconnect from database
///////////////////////////////////////////////////////////////////////////////
void CDBInterface::Disconnect()
{
    EnterCriticalSection(&m_csDB);

    // Release main connection
    if (m_hConnection != SQL_NULL_HDBC)
    {
        SQLDisconnect(m_hConnection);
        SQLFreeHandle(SQL_HANDLE_DBC, m_hConnection);
        m_hConnection = SQL_NULL_HDBC;
    }

    if (m_hEnv != SQL_NULL_HENV)
    {
        SQLFreeHandle(SQL_HANDLE_ENV, m_hEnv);
        m_hEnv = SQL_NULL_HENV;
    }

    m_bConnected = FALSE;
    m_strMainConnString.Empty();

    LeaveCriticalSection(&m_csDB);

    // Note: TLS connections will be released when their threads exit
    // or when ReleaseAllThreadConnections() is called
    TRACE(_T("DBInterface: Disconnected from database\n"));
}

///////////////////////////////////////////////////////////////////////////////
// Execute SQL statement (using Thread Local Storage connection)
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::ExecuteSQL(const CString& strSQL)
{
    // Ensure thread has a valid connection
    if (!EnsureThreadConnection())
    {
        return FALSE;
    }

    SQLHDBC hConn = GetThreadConnection();
    SQLHSTMT hStmt = SQL_NULL_HSTMT;
    SQLRETURN ret;

    // Allocate statement handle
    ret = SQLAllocHandle(SQL_HANDLE_STMT, hConn, &hStmt);
    if (!SQL_SUCCEEDED(ret))
    {
        m_strLastError = _T("Failed to allocate statement handle");
        return FALSE;
    }

    // Execute SQL using Unicode ODBC function
    ret = SQLExecDirect(hStmt, (SQLWCHAR*)strSQL.GetString(), SQL_NTS);

    if (!SQL_SUCCEEDED(ret))
    {
        // Must get error info BEFORE freeing the handle!
        m_strLastError = GetODBCError(SQL_HANDLE_STMT, hStmt);
        //theApp.m_pTestLog->Info(_T("[DBError] SQL Error - %s | SQL: %s"), m_strLastError, strSQL);
    }

    // Free statement handle after getting error
    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);

    return SQL_SUCCEEDED(ret);
}

///////////////////////////////////////////////////////////////////////////////
// Execute SQL query and return result set
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::ExecuteQuery(const CString& strSQL, SQLHSTMT& hStmt)
{
    // Ensure thread has a valid connection
    if (!EnsureThreadConnection())
    {
        return FALSE;
    }

    SQLHDBC hConn = GetThreadConnection();
    SQLRETURN ret;

    // Allocate statement handle
    ret = SQLAllocHandle(SQL_HANDLE_STMT, hConn, &hStmt);
    if (!SQL_SUCCEEDED(ret))
    {
        m_strLastError = _T("Failed to allocate statement handle");
        return FALSE;
    }

    // Execute SQL using Unicode ODBC function
    ret = SQLExecDirect(hStmt, (SQLWCHAR*)strSQL.GetString(), SQL_NTS);

    if (!SQL_SUCCEEDED(ret))
    {
        m_strLastError = GetODBCError(SQL_HANDLE_STMT, hStmt);
        TRACE(_T("DBInterface: SQL Query Error - %s\nSQL: %s\n"), m_strLastError, strSQL);

        // 检测 MySQL server has gone away，自动重连
        if (m_strLastError.Find(_T("MySQL server has gone away")) >= 0 ||
            m_strLastError.Find(_T("Lost connection")) >= 0 ||
            m_strLastError.Find(_T("server has gone away")) >= 0)
        {
            TRACE(_T("[DB] MySQL connection lost, trying to reconnect...\n"));
            //theApp.m_pTestLog->Info(_T("[DB] MySQL connection lost, reconnecting..."));

            // 重建当前线程的连接
            if (ReconnectThreadConnection())
            {
                TRACE(_T("[DB] Reconnection successful, retrying query...\n"));
                //theApp.m_pTestLog->Info(_T("[DB] Reconnection successful, retrying query"));

                // 重试 SQL
                hConn = GetThreadConnection();
                if (hConn)
                {
                    ret = SQLAllocHandle(SQL_HANDLE_STMT, hConn, &hStmt);
                    if (SQL_SUCCEEDED(ret))
                    {
                        ret = SQLExecDirect(hStmt, (SQLWCHAR*)strSQL.GetString(), SQL_NTS);
                        if (SQL_SUCCEEDED(ret))
                        {
                            TRACE(_T("[DB] Retry successful!\n"));
                            return TRUE;
                        }
                        m_strLastError = GetODBCError(SQL_HANDLE_STMT, hStmt);
                        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
                        hStmt = SQL_NULL_HSTMT;
                    }
                }
            }
            else
            {
                TRACE(_T("[DB] Reconnection failed!\n"));
                //theApp.m_pTestLog->Info(_T("[DB] Reconnection failed"));
            }
        }

        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        hStmt = SQL_NULL_HSTMT;
        return FALSE;
    }

    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// Generate GUID
///////////////////////////////////////////////////////////////////////////////
CString CDBInterface::GenerateGUID()
{
    GUID guid;
    CoCreateGuid(&guid);

    CString strGUID;
    strGUID.Format(_T("{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}"),
        guid.Data1, guid.Data2, guid.Data3,
        guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
        guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);

    return strGUID;
}

///////////////////////////////////////////////////////////////////////////////
// Generate unique inspection ID (YYYY_MM_DD_HH_MM_SS_fff_JJ, guaranteed unique)
///////////////////////////////////////////////////////////////////////////////
CString CDBInterface::GenerateUniqueIDForJig(int jigNum)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    CString strJig;
    // jigNum 从 0 开始（如 panelNum），但 UniqueID 后缀应为实际工位号（从 1 开始）
    int nJig = (jigNum >= 0) ? (jigNum + 1) : 1;
    strJig.Format(_T("%02d"), nJig % 100);  // 取后两位，保证两位数格式
    CString strUniqueID;
    strUniqueID.Format(_T("%04d_%02d_%02d_%02d_%02d_%02d_%03d_%s"),
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        strJig);
    return strUniqueID;
}

///////////////////////////////////////////////////////////////////////////////
// Update ivs_lcd_idmap before inspection starts
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::UpsertIDMapBeforeStart(const CString& markID, int posID, const CString& uniqueID,
                                          const CString& barcode, const CString& mainAoiFixID)
{
    if (!m_bConnected)
        return FALSE;

    CString strSQL;
    strSQL.Format(
        _T("INSERT INTO IVS_LCD_IDMap ")
        _T("(MarkID, PosID, UniqueID, TableSuffix, Barcode, MainAoiFixID, Fix_IDCode) ")
        _T("VALUES ")
        _T("('%s', %d, '%s', '%s', '%s', '%s', '%s') ")
        _T("ON DUPLICATE KEY UPDATE ")
        _T("PosID = VALUES(PosID), ")
        _T("UniqueID = VALUES(UniqueID), ")
        _T("TableSuffix = VALUES(TableSuffix), ")
        _T("Barcode = VALUES(Barcode), ")
        _T("MainAoiFixID = VALUES(MainAoiFixID), ")
        _T("Fix_IDCode = VALUES(Fix_IDCode)"),
        EscapeString(markID),
        posID,
        EscapeString(uniqueID),
        _T(""),
        EscapeString(barcode),
        EscapeString(mainAoiFixID),
        _T(""));

    return ExecuteSQL(strSQL);
}

///////////////////////////////////////////////////////////////////////////////
// Clear all records from IVS_LCD_IDMap table
// Call this before a new batch of panels starts (e.g., at panelNum==0)
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::ClearIDMapTable()
{
    if (!m_bConnected)
    {
        m_strLastError = _T("Not connected to database");
        return FALSE;
    }

    return ExecuteSQL(_T("DELETE FROM IVS_LCD_IDMap"));
}

///////////////////////////////////////////////////////////////////////////////
// UPDATE IVS_LCD_IDMap based on Start$ prefix jig pattern
// Each jig generates new GUID (CoCreateGuid), UPDATE MainAoiFixID corresponding records
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::UpdateIDMapForStartPattern(const CString& strCurrentJigs)
{
    if (!m_bConnected)
    {
        m_strLastError = _T("Not connected to database");
        return FALSE;
    }

    if (strCurrentJigs.GetLength() != 8)
    {
        m_strLastError = _T("UpdateIDMapForStartPattern: strCurrentJigs length must be 8");
        return FALSE;
    }

    BOOL bAllOk = TRUE;
    for (int i = 0; i < 4; i++)
    {
        CString pair = strCurrentJigs.Mid(i * 2, 2);
        if (pair.IsEmpty() || pair.CompareNoCase(_T("00")) == 0)
            continue;

        int nJig = _ttoi(pair);
        if (nJig < 1 || nJig > 4)
        {
            m_strLastError.Format(_T("Invalid jig pair at slot %d: %s"), i, (LPCTSTR)pair);
            bAllOk = FALSE;
            continue;
        }

        // Jig number 1~4 corresponds to MainAoiFixID
        int nMainAoiFixID = nJig;
        CString strMarkID;
        strMarkID.Format(_T("%02d"), nJig);

        // Generate new GUID (CoCreateGuid, globally unique, without braces)
        GUID guid;
        CoCreateGuid(&guid);
        CString strUniqueID;
        strUniqueID.Format(
            _T("%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X"),
            guid.Data1, guid.Data2, guid.Data3,
            guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
            guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);

        // Barcode filled with AUTO_TEST marker for easy identification
        CString strBarcode;
        strBarcode.Format(_T("AUTO_TEST_%s"), strUniqueID);

        CString strSQL;
        strSQL.Format(
            _T("INSERT INTO IVS_LCD_IDMap (MainAoiFixID, UniqueID, Barcode, MarkID, PosID) ")
            _T("VALUES (%d, '%s', '%s', '%s', %d) ")
            _T("ON DUPLICATE KEY UPDATE ")
            _T("UniqueID = VALUES(UniqueID), ")
            _T("Barcode = VALUES(Barcode), ")
            _T("MarkID = VALUES(MarkID)"),
            nMainAoiFixID,
            EscapeString(strUniqueID),
            EscapeString(strBarcode),
            EscapeString(strMarkID),
            nMainAoiFixID - 1);

        if (!ExecuteSQL(strSQL))
        {
            TRACE(_T("UpdateIDMapForStartPattern: UPDATE failed for MainAoiFixID=%d\n"), nMainAoiFixID);
            bAllOk = FALSE;
        }
        else
        {
            TRACE(_T("UpdateIDMapForStartPattern: MainAoiFixID=%d, UniqueID=%s, Barcode=%s\n"),
                nMainAoiFixID, (LPCTSTR)strUniqueID, (LPCTSTR)strBarcode);
        }

        // Millisecond delay to reduce GUID collision risk
        Sleep(1);
    }

    return bAllOk;
}

///////////////////////////////////////////////////////////////////////////////
// Escape SQL string
///////////////////////////////////////////////////////////////////////////////
CString CDBInterface::EscapeString(const CString& str)
{
    CString result = str;
    result.Replace(_T("'"), _T("''"));
    return result;
}

CString CDBInterface::GetNowFunctionSQL() const
{
    return _T("NOW()");
}

CString CDBInterface::GetSelectLatestByUniqueIDSQL(const CString& strUniqueID) const
{
    CString strSQL;
    strSQL.Format(
        _T("SELECT * FROM IVS_LCD_InspectionResult WHERE UniqueID = '%s' ORDER BY SysID DESC LIMIT 1"),
        EscapeString(strUniqueID));
    return strSQL;
}

BOOL CDBInterface::UpsertIDMap(const CInspectionResult& result)
{
    CString strSQL;
    strSQL.Format(
        _T("INSERT INTO IVS_LCD_IDMap ")
        _T("(MarkID, PosID, UniqueID, TableSuffix, Barcode, MainAoiFixID, Fix_IDCode) ")
        _T("VALUES ")
        _T("('%s', %d, '%s', '%s', '%s', '%s', '%s') ")
        _T("ON DUPLICATE KEY UPDATE ")
        _T("PosID = VALUES(PosID), ")
        _T("UniqueID = VALUES(UniqueID), ")
        _T("TableSuffix = VALUES(TableSuffix), ")
        _T("Barcode = VALUES(Barcode), ")
        _T("MainAoiFixID = VALUES(MainAoiFixID), ")
        _T("Fix_IDCode = VALUES(Fix_IDCode)"),
        EscapeString(result.MarkID),
        result.PlatformID,
        EscapeString(result.UniqueID),
        _T(""),
        EscapeString(result.ScreenID),
        EscapeString(result.MainAoiFixID),
        _T(""));

    return ExecuteSQL(strSQL);
}

// Helper function to get string from column using SQLGetData
static CString GetColumnString(SQLHSTMT hStmt, SQLUSMALLINT colIndex)
{
    SQLWCHAR buffer[4096];
    SQLLEN cbLen = 0;
    SQLRETURN ret;

    ret = SQLGetData(hStmt, colIndex, SQL_C_WCHAR, buffer, sizeof(buffer), &cbLen);
    if (cbLen == SQL_NULL_DATA || cbLen == 0 || !SQL_SUCCEEDED(ret))
        return _T("");

    // Handle truncated data
    if (cbLen >= (SQLLEN)sizeof(buffer))
    {
        // Data was truncated, get the actual length
        SQLLEN cbActual;
        SQLGetData(hStmt, colIndex, SQL_C_WCHAR, NULL, 0, &cbActual);
        if (cbActual > 0 && cbActual != SQL_NULL_DATA)
        {
            // Allocate and get full data
            SQLWCHAR* pFullBuffer = new SQLWCHAR[(cbActual / sizeof(SQLWCHAR)) + 1];
            SQLGetData(hStmt, colIndex, SQL_C_WCHAR, pFullBuffer, cbActual + sizeof(SQLWCHAR), &cbLen);
            CString str(pFullBuffer);
            delete[] pFullBuffer;
            return str;
        }
    }

    return CString(buffer);
}

// Helper function to get integer from column
static int GetColumnInt(SQLHSTMT hStmt, SQLUSMALLINT colIndex)
{
    SQLINTEGER nValue = 0;
    SQLGetData(hStmt, colIndex, SQL_C_LONG, &nValue, sizeof(nValue), NULL);
    return (int)nValue;
}

// Helper function to get double from column
static double GetColumnDouble(SQLHSTMT hStmt, SQLUSMALLINT colIndex)
{
    double dValue = 0.0;
    SQLGetData(hStmt, colIndex, SQL_C_DOUBLE, &dValue, sizeof(dValue), NULL);
    return dValue;
}

BOOL CDBInterface::InsertDefectToTable(const CString& strTableName, const CDefectInfo& defect)
{
    CString strSQL;
    strSQL.Format(
        _T("INSERT INTO %s ")
        _T("(GUID_IVS_LCD_InspectionResult, DefectIndex, Type, PatternID, PatternName, InspType, ")
        _T("Pos_x, Pos_y, Pos_width, Pos_height, ")
        _T("TrueSize, TrueDiameter, TrueLongSize, TrueShortSize, ")
        _T("OriArea, OriLongSize, OriShortSize, ")
        _T("GrayScale, GrayScale_BK, GrayScaleDiff, GrayscaleMean, GrayscaleMin, GrayscaleMax, ")
        _T("Area, Roundness, MajorAxisAngle, JND, ")
        _T("Layer, Code_AOI, Grade_AOI, Level_AOI, DefClass_AOI, DefName_AOI, ")
        _T("AlgName, AlgID, ReasonCode, FeatureName, FeatureMin, FeatureMax, FeatureUnit, FeatureValue, ")
        _T("ImagePath, XMLInfo) ")
        _T("VALUES ")
        _T("('%s', %d, '%s', %d, '%s', '%s', ")
        _T("%d, %d, %d, %d, ")
        _T("%f, %f, %f, %f, ")
        _T("%d, %d, %d, ")
        _T("%f, %f, %f, %f, %f, %f, ")
        _T("%d, %f, %f, %f, ")
        _T("'%s', '%s', '%s', '%s', '%s', '%s', ")
        _T("'%s', %d, '%s', '%s', '%s', '%s', '%s', '%s', ")
        _T("'%s', '%s')"),
        strTableName,
        EscapeString(defect.GUID_Parent),
        defect.DefectIndex,
        EscapeString(defect.Type),
        defect.PatternID,
        EscapeString(defect.PatternName),
        EscapeString(defect.InspType),
        defect.Pos_x,
        defect.Pos_y,
        defect.Pos_width,
        defect.Pos_height,
        defect.TrueSize,
        defect.TrueDiameter,
        defect.TrueLongSize,
        defect.TrueShortSize,
        defect.OriArea,
        defect.OriLongSize,
        defect.OriShortSize,
        defect.GrayScale,
        defect.GrayScale_BK,
        defect.GrayScaleDiff,
        defect.GrayscaleMean,
        defect.GrayscaleMin,
        defect.GrayscaleMax,
        defect.Area,
        defect.Roundness,
        defect.MajorAxisAngle,
        defect.JND,
        EscapeString(defect.Layer),
        EscapeString(defect.Code_AOI),
        EscapeString(defect.Grade_AOI),
        EscapeString(defect.Level_AOI),
        EscapeString(defect.DefClass_AOI),
        EscapeString(defect.DefName_AOI),
        EscapeString(defect.AlgName),
        defect.AlgID,
        EscapeString(defect.ReasonCode),
        EscapeString(defect.FeatureName),
        EscapeString(defect.FeatureMin),
        EscapeString(defect.FeatureMax),
        EscapeString(defect.FeatureUnit),
        EscapeString(defect.FeatureValue),
        EscapeString(defect.ImagePath),
        EscapeString(defect.XMLInfo));

    return ExecuteSQL(strSQL);
}

BOOL CDBInterface::QueryDefectsByParentGUIDFromTable(const CString& strTableName, const CString& strParentGUID, CDefectInfoList& defects)
{
    CString strSQL;
    strSQL.Format(
        _T("SELECT ")
        // 基本信息
        _T("GUID_IVS_LCD_InspectionResult, DefectIndex, Type, ")
        _T("PatternID, PatternName, InspType, ")
        // 位置
        _T("Pos_x, Pos_y, Pos_width, Pos_height, ")
        // 物理尺寸
        _T("TrueSize, TrueDiameter, TrueLongSize, TrueShortSize, ")
        // 原始图特征
        _T("OriArea, OriLongSize, OriShortSize, ")
        // 灰度特征
        _T("Grayscale, Grayscale_BK, GrayscaleDiff, GrayscaleMean, GrayscaleMin, GrayscaleMax, ")
        // 几何特征
        _T("Area, Roundness, MajorAxisAngle, JND, ")
        // 层级
        _T("Layer, ")
        // AOI分类
        _T("Code_AOI, Grade_AOI, Level_AOI, DefClass_AOI, DefName_AOI, ")
        // 算法/特征
        _T("AlgName, AlgID, ReasonCode, ")
        _T("FeatureName, FeatureMin, FeatureMax, FeatureUnit, FeatureValue, ")
        // 图像和XML
        _T("ImagePath, ")
        // 复判字段
        _T("ReviewResult_Worker, ReviewResult_Machine, MachineReviewDefectName, ")
        _T("DefColor, DefColorValue, DefClass_AutoReview, DefName_AutoReview, ")
        _T("AreaStat, Energy ")
        _T("FROM %s WHERE GUID_IVS_LCD_InspectionResult = '%s' ORDER BY DefectIndex"),
        strTableName,
        EscapeString(strParentGUID));

    TRACE(_T("[QueryDefectsByParentGUIDFromTable] 执行SQL, Table=%s, ParentGUID=%s\n"), (LPCTSTR)strTableName, (LPCTSTR)strParentGUID);
    TRACE(_T("  SQL: %s\n"), (LPCTSTR)strSQL);
    //theApp.m_pTestLog->Info(_T("[QueryDefectsByParentGUIDFromTable] 执行SQL, Table=%s, ParentGUID=%s"),
    //    (LPCTSTR)strTableName, (LPCTSTR)strParentGUID);
    //theApp.m_pTestLog->Info(_T("  SQL: %s"), (LPCTSTR)strSQL);

    SQLHSTMT hStmt;
    if (!ExecuteQuery(strSQL, hStmt))
    {
        //TRACE(_T("[QueryDefectsByParentGUIDFromTable] ExecuteQuery failed, Error=%s\n"), (LPCTSTR)m_strLastError);
        //theApp.m_pTestLog->Info(_T("[QueryDefectsByParentGUIDFromTable] ExecuteQuery failed, Error=%s"), (LPCTSTR)m_strLastError);
        return FALSE;
    }

    //TRACE(_T("[QueryDefectsByParentGUIDFromTable] SQL执行成功, 开始Fetch数据...\n"));
    //theApp.m_pTestLog->Info(_T("[QueryDefectsByParentGUIDFromTable] SQL执行成功, 开始Fetch数据..."));

    SQLRETURN ret;
    int nFetchCount = 0;
    while ((ret = SQLFetch(hStmt)) != SQL_NO_DATA)
    {
        if (ret == SQL_ERROR)
        {
            m_strLastError = GetODBCError(SQL_HANDLE_STMT, hStmt);
            //TRACE(_T("[QueryDefectsByParentGUIDFromTable] Fetch failed - %s\n"), m_strLastError);
            //theApp.m_pTestLog->Info(_T("[QueryDefectsByParentGUIDFromTable] Fetch failed - %s"), m_strLastError);
            SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
            return FALSE;
        }

        CDefectInfo defect;
        defect.GUID_Parent = strParentGUID;
        defect.DefectIndex = GetColumnInt(hStmt, 2);
        defect.Type = GetColumnString(hStmt, 3);
        defect.PatternID = GetColumnInt(hStmt, 4);
        defect.PatternName = GetColumnString(hStmt, 5);
        defect.InspType = GetColumnString(hStmt, 6);
        defect.Pos_x = GetColumnInt(hStmt, 7);
        defect.Pos_y = GetColumnInt(hStmt, 8);
        defect.Pos_width = GetColumnInt(hStmt, 9);
        defect.Pos_height = GetColumnInt(hStmt, 10);
        defect.TrueSize = GetColumnDouble(hStmt, 11);
        defect.TrueDiameter = GetColumnDouble(hStmt, 12);
        defect.TrueLongSize = GetColumnDouble(hStmt, 13);
        defect.TrueShortSize = GetColumnDouble(hStmt, 14);
        defect.OriArea = GetColumnInt(hStmt, 15);
        defect.OriLongSize = GetColumnInt(hStmt, 16);
        defect.OriShortSize = GetColumnInt(hStmt, 17);
        defect.GrayScale = GetColumnInt(hStmt, 18);
        defect.GrayScale_BK = GetColumnInt(hStmt, 19);
        defect.GrayScaleDiff = GetColumnDouble(hStmt, 20);
        defect.GrayscaleMean = GetColumnDouble(hStmt, 21);
        defect.GrayscaleMin = GetColumnInt(hStmt, 22);
        defect.GrayscaleMax = GetColumnInt(hStmt, 23);
        defect.Area = GetColumnInt(hStmt, 24);
        defect.Roundness = GetColumnDouble(hStmt, 25);
        defect.MajorAxisAngle = GetColumnDouble(hStmt, 26);
        defect.JND = GetColumnDouble(hStmt, 27);
        defect.Layer = GetColumnString(hStmt, 28);
        defect.Code_AOI = GetColumnString(hStmt, 29);
        defect.Grade_AOI = GetColumnString(hStmt, 30);
        defect.Level_AOI = GetColumnString(hStmt, 31);
        defect.DefClass_AOI = GetColumnString(hStmt, 32);
        defect.DefName_AOI = GetColumnString(hStmt, 33);
        defect.AlgName = GetColumnString(hStmt, 34);
        defect.AlgID = GetColumnInt(hStmt, 35);
        defect.ReasonCode = GetColumnString(hStmt, 36);
        defect.FeatureName = GetColumnString(hStmt, 37);
        defect.FeatureMin = GetColumnString(hStmt, 38);
        defect.FeatureMax = GetColumnString(hStmt, 39);
        defect.FeatureUnit = GetColumnString(hStmt, 40);
        defect.FeatureValue = GetColumnString(hStmt, 41);
        defect.ImagePath = GetColumnString(hStmt, 42);
        //defect.XMLInfo = GetColumnString(hStmt, 43);
        defect.ReviewResult_Worker = GetColumnString(hStmt, 43);
        defect.ReviewResult_Machine = GetColumnString(hStmt, 44);
        defect.MachineReviewDefectName = GetColumnString(hStmt, 45);
        defect.DefColor = GetColumnString(hStmt, 46);
        defect.DefColorValue = GetColumnDouble(hStmt, 47);
        defect.DefClass_AutoReview = GetColumnString(hStmt, 48);
        defect.DefName_AutoReview = GetColumnString(hStmt, 49);

        nFetchCount++;
        CString strFetchLog;
        strFetchLog.Format(_T("  [Fetch #%d] DefectIndex=%d, Type=%s, Pos=(%d,%d,%d,%d), Code=%s, DefClass=%s, DefName=%s"),
            nFetchCount, defect.DefectIndex, (LPCTSTR)defect.Type,
            defect.Pos_x, defect.Pos_y, defect.Pos_width, defect.Pos_height,
            (LPCTSTR)defect.Code_AOI, (LPCTSTR)defect.DefClass_AOI, (LPCTSTR)defect.DefName_AOI);
        TRACE(_T("%s\n"), (LPCTSTR)strFetchLog);
        //theApp.m_pTestLog->Info(strFetchLog);

        defects.push_back(defect);
    }

    TRACE(_T("[QueryDefectsByParentGUIDFromTable] Fetch completed, got %d defect records\n"), nFetchCount);
    //theApp.m_pTestLog->Info(_T("[QueryDefectsByParentGUIDFromTable] Fetch completed, got %d defect records"), nFetchCount);

    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return TRUE;
}

BOOL CDBInterface::DeleteDefectsByParentGUIDFromTable(const CString& strTableName, const CString& strParentGUID)
{
    CString strSQL;
    strSQL.Format(
        _T("DELETE FROM %s WHERE GUID_IVS_LCD_InspectionResult = '%s'"),
        strTableName,
        EscapeString(strParentGUID));

    return ExecuteSQL(strSQL);
}

///////////////////////////////////////////////////////////////////////////////
// Insert inspection result
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::InsertInspectionResult(const CInspectionResult& result)
{
    CString strSQL;
    strSQL.Format(
        _T("INSERT INTO IVS_LCD_InspectionResult ")
        _T("(GUID, Barcode, DeviceID, PlatformID, ModelName, UniqueID, MarkID, MainAoiFixID, ")
        _T("StartTime, StopTime, Status, AOIResult, ")
        _T("LocateShiftX, LocateShiftY, LocateAngle, ")
        _T("RawImageXLen, RawImageYLen, GridImageXLen, GridImageYLen, ")
        _T("PanelPhysicalXLen, PanelPhysicalYLen, ")
        _T("Code_AOI, Grade_AOI, Level_AOI, DefClass_AOI, DefName_AOI, ")
        _T("OperatorID, XMLInfo) ")
        _T("VALUES ")
        _T("('%s', '%s', '%s', %d, '%s', '%s', '%s', '%s', ")
        _T("'%s', '%s', '%s', '%s', ")
        _T("%f, %f, %f, ")
        _T("%d, %d, %d, %d, ")
        _T("%f, %f, ")
        _T("'%s', '%s', '%s', '%s', '%s', ")
        _T("'%s', '%s')"),
        EscapeString(result.GUID),
        EscapeString(result.ScreenID),
        EscapeString(result.DeviceID),
        result.PlatformID,
        EscapeString(result.ModelName),
        EscapeString(result.UniqueID),
        EscapeString(result.MarkID),
        EscapeString(result.MainAoiFixID),
        result.StartTime.Format(_T("%Y-%m-%d %H:%M:%S")),
        result.StopTime.Format(_T("%Y-%m-%d %H:%M:%S")),
        EscapeString(result.Status),
        EscapeString(result.AOIResult),
        result.LocateShiftX,
        result.LocateShiftY,
        result.LocateAngle,
        (int)result.RawImageXLen,
        (int)result.RawImageYLen,
        (int)result.GridImageXLen,
        (int)result.GridImageYLen,
        result.PanelPhysicalXLen,
        result.PanelPhysicalYLen,
        EscapeString(result.Code_AOI),
        EscapeString(result.Grade_AOI),
        EscapeString(result.Level_AOI),
        EscapeString(result.DefClass_AOI),
        EscapeString(result.DefName_AOI),
        EscapeString(result.OperatorID),
        EscapeString(result.XMLInfo));

    // Write to IVS_LCD_InspectionResult first
    if (!ExecuteSQL(strSQL))
        return FALSE;

    // Then write to IVS_LCD_IDMap
    return UpsertIDMap(result);
}

///////////////////////////////////////////////////////////////////////////////
// Update manual review result
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::UpdateManualReviewResult(const CString& strGUID,
                                             const CString& strResult,
                                             const CString& strCode,
                                             const CString& strGrade,
                                             const CString& strOperator)
{
    CString strSQL;
    strSQL.Format(
        _T("UPDATE IVS_LCD_InspectionResult SET ")
        _T("ReviewResult_Worker = '%s', ")
        _T("Code_ManualReview = '%s', ")
        _T("Grade_ManualReview = '%s', ")
        _T("Operator_ManualReview = '%s', ")
        _T("StopTime_ManualReview = %s ")
        _T("WHERE GUID = '%s'"),
        EscapeString(strResult),
        EscapeString(strCode),
        EscapeString(strGrade),
        EscapeString(strOperator),
        GetNowFunctionSQL(),
        EscapeString(strGUID));

    return ExecuteSQL(strSQL);
}

///////////////////////////////////////////////////////////////////////////////
// Update auto review result
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::UpdateAutoReviewResult(const CString& strGUID,
                                           const CString& strResult,
                                           const CString& strCode,
                                           const CString& strGrade)
{
    CString strSQL;
    strSQL.Format(
        _T("UPDATE IVS_LCD_InspectionResult SET ")
        _T("ReviewResult_Machine = '%s', ")
        _T("Code_AutoReview = '%s', ")
        _T("Grade_AutoReview = '%s', ")
        _T("StopTime_AutoReview = %s ")
        _T("WHERE GUID = '%s'"),
        EscapeString(strResult),
        EscapeString(strCode),
        EscapeString(strGrade),
        GetNowFunctionSQL(),
        EscapeString(strGUID));

    return ExecuteSQL(strSQL);
}

///////////////////////////////////////////////////////////////////////////////
// Insert defect record
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::InsertDefectInfo(const CDefectInfo& defect)
{
    // Only use ivs_lcd_aoidefect table
    return InsertDefectToTable(_T("ivs_lcd_aoidefect"), defect);
}

///////////////////////////////////////////////////////////////////////////////
// Batch insert defect records
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::InsertDefectInfoBatch(const CDefectInfoList& defects)
{
    for (size_t i = 0; i < defects.size(); i++)
    {
        if (!InsertDefectInfo(defects[i]))
        {
            return FALSE;
        }
    }
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// Query defects
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::QueryDefectsByParentGUID(const CString& strParentGUID, CDefectInfoList& defects)
{
    defects.clear();

    if (!m_bConnected)
    {
        m_strLastError = _T("Not connected to database");
        TRACE(_T("[QueryDefectsByParentGUID] Failed: not connected to DB, ParentGUID=%s\n"), (LPCTSTR)strParentGUID);
        //theApp.m_pTestLog->Info(_T("[QueryDefectsByParentGUID] Failed: not connected to DB, ParentGUID=%s"), (LPCTSTR)strParentGUID);
        return FALSE;
    }

	TRACE(_T("[QueryDefectsByParentGUID] Start query defects, ParentGUID=%s\n"), (LPCTSTR)strParentGUID);
    //theApp.m_pTestLog->Info(_T("[QueryDefectsByParentGUID] Start query defects, ParentGUID=%s"), (LPCTSTR)strParentGUID);

    if (!QueryDefectsByParentGUIDFromTable(_T("ivs_lcd_aoidefect"), strParentGUID, defects))
    {
        defects.clear();
        TRACE(_T("[QueryDefectsByParentGUID] Query from ivs_lcd_aoidefect table failed, ParentGUID=%s, Error=%s\n"),
            (LPCTSTR)strParentGUID, (LPCTSTR)m_strLastError);
        //theApp.m_pTestLog->Info(_T("[QueryDefectsByParentGUID] Query from ivs_lcd_aoidefect table failed, ParentGUID=%s, Error=%s"),
        //    (LPCTSTR)strParentGUID, (LPCTSTR)m_strLastError);
        return FALSE;
    }

    TRACE(_T("[QueryDefectsByParentGUID] Query completed, ParentGUID=%s, defect count=%d\n"), (LPCTSTR)strParentGUID, (int)defects.size());
    //theApp.m_pTestLog->Info(_T("[QueryDefectsByParentGUID] Query completed, ParentGUID=%s, defect count=%d"), (LPCTSTR)strParentGUID, (int)defects.size());
    for (int i = 0; i < (int)defects.size(); i++)
    {
        const CDefectInfo& def = defects[i];
        CString strDefLog;
        strDefLog.Format(_T("  [Defect %d] Type=%s, Pos_x=%d, Pos_y=%d, Pos_w=%d, Pos_h=%d, Code=%s, DefClass=%s, DefName=%s"),
            i, (LPCTSTR)def.Type, def.Pos_x, def.Pos_y, def.Pos_width, def.Pos_height,
            (LPCTSTR)def.Code_AOI, (LPCTSTR)def.DefClass_AOI, (LPCTSTR)def.DefName_AOI);
        TRACE(_T("%s\n"), (LPCTSTR)strDefLog);
        //theApp.m_pTestLog->Info(strDefLog);
    }

    return TRUE;
}

BOOL CDBInterface::QueryDefectsByParentGUID_Vision(const CString& strParentGUID, CDefectInfoList& defects)
{
    defects.clear();

    if (!m_bConnected)
    {
        m_strLastError = _T("Not connected to database");
        TRACE(_T("[QueryDefectsByParentGUID] Failed: not connected to DB, ParentGUID=%s\n"), (LPCTSTR)strParentGUID);
        theApp.m_VisionLog->Info(_T("[QueryDefectsByParentGUID] Failed: not connected to DB, ParentGUID=%s"), (LPCTSTR)strParentGUID);
        return FALSE;
    }

	TRACE(_T("[QueryDefectsByParentGUID] Start query defects, ParentGUID=%s\n"), (LPCTSTR)strParentGUID);
    theApp.m_VisionLog->Info(_T("[QueryDefectsByParentGUID] Start query defects, ParentGUID=%s"), (LPCTSTR)strParentGUID);

    if (!QueryDefectsByParentGUIDFromTable(_T("ivs_lcd_aoidefect"), strParentGUID, defects))
    {
        defects.clear();
        TRACE(_T("[QueryDefectsByParentGUID] Query from ivs_lcd_aoidefect table failed, ParentGUID=%s, Error=%s\n"),
            (LPCTSTR)strParentGUID, (LPCTSTR)m_strLastError);
        theApp.m_VisionLog->Info(_T("[QueryDefectsByParentGUID] Query from ivs_lcd_aoidefect table failed, ParentGUID=%s, Error=%s"),
            (LPCTSTR)strParentGUID, (LPCTSTR)m_strLastError);
        return FALSE;
    }

    TRACE(_T("[QueryDefectsByParentGUID] Query completed, ParentGUID=%s, defect count=%d\n"), (LPCTSTR)strParentGUID, (int)defects.size());
    theApp.m_VisionLog->Info(_T("[QueryDefectsByParentGUID] Query completed, ParentGUID=%s, defect count=%d"), (LPCTSTR)strParentGUID, (int)defects.size());
    for (int i = 0; i < (int)defects.size(); i++)
    {
        const CDefectInfo& def = defects[i];
        CString strDefLog;
        strDefLog.Format(_T("  [Defect %d] Type=%s, Pos_x=%d, Pos_y=%d, Pos_w=%d, Pos_h=%d, Code=%s, DefClass=%s, DefName=%s"),
            i, (LPCTSTR)def.Type, def.Pos_x, def.Pos_y, def.Pos_width, def.Pos_height,
            (LPCTSTR)def.Code_AOI, (LPCTSTR)def.DefClass_AOI, (LPCTSTR)def.DefName_AOI);
        TRACE(_T("%s\n"), (LPCTSTR)strDefLog);
        theApp.m_VisionLog->Info(strDefLog);
    }

    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// Delete defects
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::DeleteDefectsByParentGUID(const CString& strParentGUID)
{
    // Only use ivs_lcd_aoidefect table
    return DeleteDefectsByParentGUIDFromTable(_T("ivs_lcd_aoidefect"), strParentGUID);
}

///////////////////////////////////////////////////////////////////////////////
// Query by GUID
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::QueryByGUID(const CString& strGUID, CInspectionResult& result)
{
    CString strSQL;
    strSQL.Format(
        _T("SELECT * FROM IVS_LCD_InspectionResult WHERE GUID = '%s'"),
        EscapeString(strGUID));

    SQLHSTMT hStmt;
    if (!ExecuteQuery(strSQL, hStmt))
        return FALSE;

    SQLRETURN ret = SQLFetch(hStmt);
    if (ret == SQL_NO_DATA)
    {
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return FALSE;
    }

    if (ret == SQL_ERROR)
    {
        m_strLastError = GetODBCError(SQL_HANDLE_STMT, hStmt);
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return FALSE;
    }

    result.GUID = GetColumnString(hStmt, 2);
    result.ScreenID = GetColumnString(hStmt, 3);
    result.DeviceID = GetColumnString(hStmt, 4);
    result.UniqueID = GetColumnString(hStmt, 19);      // 19: UniqueID
    result.AOIResult = GetColumnString(hStmt, 12);    // 12: AOIResult
    result.Grade_AOI = GetColumnString(hStmt, 45);    // 45: Grade_AOI

    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// Query by UniqueID
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::QueryByUniqueID(const CString& strUniqueID, CInspectionResult& result)
{
    CString strSQL = GetSelectLatestByUniqueIDSQL(strUniqueID);

    SQLHSTMT hStmt;
    if (!ExecuteQuery(strSQL, hStmt))
        return FALSE;

    SQLRETURN ret = SQLFetch(hStmt);
    if (ret == SQL_NO_DATA)
    {
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return FALSE;
    }

    if (ret == SQL_ERROR)
    {
        m_strLastError = GetODBCError(SQL_HANDLE_STMT, hStmt);
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return FALSE;
    }

    result.SysID = GetColumnInt(hStmt, 1);         // 1: SysID
    result.GUID = GetColumnString(hStmt, 2);
    result.ScreenID = GetColumnString(hStmt, 3);
    result.PlatformID = GetColumnInt(hStmt, 5);    // 5: PlatformID
    result.UniqueID = GetColumnString(hStmt, 19);   // 19: UniqueID
    result.AOIResult = GetColumnString(hStmt, 12);     // 12: AOIResult
    result.Code_AOI = GetColumnString(hStmt, 44);     // 44: Code_AOI
    result.Grade_AOI = GetColumnString(hStmt, 45);     // 45: Grade_AOI
    // 30: GridImageXLen, 31: GridImageYLen, 32: PanelPhysicalXLen, 33: PanelPhysicalYLen
    result.GridImageXLen = GetColumnInt(hStmt, 30);
    result.GridImageYLen = GetColumnInt(hStmt, 31);
    result.PanelPhysicalXLen = GetColumnDouble(hStmt, 32);
    result.PanelPhysicalYLen = GetColumnDouble(hStmt, 33);

    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// Query IVS_LCD_IDMap by MarkID (jig/station number) to get UniqueID/ScreenID
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::QueryIDMapByFixtureNo(int nFixtureNo, CIDMapInfo& idMapInfo)
{
    if (nFixtureNo < 1)
    {
        m_strLastError.Format(_T("Invalid fixture number: %d, expected >= 1"), nFixtureNo);
        return FALSE;
    }

    // Format MarkID as two-digit string (e.g., 1->'01', 7->'07', 12->'12')
    CString strMarkID;
    strMarkID.Format(_T("%02d"), nFixtureNo % 100);

    CString strSQL;
    strSQL.Format(
        _T("SELECT MarkID, MainAoiFixID, UniqueID, Barcode FROM IVS_LCD_IDMap WHERE MarkID = '%s'"),
        (LPCTSTR)strMarkID);

    SQLHSTMT hStmt;
    if (!ExecuteQuery(strSQL, hStmt))
        return FALSE;

    SQLRETURN ret = SQLFetch(hStmt);
    if (ret == SQL_NO_DATA)
    {
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        m_strLastError.Format(_T("No record found for MarkID: %s"), (LPCTSTR)strMarkID);
        return FALSE;
    }

    if (ret == SQL_ERROR)
    {
        m_strLastError = GetODBCError(SQL_HANDLE_STMT, hStmt);
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return FALSE;
    }

    idMapInfo.MarkID = GetColumnString(hStmt, 1);
    idMapInfo.MainAoiFixID = GetColumnString(hStmt, 2);
    idMapInfo.UniqueID = GetColumnString(hStmt, 3);
    idMapInfo.ScreenID = GetColumnString(hStmt, 4);  // Barcode stored in ScreenID field

    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// Query IVS_LCD_IDMap by PanelID/Barcode to get UniqueID
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::QueryIDMapByPanelID(const CString& strPanelID, CIDMapInfo& idMapInfo)
{
    CString strSQL;
    strSQL.Format(
        _T("SELECT * FROM IVS_LCD_IDMap WHERE Barcode = '%s' OR MarkID = '%s' LIMIT 1"),
        EscapeString(strPanelID), EscapeString(strPanelID));

    SQLHSTMT hStmt;
    if (!ExecuteQuery(strSQL, hStmt))
        return FALSE;

    SQLRETURN ret = SQLFetch(hStmt);
    if (ret == SQL_NO_DATA)
    {
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        m_strLastError = _T("No record found for PanelID: ") + strPanelID;
        return FALSE;
    }

    if (ret == SQL_ERROR)
    {
        m_strLastError = GetODBCError(SQL_HANDLE_STMT, hStmt);
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return FALSE;
    }

    idMapInfo.MarkID = GetColumnString(hStmt, 2);
    idMapInfo.UniqueID = GetColumnString(hStmt, 4);
    idMapInfo.ScreenID = GetColumnString(hStmt, 6);
    idMapInfo.MainAoiFixID = GetColumnString(hStmt, 7);

    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// Query inspection result by UniqueID and convert to DFS data format
///////////////////////////////////////////////////////////////////////////////
//BOOL CDBInterface::QueryInspectionResultForDFS(const CString& strUniqueID, DfsDataValue& dfsData)
//{
//    dfsData.Reset();
//
//    CString strSQL;
//    strSQL.Format(
//        _T("SELECT * FROM IVS_LCD_InspectionResult WHERE UniqueID = '%s' ORDER BY SysID DESC LIMIT 1"),
//        EscapeString(strUniqueID));
//
//    SQLHSTMT hStmt;
//    if (!ExecuteQuery(strSQL, hStmt))
//        return FALSE;
//
//    SQLRETURN ret = SQLFetch(hStmt);
//    if (ret == SQL_NO_DATA)
//    {
//        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//        m_strLastError = _T("No inspection result found for UniqueID: ") + strUniqueID;
//        return FALSE;
//    }
//
//    if (ret == SQL_ERROR)
//    {
//        m_strLastError = GetODBCError(SQL_HANDLE_STMT, hStmt);
//        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//        return FALSE;
//    }
//
//    CString strTemp;
//
//    dfsData.m_PanelID = GetColumnString(hStmt, 3);
//    dfsData.m_FpcID = dfsData.m_PanelID;
//    dfsData.m_StartTime = GetColumnString(hStmt, 10);
//    dfsData.m_EndTime = GetColumnString(hStmt, 11);
//    dfsData.m_StageNum = GetColumnInt(hStmt, 5) + 1;
//
//    strTemp = GetColumnString(hStmt, 14);
//    if (strTemp.CompareNoCase(_T("OK")) == 0)
//        dfsData.m_AOIInpsect = _T("OK");
//    else if (strTemp.IsEmpty() || strTemp.CompareNoCase(_T("NG")) == 0)
//        dfsData.m_AOIInpsect = _T("NG");
//    else
//        dfsData.m_AOIInpsect = _T("NG");
//
//    dfsData.m_ModelID = _T("");
//    dfsData.m_IndexNum = strTemp;
//    dfsData.m_ChNum = _T("1");
//
//    dfsData.m_Contact = _T("BYPASS");
//    dfsData.m_PreGamma = _T("BYPASS");
//    dfsData.m_TpResult = _T("BYPASS");
//    dfsData.m_TpResult2 = _T("BYPASS");
//    dfsData.m_Lumitop = _T("BYPASS");
//    dfsData.m_mura = _T("BYPASS");
//    dfsData.m_opViewResult = _T("BYPASS");
//
//    dfsData.m_TpTime = _T("0");
//    dfsData.m_PreGammaTime = _T("0");
//    dfsData.m_TactTime = _T("0");
//    dfsData.m_LoadHandlerTime = _T("");
//    dfsData.m_UnloadHandlerTime = _T("");
//    dfsData.m_PreGammaContactStatus = _T("3");
//
//    dfsData.m_TypeNum = 1;
//
//    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//    return TRUE;
//}

///////////////////////////////////////////////////////////////////////////////
// Query by panel barcode
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::QueryByBarcode(const CString& strBarcode, CInspectionResultList& results)
{
    results.clear();

    CString strSQL;
    strSQL.Format(
        _T("SELECT SysID, GUID, ScreenID, DeviceID, PlatformID, LocalIP, UniqueID, AOIResult, Grade_AOI, StartTime ")
        _T("FROM IVS_LCD_InspectionResult WHERE ScreenID = '%s' ORDER BY StartTime DESC"),
        EscapeString(strBarcode));

    SQLHSTMT hStmt;
    if (!ExecuteQuery(strSQL, hStmt))
        return FALSE;

    SQLRETURN ret;
    while ((ret = SQLFetch(hStmt)) != SQL_NO_DATA)
    {
        if (ret == SQL_ERROR)
        {
            m_strLastError = GetODBCError(SQL_HANDLE_STMT, hStmt);
            SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
            return FALSE;
        }

        CInspectionResult result;
        result.SysID = GetColumnInt(hStmt, 1);         // 1: SysID
        result.GUID = GetColumnString(hStmt, 2);        // 2: GUID
        result.ScreenID = GetColumnString(hStmt, 3);    // 3: ScreenID
        result.DeviceID = GetColumnString(hStmt, 4);    // 4: DeviceID
        result.PlatformID = GetColumnInt(hStmt, 5);     // 5: PlatformID
        result.LocalIP = GetColumnString(hStmt, 6);     // 6: LocalIP
        result.UniqueID = GetColumnString(hStmt, 7);    // 7: UniqueID
        result.AOIResult = GetColumnString(hStmt, 8);   // 8: AOIResult
        result.Grade_AOI = GetColumnString(hStmt, 9);   // 9: Grade_AOI

        // 解析时间字符串，处理毫秒 ".920" 后缀
        CString strStartTime = GetColumnString(hStmt, 10);  // 10: StartTime
        int nDot = strStartTime.Find('.');
        if (nDot >= 0)
            strStartTime = strStartTime.Left(nDot);  // 去掉毫秒部分
        result.StartTime.ParseDateTime(strStartTime);

        results.push_back(result);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// Query defect code by panel barcode (for SetLoadResultCode)
// Query priority: Code_ManualReview > Code_AutoReview > Code_AOI
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::QueryDefectCodeByBarcode(const CString& strBarcode, CString& strCode, CString& strGrade)
{
    strCode = _T("");
    strGrade = _T("");

    CString strSQL;
    strSQL.Format(
        _T("SELECT Code_ManualReview, Grade_ManualReview, ")
        _T("       Code_AutoReview, Grade_AutoReview, ")
        _T("       Code_AOI, Grade_AOI ")
        _T("FROM IVS_LCD_InspectionResult ")
        _T("WHERE ScreenID = '%s' ")
        _T("ORDER BY StartTime DESC LIMIT 1"),
        EscapeString(strBarcode));

    TRACE(_T("QueryDefectCodeByBarcode SQL: %s\n"), strSQL);

    SQLHSTMT hStmt;
    if (!ExecuteQuery(strSQL, hStmt))
        return FALSE;

    if (hStmt == SQL_NULL_HSTMT)
    {
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return FALSE;
    }

    SQLRETURN ret = SQLFetch(hStmt);
    if (ret == SQL_NO_DATA)
    {
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        m_strLastError = _T("No inspection result found for this Barcode");

        strCode = _T("XPOXSD");
        strGrade = _T("Y5");

        return FALSE;
    }

    if (ret == SQL_ERROR)
    {
        m_strLastError = GetODBCError(SQL_HANDLE_STMT, hStmt);
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);

        strCode = _T("XPOXSD");
        strGrade = _T("Y5");
        return FALSE;
    }

    CString strCodeManual = GetColumnString(hStmt, 1);
    CString strGradeManual = GetColumnString(hStmt, 2);
    CString strCodeAuto = GetColumnString(hStmt, 3);
    CString strGradeAuto = GetColumnString(hStmt, 4);
    CString strCodeAoi = GetColumnString(hStmt, 5);
    CString strGradeAoi = GetColumnString(hStmt, 6);

    if (!strCodeManual.IsEmpty())
    {
        strCode = strCodeManual;
        strGrade = strGradeManual;
    }
    else if (!strCodeAuto.IsEmpty())
    {
        strCode = strCodeAuto;
        strGrade = strGradeAuto;
    }
    else if (!strCodeAoi.IsEmpty())
    {
        strCode = strCodeAoi;
        strGrade = strGradeAoi;
    }

    // 当所有 Code 都为空时，设置默认值
    if (strCode.IsEmpty())
    {
        strCode = _T("XPOXSD");
        strGrade = _T("Y5");
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// Stub implementations for other methods
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::UpdateInspectionResult(const CInspectionResult& result)
{
    // TODO: Implement full update
    return FALSE;
}

BOOL CDBInterface::QueryByDateRange(const COleDateTime& dtStart,
                                     const COleDateTime& dtEnd,
                                     CInspectionResultList& results)
{
    // TODO: Implement date range query
    return FALSE;
}

BOOL CDBInterface::GetDailyStatistics(const COleDateTime& dtStart,
                                       const COleDateTime& dtEnd,
                                       std::vector<DailyStatistics>& stats)
{
    // TODO: Implement statistics query
    return FALSE;
}

BOOL CDBInterface::GetDefectTypeDistribution(const CString& strParentGUID,
                                              std::vector<DefectTypeCount>& distribution)
{
    // TODO: Implement defect type statistics
    return FALSE;
}

BOOL CDBInterface::UpdateDefectReviewResult(int nSysID,
                                              const CString& strResult,
                                              const CString& strCode,
                                              const CString& strGrade)
{
    // TODO: Implement defect review result update
    return FALSE;
}
