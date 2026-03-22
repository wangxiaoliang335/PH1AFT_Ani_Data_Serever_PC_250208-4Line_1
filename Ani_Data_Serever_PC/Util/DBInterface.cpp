///////////////////////////////////////////////////////////////////////////////
// FILE : DBInterface.cpp
// 数据库操作接口实现
// 使用 MySQL Connector/C++ 连接 MySQL 数据库
///////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "DBInterface.h"
#include "DataModels.h"
#include "Migration.h"
#include <objbase.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// Helper function to convert CString to std::string
static std::string CStringToString(const CString& str)
{
    CT2A pszStr(str);
    return std::string(pszStr);
}

// Helper function to convert std::string to CString
static CString StringToCString(const std::string& str)
{
    return CString(str.c_str());
}

// Helper function to convert exception message to CString
static CString ExceptionToCString(sql::SQLException& e)
{
    return StringToCString(std::string(e.what()));
}

///////////////////////////////////////////////////////////////////////////////
// 全局实例
///////////////////////////////////////////////////////////////////////////////
static CDBInterface g_DBInterface;

CDBInterface& GetDBInterface()
{
    return g_DBInterface;
}

///////////////////////////////////////////////////////////////////////////////
// 构造/析构
///////////////////////////////////////////////////////////////////////////////
CDBInterface::CDBInterface()
    : m_bConnected(FALSE)
    , m_pConnection(nullptr)
{
    InitializeCriticalSection(&m_csDB);
}

CDBInterface::~CDBInterface()
{
    Disconnect();
    DeleteCriticalSection(&m_csDB);
}

///////////////////////////////////////////////////////////////////////////////
// 连接数据库
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::Connect(const CString& strConnString)
{
	EnterCriticalSection(&m_csDB);

	if (m_bConnected)
	{
		Disconnect();
	}

	try
	{
		// 获取 MySQL Driver
		TRACE(_T("DBInterface: Getting MySQL driver...\n"));
		sql::Driver* pDriver = sql::mysql::get_mysql_driver_instance();
		if (!pDriver)
		{
			m_strLastError = _T("Failed to get MySQL driver");
			LeaveCriticalSection(&m_csDB);
			return FALSE;
		}
		TRACE(_T("DBInterface: Driver obtained: %s\n"), pDriver->getName().c_str());

		// 解析连接字符串 (格式: tcp://host:port/database?user=xxx&password=xxx)
		CString strConn = strConnString;
		TRACE(_T("DBInterface: Connection string: %s\n"), strConn);

		// 提取 host, port, database
		CString strHost = _T("localhost");
		int nPort = 3306;
		CString strDBName = _T("");

		// 先跳过 tcp://
		int nTcpPos = strConn.Find(_T("tcp://"));
		CString strTemp;
		if (nTcpPos >= 0)
			strTemp = strConn.Mid(nTcpPos + 6); // tcp:// 是6个字符，跳过
		else
			strTemp = strConn;

		// 找 / 或 ? 的位置
		int nSlash = strTemp.Find(_T("/"));
		int nQues = strTemp.Find(_T("?"));

		// host:port 部分
		CString strHostPort;
		if (nSlash > 0 && nQues > 0)
			strHostPort = strTemp.Left(min(nSlash, nQues));
		else if (nSlash > 0)
			strHostPort = strTemp.Left(nSlash);
		else if (nQues > 0)
			strHostPort = strTemp.Left(nQues);
		else
			strHostPort = strTemp;

		// 从 host:port 分离
		int nColon = strHostPort.ReverseFind(_T(':'));
		if (nColon > 0)
		{
			strHost = strHostPort.Left(nColon);
			CString strPortStr = strHostPort.Mid(nColon + 1);
			nPort = _ttoi(strPortStr);
		}
		else
		{
			strHost = strHostPort;
		}

		// 提取 database (在 / 和 ? 之间)
		if (nSlash > 0)
		{
			CString strAfterSlash;
			if (nQues > 0 && nQues > nSlash)
				strAfterSlash = strTemp.Mid(nSlash + 1, nQues - nSlash - 1);
			else
				strAfterSlash = strTemp.Mid(nSlash + 1);
			strDBName = strAfterSlash;
		}

		// 提取 user 和 password
		CString strUser = _T("root");
		CString strPass = _T("");

		int nPos = strConn.Find(_T("user="));
		if (nPos >= 0)
		{
			CString strAfterUser = strConn.Mid(nPos + 5);
			int nAmp = strAfterUser.Find(_T("&"));
			if (nAmp > 0)
				strUser = strAfterUser.Left(nAmp);
			else
				strUser = strAfterUser;
		}

		nPos = strConn.Find(_T("password="));
		if (nPos >= 0)
		{
			CString strAfterPass = strConn.Mid(nPos + 9);
			int nAmp = strAfterPass.Find(_T("&"));
			if (nAmp > 0)
				strPass = strAfterPass.Left(nAmp);
			else
				strPass = strAfterPass;
		}

		TRACE(_T("DBInterface: Parsed host: %s, port: %d, db: %s, user: %s\n"),
			strHost, nPort, strDBName, strUser);

		// 转换为 std::string
		std::string hostStr = CStringToString(strHost);
		std::string userStr = CStringToString(strUser);
		std::string passStr = CStringToString(strPass);
		std::string dbStr = CStringToString(strDBName);

		// 使用 ConnectOptionsMap 连接
		sql::ConnectOptionsMap connectionProperties;
		connectionProperties[OPT_HOSTNAME] = hostStr.c_str();
		connectionProperties[OPT_PORT] = nPort;
		connectionProperties[OPT_USERNAME] = userStr.c_str();
		connectionProperties[OPT_PASSWORD] = passStr.c_str();
		if (!dbStr.empty())
		{
			connectionProperties[OPT_SCHEMA] = dbStr.c_str();
		}

		TRACE(_T("DBInterface: Connecting to %s:%d...\n"), strHost, nPort);

		// 连接数据库
		m_pConnection = pDriver->connect(connectionProperties);
		if (!m_pConnection)
		{
			m_strLastError = _T("Failed to connect to MySQL");
			LeaveCriticalSection(&m_csDB);
			return FALSE;
		}

		m_pConnection->setAutoCommit(true);
		m_bConnected = TRUE;
		TRACE(_T("DBInterface: Connected to MySQL database\n"));
	}
	catch (sql::SQLException& e)
	{
		std::string errMsg = e.what();
		m_strLastError = StringToCString(errMsg);
		TRACE(_T("DBInterface: Connection failed - %s\n"), m_strLastError);
		m_bConnected = FALSE;
	}
	catch (std::bad_alloc& e)
	{
		m_strLastError = _T("Memory allocation failed - MySQL driver issue");
		TRACE(_T("DBInterface: bad_alloc - %s\n"), e.what());
		m_bConnected = FALSE;
	}

	LeaveCriticalSection(&m_csDB);
	return m_bConnected;
}

///////////////////////////////////////////////////////////////////////////////
// 断开连接
///////////////////////////////////////////////////////////////////////////////
void CDBInterface::Disconnect()
{
    EnterCriticalSection(&m_csDB);

    if (m_bConnected && m_pConnection)
    {
        try
        {
            m_pConnection->close();
            delete m_pConnection;
            m_pConnection = nullptr;
        }
    catch (sql::SQLException& e)
    {
        std::string errMsg = e.what();
        m_strLastError = StringToCString(errMsg);
        TRACE(_T("DBInterface: Close error - %s\n"), m_strLastError);
    }
        m_bConnected = FALSE;
    }

    LeaveCriticalSection(&m_csDB);
}

///////////////////////////////////////////////////////////////////////////////
// 执行SQL语句
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::ExecuteSQL(const CString& strSQL)
{
    if (!m_bConnected || !m_pConnection)
    {
        m_strLastError = _T("Not connected to database");
        return FALSE;
    }

    try
    {
        std::unique_ptr<sql::Statement> pStmt(m_pConnection->createStatement());
        std::string sqlStr = CStringToString(strSQL);
        pStmt->execute(sqlStr);
        return TRUE;
    }
    catch (sql::SQLException& e)
    {
        m_strLastError = ExceptionToCString(e);
        TRACE(_T("DBInterface: SQL Error - %s\nSQL: %s\n"), m_strLastError, strSQL);
        return FALSE;
    }
}

///////////////////////////////////////////////////////////////////////////////
// 生成GUID
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
// 生成检测唯一ID（YYYY_MM_DD_HH_MM_SS_fff_JJ，保证不重复）
///////////////////////////////////////////////////////////////////////////////
CString CDBInterface::GenerateUniqueIDForJig(int jigNum)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    CString strJig;
    strJig.Format(_T("%02d"), (jigNum >= 0 && jigNum <= 3) ? (jigNum + 1) : 1);
    CString strUniqueID;
    strUniqueID.Format(_T("%04d_%02d_%02d_%02d_%02d_%02d_%03d_%s"),
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        strJig);
    return strUniqueID;
}

///////////////////////////////////////////////////////////////////////////////
// 发送开始检测前更新 ivs_lcd_idmap
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::UpsertIDMapBeforeStart(const CString& markID, int posID, const CString& uniqueID,
                                          const CString& barcode, const CString& mainAoiFixID)
{
    if (!m_bConnected)
        return FALSE;
    EnterCriticalSection(&m_csDB);

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

    BOOL bRet = ExecuteSQL(strSQL);
    LeaveCriticalSection(&m_csDB);
    return bRet;
}

///////////////////////////////////////////////////////////////////////////////
// 转义SQL字符串
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
        _T("SELECT * FROM %s WHERE GUID_IVS_LCD_InspectionResult = '%s' ORDER BY DefectIndex"),
        strTableName,
        EscapeString(strParentGUID));

    if (!m_bConnected || !m_pConnection)
        return FALSE;

    try
    {
        std::unique_ptr<sql::Statement> pStmt(m_pConnection->createStatement());
        std::string sqlStr = CStringToString(strSQL);
        std::unique_ptr<sql::ResultSet> pRes(pStmt->executeQuery(sqlStr));

        while (pRes->next())
        {
            CDefectInfo defect;
            defect.GUID_Parent = strParentGUID;
            defect.DefectIndex = pRes->getInt("DefectIndex");
            defect.Type = StringToCString(pRes->getString("Type").c_str());
            defect.PatternID = pRes->getInt("PatternID");
            defect.PatternName = StringToCString(pRes->getString("PatternName").c_str());
            defect.InspType = StringToCString(pRes->getString("InspType").c_str());
            defect.Pos_x = pRes->getInt("Pos_x");
            defect.Pos_y = pRes->getInt("Pos_y");
            defect.Pos_width = pRes->getInt("Pos_width");
            defect.Pos_height = pRes->getInt("Pos_height");
            defect.TrueSize = pRes->getDouble("TrueSize");
            defect.TrueDiameter = pRes->getDouble("TrueDiameter");
            defect.TrueLongSize = pRes->getDouble("TrueLongSize");
            defect.TrueShortSize = pRes->getDouble("TrueShortSize");
            defect.OriArea = pRes->getInt("OriArea");
            defect.OriLongSize = pRes->getDouble("OriLongSize");
            defect.OriShortSize = pRes->getDouble("OriShortSize");
            defect.GrayScale = pRes->getInt("GrayScale");
            defect.GrayScale_BK = pRes->getInt("GrayScale_BK");
            defect.GrayScaleDiff = pRes->getDouble("GrayScaleDiff");
            defect.GrayscaleMean = pRes->getDouble("GrayscaleMean");
            defect.GrayscaleMin = pRes->getInt("GrayscaleMin");
            defect.GrayscaleMax = pRes->getInt("GrayscaleMax");
            defect.Area = pRes->getInt("Area");
            defect.Roundness = pRes->getDouble("Roundness");
            defect.MajorAxisAngle = pRes->getDouble("MajorAxisAngle");
            defect.JND = pRes->getDouble("JND");
            defect.Layer = StringToCString(pRes->getString("Layer").c_str());
            defect.Code_AOI = StringToCString(pRes->getString("Code_AOI").c_str());
            defect.Grade_AOI = StringToCString(pRes->getString("Grade_AOI").c_str());
            defect.Level_AOI = StringToCString(pRes->getString("Level_AOI").c_str());
            defect.DefClass_AOI = StringToCString(pRes->getString("DefClass_AOI").c_str());
            defect.DefName_AOI = StringToCString(pRes->getString("DefName_AOI").c_str());
            defect.AlgName = StringToCString(pRes->getString("AlgName").c_str());
            defect.AlgID = pRes->getInt("AlgID");
            defect.ReasonCode = StringToCString(pRes->getString("ReasonCode").c_str());
            defect.FeatureName = StringToCString(pRes->getString("FeatureName").c_str());
            defect.FeatureMin = StringToCString(pRes->getString("FeatureMin").c_str());
            defect.FeatureMax = StringToCString(pRes->getString("FeatureMax").c_str());
            defect.FeatureUnit = StringToCString(pRes->getString("FeatureUnit").c_str());
            defect.FeatureValue = StringToCString(pRes->getString("FeatureValue").c_str());
            defect.ImagePath = StringToCString(pRes->getString("ImagePath").c_str());
            defect.XMLInfo = StringToCString(pRes->getString("XMLInfo").c_str());

            defects.push_back(defect);
        }
        return TRUE;
    }
    catch (sql::SQLException& e)
    {
        m_strLastError = ExceptionToCString(e);
        TRACE(_T("DBInterface: Query defects from table failed - %s\n"), m_strLastError);
    }

    return FALSE;
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
// 插入检测结果
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::InsertInspectionResult(const CInspectionResult& result)
{
    EnterCriticalSection(&m_csDB);

    CString strSQL;
    strSQL.Format(
        _T("INSERT INTO IVS_LCD_InspectionResult ")
        _T("(GUID, ScreenID, DeviceID, PlatformID, ModelName, UniqueID, MarkID, MainAoiFixID, ")
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
        result.RawImageXLen,
        result.RawImageYLen,
        result.GridImageXLen,
        result.GridImageYLen,
        result.PanelPhysicalXLen,
        result.PanelPhysicalYLen,
        EscapeString(result.Code_AOI),
        EscapeString(result.Grade_AOI),
        EscapeString(result.Level_AOI),
        EscapeString(result.DefClass_AOI),
        EscapeString(result.DefName_AOI),
        EscapeString(result.OperatorID),
        EscapeString(result.XMLInfo));

    // 先写入 IVS_LCD_InspectionResult
    BOOL bRet = ExecuteSQL(strSQL);

    // 再写入 IVS_LCD_IDMap
    if (bRet)
    {
        bRet = UpsertIDMap(result);
    }

    LeaveCriticalSection(&m_csDB);
    return bRet;
}

///////////////////////////////////////////////////////////////////////////////
// 更新人工复判结果
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::UpdateManualReviewResult(const CString& strGUID,
                                             const CString& strResult,
                                             const CString& strCode,
                                             const CString& strGrade,
                                             const CString& strOperator)
{
    EnterCriticalSection(&m_csDB);

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

    BOOL bRet = ExecuteSQL(strSQL);
    LeaveCriticalSection(&m_csDB);
    return bRet;
}

///////////////////////////////////////////////////////////////////////////////
// 更新自动复检结果
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::UpdateAutoReviewResult(const CString& strGUID,
                                           const CString& strResult,
                                           const CString& strCode,
                                           const CString& strGrade)
{
    EnterCriticalSection(&m_csDB);

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

    BOOL bRet = ExecuteSQL(strSQL);
    LeaveCriticalSection(&m_csDB);
    return bRet;
}

///////////////////////////////////////////////////////////////////////////////
// 插入缺陷记录
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::InsertDefectInfo(const CDefectInfo& defect)
{
    EnterCriticalSection(&m_csDB);
    // 新旧库表名兼容：先尝试旧表，再尝试新表。
    BOOL bRet = InsertDefectToTable(_T("IVS_LCD_AOIResult"), defect);
    if (!bRet)
    {
        bRet = InsertDefectToTable(_T("ivs_lcd_aoidefect"), defect);
    }

    LeaveCriticalSection(&m_csDB);
    return bRet;
}

///////////////////////////////////////////////////////////////////////////////
// 批量插入缺陷记录
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
// 查询缺陷
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::QueryDefectsByParentGUID(const CString& strParentGUID, CDefectInfoList& defects)
{
    defects.clear();

    if (!m_bConnected)
    {
        m_strLastError = _T("Not connected to database");
        return FALSE;
    }

    EnterCriticalSection(&m_csDB);

    if (!QueryDefectsByParentGUIDFromTable(_T("IVS_LCD_AOIResult"), strParentGUID, defects))
    {
        defects.clear();
        QueryDefectsByParentGUIDFromTable(_T("ivs_lcd_aoidefect"), strParentGUID, defects);
    }

    LeaveCriticalSection(&m_csDB);
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// 删除缺陷
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::DeleteDefectsByParentGUID(const CString& strParentGUID)
{
    EnterCriticalSection(&m_csDB);

    BOOL bRet = DeleteDefectsByParentGUIDFromTable(_T("IVS_LCD_AOIResult"), strParentGUID);
    if (!bRet)
    {
        bRet = DeleteDefectsByParentGUIDFromTable(_T("ivs_lcd_aoidefect"), strParentGUID);
    }

    LeaveCriticalSection(&m_csDB);
    return bRet;
}

///////////////////////////////////////////////////////////////////////////////
// 按GUID查询
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::QueryByGUID(const CString& strGUID, CInspectionResult& result)
{
    if (!m_bConnected || !m_pConnection)
    {
        m_strLastError = _T("Not connected to database");
        return FALSE;
    }

    EnterCriticalSection(&m_csDB);

    try
    {
        CString strSQL;
        strSQL.Format(
            _T("SELECT * FROM IVS_LCD_InspectionResult WHERE GUID = '%s'"),
            EscapeString(strGUID));

        std::unique_ptr<sql::Statement> pStmt(m_pConnection->createStatement());
        std::string sqlStr = CStringToString(strSQL);
        std::unique_ptr<sql::ResultSet> pRes(pStmt->executeQuery(sqlStr));

        if (!pRes->next())
        {
            LeaveCriticalSection(&m_csDB);
            return FALSE;
        }

        // 读取字段
        result.GUID = StringToCString(pRes->getString("GUID").c_str());
        result.ScreenID = StringToCString(pRes->getString("ScreenID").c_str());
        result.DeviceID = StringToCString(pRes->getString("DeviceID").c_str());
        result.UniqueID = StringToCString(pRes->getString("UniqueID").c_str());
        result.AOIResult = StringToCString(pRes->getString("AOIResult").c_str());
        result.Grade_AOI = StringToCString(pRes->getString("Grade_AOI").c_str());
    }
    catch (sql::SQLException& e)
    {
        m_strLastError = ExceptionToCString(e);
        LeaveCriticalSection(&m_csDB);
        return FALSE;
    }

    LeaveCriticalSection(&m_csDB);
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// 按UniqueID查询
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::QueryByUniqueID(const CString& strUniqueID, CInspectionResult& result)
{
    if (!m_bConnected || !m_pConnection)
    {
        m_strLastError = _T("Not connected to database");
        return FALSE;
    }

    EnterCriticalSection(&m_csDB);

    try
    {
        CString strSQL = GetSelectLatestByUniqueIDSQL(strUniqueID);

        std::unique_ptr<sql::Statement> pStmt(m_pConnection->createStatement());
        std::string sqlStr = CStringToString(strSQL);
        std::unique_ptr<sql::ResultSet> pRes(pStmt->executeQuery(sqlStr));

        if (!pRes->next())
        {
            LeaveCriticalSection(&m_csDB);
            return FALSE;
        }

        result.GUID = StringToCString(pRes->getString("GUID").c_str());
        result.ScreenID = StringToCString(pRes->getString("ScreenID").c_str());
        result.UniqueID = StringToCString(pRes->getString("UniqueID").c_str());
        result.AOIResult = StringToCString(pRes->getString("AOIResult").c_str());
    }
    catch (sql::SQLException& e)
    {
        m_strLastError = ExceptionToCString(e);
        LeaveCriticalSection(&m_csDB);
        return FALSE;
    }

    LeaveCriticalSection(&m_csDB);
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// 根据 PanelID/Barcode 查询 ivs_lcd_idmap 获取 UniqueID
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::QueryIDMapByPanelID(const CString& strPanelID, CIDMapInfo& idMapInfo)
{
    if (!m_bConnected || !m_pConnection)
    {
        m_strLastError = _T("Not connected to database");
        return FALSE;
    }

    EnterCriticalSection(&m_csDB);

    try
    {
        CString strSQL;
        strSQL.Format(
            _T("SELECT * FROM IVS_LCD_IDMap WHERE Barcode = '%s' OR MarkID = '%s' ORDER BY SysID DESC LIMIT 1"),
            EscapeString(strPanelID), EscapeString(strPanelID));

        std::unique_ptr<sql::Statement> pStmt(m_pConnection->createStatement());
        std::string sqlStr = CStringToString(strSQL);
        std::unique_ptr<sql::ResultSet> pRes(pStmt->executeQuery(sqlStr));

        if (!pRes->next())
        {
            LeaveCriticalSection(&m_csDB);
            m_strLastError = _T("No record found for PanelID: ") + strPanelID;
            return FALSE;
        }

        idMapInfo.SysID = pRes->getInt("SysID");
        idMapInfo.MarkID = StringToCString(pRes->getString("MarkID").c_str());
        idMapInfo.PosID = pRes->getInt("PosID");
        idMapInfo.UniqueID = StringToCString(pRes->getString("UniqueID").c_str());
        idMapInfo.ScreenID = StringToCString(pRes->getString("Barcode").c_str());
        idMapInfo.MainAoiFixID = StringToCString(pRes->getString("MainAoiFixID").c_str());
    }
    catch (sql::SQLException& e)
    {
        m_strLastError = ExceptionToCString(e);
        LeaveCriticalSection(&m_csDB);
        return FALSE;
    }

    LeaveCriticalSection(&m_csDB);
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// 按UniqueID查询检测结果并转换为DFS数据格式
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::QueryInspectionResultForDFS(const CString& strUniqueID, DfsDataValue& dfsData)
{
    dfsData.Reset();
    if (!m_bConnected || !m_pConnection)
    {
        m_strLastError = _T("Not connected to database");
        return FALSE;
    }

    EnterCriticalSection(&m_csDB);

    try
    {
        CString strSQL;
        strSQL.Format(
            _T("SELECT * FROM IVS_LCD_InspectionResult WHERE UniqueID = '%s' ORDER BY SysID DESC LIMIT 1"),
            EscapeString(strUniqueID));

        std::unique_ptr<sql::Statement> pStmt(m_pConnection->createStatement());
        std::string sqlStr = CStringToString(strSQL);
        std::unique_ptr<sql::ResultSet> pRes(pStmt->executeQuery(sqlStr));

        if (!pRes->next())
        {
            LeaveCriticalSection(&m_csDB);
            m_strLastError = _T("No inspection result found for UniqueID: ") + strUniqueID;
            return FALSE;
        }

        // 映射数据库字段到 DFS 格式
        CString strTemp;

        dfsData.m_PanelID = StringToCString(pRes->getString("ScreenID").c_str());       // 屏二维码
        dfsData.m_FpcID = dfsData.m_PanelID;                                  // FPC ID 同屏二维码

        dfsData.m_StartTime = StringToCString(pRes->getString("StartTime").c_str());     // 检测开始时间
        dfsData.m_EndTime = StringToCString(pRes->getString("StopTime").c_str());        // 检测结束时间

        dfsData.m_StageNum = pRes->getInt("PlatformID") + 1;                  // 槽位号 1-4

        strTemp = StringToCString(pRes->getString("AOIResult").c_str());
        // AOIResult: OK/NG/BrightDot/... → 转换为 OK/NG/BYPASS
        if (strTemp.CompareNoCase(_T("OK")) == 0)
            dfsData.m_AOIInpsect = _T("OK");
        else if (strTemp.IsEmpty() || strTemp.CompareNoCase(_T("NG")) == 0)
            dfsData.m_AOIInpsect = _T("NG");
        else
            dfsData.m_AOIInpsect = _T("NG");  // 其他异常按 NG 处理

        // 其他字段设为默认值或从 InspectionResult 获取
        dfsData.m_ModelID = _T("");              // 型号ID（从其他表或配置获取）
        dfsData.m_IndexNum = strTemp;             // 索引号
        dfsData.m_ChNum = _T("1");                // 通道号

        // 其他工序结果设为 BYPASS（因为只有 AOI 检测）
        dfsData.m_Contact = _T("BYPASS");
        dfsData.m_PreGamma = _T("BYPASS");
        dfsData.m_TpResult = _T("BYPASS");
        dfsData.m_TpResult2 = _T("BYPASS");
        dfsData.m_Lumitop = _T("BYPASS");
        dfsData.m_mura = _T("BYPASS");
        dfsData.m_opViewResult = _T("BYPASS");

        // 时间相关（只填 AOI 检测的时间）
        dfsData.m_TpTime = _T("0");
        dfsData.m_PreGammaTime = _T("0");
        dfsData.m_TactTime = _T("0");
        dfsData.m_LoadHandlerTime = _T("");
        dfsData.m_UnloadHandlerTime = _T("");
        dfsData.m_PreGammaContactStatus = _T("3");  // BYPASS

        // 设备类型：AOI 检测
        dfsData.m_TypeNum = 1;  // Machine_AOI = 1
    }
    catch (sql::SQLException& e)
    {
        m_strLastError = ExceptionToCString(e);
        LeaveCriticalSection(&m_csDB);
        return FALSE;
    }

    LeaveCriticalSection(&m_csDB);
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// 按屏二维码查询
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::QueryByScreenID(const CString& strScreenID, CInspectionResultList& results)
{
    results.clear();

    if (!m_bConnected || !m_pConnection)
    {
        m_strLastError = _T("Not connected to database");
        return FALSE;
    }

    EnterCriticalSection(&m_csDB);

    try
    {
        CString strSQL;
        strSQL.Format(
            _T("SELECT * FROM IVS_LCD_InspectionResult WHERE ScreenID = '%s' ORDER BY StartTime DESC"),
            EscapeString(strScreenID));

        std::unique_ptr<sql::Statement> pStmt(m_pConnection->createStatement());
        std::string sqlStr = CStringToString(strSQL);
        std::unique_ptr<sql::ResultSet> pRes(pStmt->executeQuery(sqlStr));

        while (pRes->next())
        {
            CInspectionResult result;
            result.GUID = StringToCString(pRes->getString("GUID").c_str());
            result.ScreenID = StringToCString(pRes->getString("ScreenID").c_str());
            result.UniqueID = StringToCString(pRes->getString("UniqueID").c_str());
            result.AOIResult = StringToCString(pRes->getString("AOIResult").c_str());
            result.Grade_AOI = StringToCString(pRes->getString("Grade_AOI").c_str());

            results.push_back(result);
        }
    }
    catch (sql::SQLException& e)
    {
        m_strLastError = ExceptionToCString(e);
        LeaveCriticalSection(&m_csDB);
        return FALSE;
    }

    LeaveCriticalSection(&m_csDB);
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// 按屏二维码查询缺陷码 (用于SetLoadResultCode)
// 查询优先级: Code_ManualReview > Code_AutoReview > Code_AOI
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::QueryDefectCodeByScreenID(const CString& strScreenID, CString& strCode, CString& strGrade)
{
    strCode = _T("");
    strGrade = _T("");

    if (!m_bConnected || !m_pConnection)
    {
        m_strLastError = _T("Not connected to database");
        return FALSE;
    }

    EnterCriticalSection(&m_csDB);

    try
    {
        CString strSQL;
        // 查询优先级: 人工复判 > 自动复检 > AOI主检
        strSQL.Format(
            _T("SELECT Code_ManualReview, Grade_ManualReview, ")
            _T("       Code_AutoReview, Grade_AutoReview, ")
            _T("       Code_AOI, Grade_AOI ")
            _T("FROM IVS_LCD_InspectionResult ")
            _T("WHERE ScreenID = '%s' AND Status = 'Finish' ")
            _T("ORDER BY StartTime DESC LIMIT 1"),
            EscapeString(strScreenID));

        std::unique_ptr<sql::Statement> pStmt(m_pConnection->createStatement());
        std::string sqlStr = CStringToString(strSQL);
        std::unique_ptr<sql::ResultSet> pRes(pStmt->executeQuery(sqlStr));

        if (pRes->next())
        {
            // 优先级: ManualReview > AutoReview > AOI
            CString strCodeManual = StringToCString(pRes->getString("Code_ManualReview").c_str());
            CString strGradeManual = StringToCString(pRes->getString("Grade_ManualReview").c_str());
            CString strCodeAuto = StringToCString(pRes->getString("Code_AutoReview").c_str());
            CString strGradeAuto = StringToCString(pRes->getString("Grade_AutoReview").c_str());
            CString strCodeAoi = StringToCString(pRes->getString("Code_AOI").c_str());
            CString strGradeAoi = StringToCString(pRes->getString("Grade_AOI").c_str());

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
        }
        else
        {
            m_strLastError = _T("No inspection result found for this ScreenID");
            LeaveCriticalSection(&m_csDB);
            return FALSE;
        }
    }
    catch (sql::SQLException& e)
    {
        m_strLastError = ExceptionToCString(e);
        LeaveCriticalSection(&m_csDB);
        return FALSE;
    }

    LeaveCriticalSection(&m_csDB);
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// 其他方法的存根实现
///////////////////////////////////////////////////////////////////////////////
BOOL CDBInterface::UpdateInspectionResult(const CInspectionResult& result)
{
    // TODO: 实现完整更新
    return FALSE;
}

BOOL CDBInterface::QueryByDateRange(const COleDateTime& dtStart,
                                     const COleDateTime& dtEnd,
                                     CInspectionResultList& results)
{
    // TODO: 实现日期范围查询
    return FALSE;
}

BOOL CDBInterface::GetDailyStatistics(const COleDateTime& dtStart,
                                       const COleDateTime& dtEnd,
                                       std::vector<DailyStatistics>& stats)
{
    // TODO: 实现统计查询
    return FALSE;
}

BOOL CDBInterface::GetDefectTypeDistribution(const CString& strParentGUID,
                                              std::vector<DefectTypeCount>& distribution)
{
    // TODO: 实现缺陷类型统计
    return FALSE;
}

BOOL CDBInterface::UpdateDefectReviewResult(int nSysID,
                                              const CString& strResult,
                                              const CString& strCode,
                                              const CString& strGrade)
{
    // TODO: 实现缺陷复判结果更新
    return FALSE;
}
