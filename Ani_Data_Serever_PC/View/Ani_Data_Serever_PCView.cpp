
// Operrator_view_YoungView.cpp : CAni_Data_Serever_PCView Å¬·¡½ºÀÇ ±¸Çö
//

#include "stdafx.h"
// SHARED_HANDLERS´Â ¹Ì¸® º¸±â, Ãà¼ÒÆÇ ±×¸² ¹× °Ë»ö ÇÊÅÍ Ã³¸®±â¸¦ ±¸ÇöÇÏ´Â ATL ÇÁ·ÎÁ§Æ®¿¡¼­ Á¤ÀÇÇÒ ¼ö ÀÖÀ¸¸ç
// ÇØ´ç ÇÁ·ÎÁ§Æ®¿Í ¹®¼­ ÄÚµå¸¦ °øÀ¯ÇÏµµ·Ï ÇØ ÁÝ´Ï´Ù.
#ifndef SHARED_HANDLERS
#include "Ani_Data_Serever_PC.h"
#endif

#include "Ani_Data_Serever_PCDoc.h"
#include "Ani_Data_Serever_PCView.h"
#include "MainFrm.h"
#include "DataInfo.h"
//#include "core.h"
#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CAni_Data_Serever_PCView
IMPLEMENT_DYNCREATE(CAni_Data_Serever_PCView, CFormView)

// CAni_Data_Serever_PCView »ý¼º/¼Ò¸ê

CAni_Data_Serever_PCView::CAni_Data_Serever_PCView()
: CFormView(CAni_Data_Serever_PCView::IDD)
{
	// TODO: ¿©±â¿¡ »ý¼º ÄÚµå¸¦ Ãß°¡ÇÕ´Ï´Ù.
	m_pAlarmHistory = NULL;
	m_pTactTimeHistory = NULL;
	m_SetSystem = NULL;
	//theApp.m_CG16Setting = NULL;
	m_pMain = NULL;
	m_pInspect = NULL;
	m_pTimeInspet = NULL;
	m_pIdCardHistory = NULL;
	m_SetRank = NULL;
	m_SetVision = NULL;
#if _SYSTEM_AMTAFT_
	m_pMainLog = NULL;
#endif

}

CAni_Data_Serever_PCView::~CAni_Data_Serever_PCView()
{
#if _SYSTEM_AMTAFT_
	if (m_pMainLog != NULL)
		delete m_pMainLog;
#endif
	if (m_pMain != NULL)
		delete m_pMain;
	if (m_SetRank != NULL)
		delete m_SetRank;
	if (m_SetVision != NULL)
		delete m_SetVision;
	if (m_pAlarmHistory != NULL)
		delete m_pAlarmHistory;
	if (m_pInspect != NULL)
		delete m_pInspect;
	if (m_pTimeInspet != NULL)
		delete m_pTimeInspet;
	if (m_SetSystem != NULL)
		delete m_SetSystem;
	//if (theApp.m_CG16Setting != NULL)
	//	delete theApp.m_CG16Setting;
	if (m_SetTimerDlg != NULL)
		delete m_SetTimerDlg;
	if (m_pTactTimeHistory != NULL)
		delete m_pTactTimeHistory;
	if (m_pIdCardHistory != NULL)
		delete m_pIdCardHistory;
}

void CAni_Data_Serever_PCView::DoDataExchange(CDataExchange* pDX)
{
	CFormView::DoDataExchange(pDX);
	DDX_Control(pDX, IDB_BTN_KOR, m_btnKor);
	DDX_Control(pDX, IDB_BTN_ENG, m_btnEng);
	DDX_Control(pDX, IDB_BTN_CHI, m_btnChi);
	DDX_Control(pDX, IDB_BTN_DEFECT_COUNT, m_btnLabelName);
}

BOOL CAni_Data_Serever_PCView::PreCreateWindow(CREATESTRUCT& cs)
{
	return CFormView::PreCreateWindow(cs);
}

void CAni_Data_Serever_PCView::OnInitialUpdate()
{
	CFormView::OnInitialUpdate();
	pMainFrame = STATIC_DOWNCAST(CMainFrame, AfxGetMainWnd());

	CRect rc;		rc.SetRectEmpty();
	GetDlgItem(IDC_MAIN_FRM_DIS1)->GetWindowRect(&rc);
 
	CREATE_RECT_DLG(m_pAlarmHistory, CDlgAlarmHistory, DLG_ALARM_HISTORY, this, rc, SW_HIDE);
	CREATE_RECT_DLG(m_pTactTimeHistory, CDlgTactTimeHistory, DLG_TACT_TIME_HISTORY, this, rc, SW_HIDE);
	CREATE_RECT_DLG(m_pIdCardHistory, CDlgIdCardHistory, DLG_ID_CARD_READER_HISTORY, this, rc, SW_HIDE);
#if _SYSTEM_AMTAFT_
	CREATE_RECT_DLG(m_pMainLog, CDlgMainLog, DLG_MAIN_LOG_BOX, this, rc, SW_HIDE);
	CREATE_RECT_DLG(m_pMain, CDlgMainView, DLG_MAIN_BOX, this, rc, SW_HIDE);
	CREATE_RECT_DLG(m_pInspect, CDlgInspect, DLG_INSPECT_AMT_AFT, this, rc, SW_SHOW);
	CREATE_RECT_DLG(m_pTimeInspet, CDlgTimeInspect, DLG_TIME_INSPECT_AMT_AFT, this, rc, SW_HIDE);

	m_btnLabelName.SetCaption(_T("LOG2"));
	CBtnEnh *pBtnEnh = (CBtnEnh*)GetDlgItem(IDB_BTN_INSPECT);
	pBtnEnh->SetValue(TRUE);
#else
	CREATE_RECT_DLG(m_pMain, CDlgGammaMain, DLG_GAMMA_MAIN_LOG_BOX1, this, rc, SW_HIDE);
	CREATE_RECT_DLG(m_pInspect, CDlgGammaInspect, DLG_INSPECT_GAMMA, this, rc, SW_SHOW);
	CREATE_RECT_DLG(m_pTimeInspet, CDlgGammaTimeInspect, DLG_TIME_INSPECT_GAMMA, this, rc, SW_HIDE);

	CBtnEnh *pBtnEnh = (CBtnEnh*)GetDlgItem(IDB_BTN_INSPECT);
	pBtnEnh->SetValue(TRUE);

	CBtnEnh *pBtnRank = (CBtnEnh*)GetDlgItem(IDB_BTN_SETNGRANK);
	pBtnRank->ShowWindow(SW_HIDE);

	CBtnEnh *pBtnSetVision = (CBtnEnh*)GetDlgItem(IDB_BTN_SETVISION);
	pBtnSetVision->ShowWindow(SW_HIDE);

	CBtnEnh *pBtnDefectCount = (CBtnEnh*)GetDlgItem(IDB_BTN_DEFECT_COUNT);
	pBtnDefectCount->ShowWindow(SW_HIDE);
#endif
	m_SetRank = new CDlgSetRank;
	m_SetRank->Create(IDD_DLG_SETRANK, this);
	m_SetRank->ShowWindow(SW_HIDE);

	m_SetSystem = new CDLGSetSystem;
	m_SetSystem->Create(IDD_DLG_SETSYSTEM, this);
	m_SetSystem->ShowWindow(SW_HIDE);

	//m_DlgCG16Setting = new CDlgCG16Setting;
	//m_DlgCG16Setting->Create(IDD_DIALOG_CG16, this);
	//m_DlgCG16Setting->ShowWindow(SW_HIDE);

	//theApp.m_CG16Setting = new CCG16SETTING;
	//theApp.m_CG16Setting->Create(IDD_DIALOG_CG16SETTING, this);
	//theApp.m_CG16Setting->ShowWindow(SW_HIDE);
	

	m_SetVision = new CDLGSetVision;
	m_SetVision->Create(IDD_DLG_SETVISION, this);
	m_SetVision->ShowWindow(SW_HIDE);

	m_SetTimerDlg = new CSetTimerDlg;
	m_SetTimerDlg->Create(IDD_DLG_SET_TIMER, this);
	m_SetTimerDlg->ShowWindow(SW_HIDE);

	theApp.m_pMsgBox = new CMsgBox(0, _T(""));
	theApp.m_pMsgBox->Create(DLG_MSG_BOX, this);
	theApp.m_pMsgBox->ShowWindow(SW_HIDE);

	theApp.m_pMsgBoxAlarm = new CMsgBox(MS_OK, _T(""));
	theApp.m_pMsgBoxAlarm->Create(DLG_MSG_BOX, this);
	theApp.m_pMsgBoxAlarm->ShowWindow(SW_HIDE);

	switch (theApp.m_iLanguageSelect)
	{
	case KOR:m_btnKor.SetValue(TRUE); break;
	case ENG:m_btnEng.SetValue(TRUE); break;
	case CHI:m_btnChi.SetValue(TRUE); break;
	}
}


#ifdef _DEBUG
void CAni_Data_Serever_PCView::AssertValid() const
{
	CFormView::AssertValid();
}

void CAni_Data_Serever_PCView::Dump(CDumpContext& dc) const
{
	CFormView::Dump(dc);
}

#endif //_DEBUG

BEGIN_MESSAGE_MAP(CAni_Data_Serever_PCView, CFormView)
	//ON_BN_CLICKED(IDC_BUTTON1, &CAni_Data_Serever_PCView::OnBnClickedButton1)
	ON_WM_PAINT()
	ON_BN_CLICKED(IDC_BUTTON9, &CAni_Data_Serever_PCView::OnBnClickedButton9)
END_MESSAGE_MAP()


void CAni_Data_Serever_PCView::OnActivateView(BOOL bActivate, CView* pActivateView, CView* pDeactiveView)
{	
	CFormView::OnActivateView(bActivate, pActivateView, pDeactiveView);
}

void CAni_Data_Serever_PCView::OnBnClickedButton1()
{
	// TODO: ¿©±â¿¡ ÄÁÆ®·Ñ ¾Ë¸² Ã³¸®±â ÄÚµå¸¦ Ãß°¡ÇÕ´Ï´Ù.
	//Invalidate(TRUE);
	//UpdateData(false);
}


void CAni_Data_Serever_PCView::OnPaint()
{
	CPaintDC dc(this); // device context for painting
	// TODO: ¿©±â¿¡ ¸Þ½ÃÁö Ã³¸®±â ÄÚµå¸¦ Ãß°¡ÇÕ´Ï´Ù.
	CRect rc;		rc.SetRectEmpty();
	GetDlgItem(IDC_MAIN_FRM_DIS1)->GetWindowRect(&rc);

}

BEGIN_EVENTSINK_MAP(CAni_Data_Serever_PCView, CFormView)
	ON_EVENT(CAni_Data_Serever_PCView, IDB_BTN_START, DISPID_CLICK, CAni_Data_Serever_PCView::OnClickIdbBtnStart, VTS_NONE)
	ON_EVENT(CAni_Data_Serever_PCView, IDB_BTN_DEFECT_COUNT, DISPID_CLICK, CAni_Data_Serever_PCView::OnClickIdbBtnDefectCount, VTS_NONE)
	ON_EVENT(CAni_Data_Serever_PCView, IDB_BTN_INSPECT, DISPID_CLICK, CAni_Data_Serever_PCView::OnClickIdbBtnInspect, VTS_NONE)
	ON_EVENT(CAni_Data_Serever_PCView, IDB_BTN_ALARM, DISPID_CLICK, CAni_Data_Serever_PCView::OnClickIdbBtnAlarm, VTS_NONE)
	ON_EVENT(CAni_Data_Serever_PCView, IDB_BTN_KOR, DISPID_CLICK, CAni_Data_Serever_PCView::OnClickIdbBtnLanguage, VTS_NONE)
	ON_EVENT(CAni_Data_Serever_PCView, IDB_BTN_ENG, DISPID_CLICK, CAni_Data_Serever_PCView::OnClickIdbBtnLanguage, VTS_NONE)
	ON_EVENT(CAni_Data_Serever_PCView, IDB_BTN_CHI, DISPID_CLICK, CAni_Data_Serever_PCView::OnClickIdbBtnLanguage, VTS_NONE)
	ON_EVENT(CAni_Data_Serever_PCView, IDB_BTN_SETTIMER, DISPID_CLICK, CAni_Data_Serever_PCView::OnClickIdbBtnSetTimer, VTS_NONE)
	ON_EVENT(CAni_Data_Serever_PCView, IDB_BTN_SETNGRANK, DISPID_CLICK, CAni_Data_Serever_PCView::OnClickIdbBtnSetRank, VTS_NONE)
	ON_EVENT(CAni_Data_Serever_PCView, IDB_BTN_TACT_TIME, DISPID_CLICK, CAni_Data_Serever_PCView::ClickBtnTactTime, VTS_NONE)
	ON_EVENT(CAni_Data_Serever_PCView, IDB_BTN_TIME_INSPECT, DISPID_CLICK, CAni_Data_Serever_PCView::ClickBtnTimeInspect, VTS_NONE)
	ON_EVENT(CAni_Data_Serever_PCView, IDB_BTN_SETSYSTEM, DISPID_CLICK, CAni_Data_Serever_PCView::ClickBtnSetsystem, VTS_NONE)
	ON_EVENT(CAni_Data_Serever_PCView, IDB_BTN_CARD_READER, DISPID_CLICK, CAni_Data_Serever_PCView::ClickBtnCardReader, VTS_NONE)
	ON_EVENT(CAni_Data_Serever_PCView, IDB_BTN_SETVISION, DISPID_CLICK, CAni_Data_Serever_PCView::ClickBtnSetVision, VTS_NONE)
	ON_EVENT(CAni_Data_Serever_PCView, IDB_BTN_SETNGRANK2, DISPID_CLICK, CAni_Data_Serever_PCView::ClickBtnSetngrank2, VTS_NONE)
	ON_EVENT(CAni_Data_Serever_PCView, IDB_BTN_SETNGRANK3, DISPID_CLICK, CAni_Data_Serever_PCView::ClickBtnSetngrank3, VTS_NONE)
END_EVENTSINK_MAP()

void CAni_Data_Serever_PCView::OnClickIdbBtnStart()
{
	// TODO: ¿©±â¿¡ ¸Þ½ÃÁö Ã³¸®±â ÄÚµå¸¦ Ãß°¡ÇÕ´Ï´Ù.
#if _SYSTEM_AMTAFT_
	m_pMain->ShowWindow(SW_SHOW);
	m_pMainLog->ShowWindow(SW_HIDE);
	m_pInspect->ShowWindow(SW_HIDE);
	m_pAlarmHistory->ShowWindow(SW_HIDE);
	m_pTactTimeHistory->ShowWindow(SW_HIDE);
	m_pTimeInspet->ShowWindow(SW_HIDE);
	m_pIdCardHistory->ShowWindow(SW_HIDE);
#else
	m_pMain->ShowWindow(SW_SHOW);
	m_pInspect->ShowWindow(SW_HIDE);
	m_pAlarmHistory->ShowWindow(SW_HIDE);
	m_pTactTimeHistory->ShowWindow(SW_HIDE);
	m_pTimeInspet->ShowWindow(SW_HIDE);
	m_pIdCardHistory->ShowWindow(SW_HIDE);
#endif
}


void CAni_Data_Serever_PCView::OnClickIdbBtnDefectCount()
{
	// TODO: ¿©±â¿¡ ¸Þ½ÃÁö Ã³¸®±â ÄÚµå¸¦ Ãß°¡ÇÕ´Ï´Ù.
#if _SYSTEM_AMTAFT_
	m_pMain->ShowWindow(SW_HIDE);
	m_pMainLog->ShowWindow(SW_SHOW);
	m_pInspect->ShowWindow(SW_HIDE);
	m_pAlarmHistory->ShowWindow(SW_HIDE);
	m_pTactTimeHistory->ShowWindow(SW_HIDE);
	m_pTimeInspet->ShowWindow(SW_HIDE);
	m_pIdCardHistory->ShowWindow(SW_HIDE);
#else
	m_pMain->ShowWindow(SW_HIDE);
	m_pInspect->ShowWindow(SW_HIDE);
	m_pAlarmHistory->ShowWindow(SW_HIDE);
	m_pTactTimeHistory->ShowWindow(SW_HIDE);
	m_pTimeInspet->ShowWindow(SW_HIDE);
	m_pIdCardHistory->ShowWindow(SW_HIDE);
#endif
}

void CAni_Data_Serever_PCView::OnClickIdbBtnInspect()
{
#if _SYSTEM_AMTAFT_
	m_pMain->ShowWindow(SW_HIDE);
	m_pMainLog->ShowWindow(SW_HIDE);
	m_pInspect->ShowWindow(SW_SHOW);
	m_pAlarmHistory->ShowWindow(SW_HIDE);
	m_pTactTimeHistory->ShowWindow(SW_HIDE);
	m_pTimeInspet->ShowWindow(SW_HIDE);
	m_pIdCardHistory->ShowWindow(SW_HIDE);
#else
	m_pMain->ShowWindow(SW_HIDE);
	m_pInspect->ShowWindow(SW_SHOW);
	m_pAlarmHistory->ShowWindow(SW_HIDE);
	m_pTactTimeHistory->ShowWindow(SW_HIDE);
	m_pTimeInspet->ShowWindow(SW_HIDE);
	m_pIdCardHistory->ShowWindow(SW_HIDE);
#endif
}

void CAni_Data_Serever_PCView::OnClickIdbBtnAlarm()
{
#if _SYSTEM_AMTAFT_
	m_pMain->ShowWindow(SW_HIDE);
	m_pMainLog->ShowWindow(SW_HIDE);
	m_pInspect->ShowWindow(SW_HIDE);
	m_pAlarmHistory->ShowWindow(SW_SHOW);
	m_pTactTimeHistory->ShowWindow(SW_HIDE);
	m_pTimeInspet->ShowWindow(SW_HIDE);
	m_pIdCardHistory->ShowWindow(SW_HIDE);
#else
	m_pMain->ShowWindow(SW_HIDE);
	m_pInspect->ShowWindow(SW_HIDE);
	m_pAlarmHistory->ShowWindow(SW_SHOW);
	m_pTactTimeHistory->ShowWindow(SW_HIDE);
	m_pTimeInspet->ShowWindow(SW_HIDE);
	m_pIdCardHistory->ShowWindow(SW_HIDE);
#endif
}

void CAni_Data_Serever_PCView::ClickBtnTactTime()
{
#if _SYSTEM_AMTAFT_
	m_pMain->ShowWindow(SW_HIDE);
	m_pMainLog->ShowWindow(SW_HIDE);
	m_pInspect->ShowWindow(SW_HIDE);
	m_pAlarmHistory->ShowWindow(SW_HIDE);
	m_pTactTimeHistory->ShowWindow(SW_SHOW);
	m_pTimeInspet->ShowWindow(SW_HIDE);
	m_pIdCardHistory->ShowWindow(SW_HIDE);
#else
	m_pMain->ShowWindow(SW_HIDE);
	m_pInspect->ShowWindow(SW_HIDE);
	m_pAlarmHistory->ShowWindow(SW_HIDE);
	m_pTactTimeHistory->ShowWindow(SW_SHOW);
	m_pTimeInspet->ShowWindow(SW_HIDE);
	m_pIdCardHistory->ShowWindow(SW_HIDE);
#endif
}

void CAni_Data_Serever_PCView::ClickBtnTimeInspect()
{
#if _SYSTEM_AMTAFT_
	m_pMain->ShowWindow(SW_HIDE);
	m_pMainLog->ShowWindow(SW_HIDE);
	m_pInspect->ShowWindow(SW_HIDE);
	m_pAlarmHistory->ShowWindow(SW_HIDE);
	m_pTactTimeHistory->ShowWindow(SW_HIDE);
	m_pTimeInspet->ShowWindow(SW_SHOW);
	m_pIdCardHistory->ShowWindow(SW_HIDE);
#else
	m_pMain->ShowWindow(SW_HIDE);
	m_pInspect->ShowWindow(SW_HIDE);
	m_pAlarmHistory->ShowWindow(SW_HIDE);
	m_pTactTimeHistory->ShowWindow(SW_HIDE);
	m_pTimeInspet->ShowWindow(SW_SHOW);
	m_pIdCardHistory->ShowWindow(SW_HIDE);
#endif
}


void CAni_Data_Serever_PCView::ClickBtnCardReader()
{
#if _SYSTEM_AMTAFT_
	m_pMain->ShowWindow(SW_HIDE);
	m_pMainLog->ShowWindow(SW_HIDE);
	m_pInspect->ShowWindow(SW_HIDE);
	m_pAlarmHistory->ShowWindow(SW_HIDE);
	m_pTactTimeHistory->ShowWindow(SW_HIDE);
	m_pTimeInspet->ShowWindow(SW_HIDE);
	m_pIdCardHistory->ShowWindow(SW_SHOW);
#else
	m_pMain->ShowWindow(SW_HIDE);
	m_pInspect->ShowWindow(SW_HIDE);
	m_pAlarmHistory->ShowWindow(SW_HIDE);
	m_pTactTimeHistory->ShowWindow(SW_HIDE);
	m_pTimeInspet->ShowWindow(SW_HIDE);
	m_pIdCardHistory->ShowWindow(SW_SHOW);
#endif
}


void CAni_Data_Serever_PCView::OnClickIdbBtnLanguage()
{
	//theApp.m_OpvSocketManager[0].OpvInspectionResult(0, _T("321,0,123"));
	CBtnEnh *pBtnEnh = (CBtnEnh*)GetFocus();

	switch (pBtnEnh->GetDlgCtrlID())
	{
	case IDB_BTN_KOR: theApp.m_iLanguageSelect = KOR;  break;
	case IDB_BTN_ENG: theApp.m_iLanguageSelect = ENG; break;
	case IDB_BTN_CHI: theApp.m_iLanguageSelect = CHI; break;
	}

	EZIni ini(DATA_SYSTEM_DATA_PATH);
	ini[_T("DATA")][_T("LANGUAGAE")] = theApp.m_iLanguageSelect;
	theApp.LanguageChange();
}

void CAni_Data_Serever_PCView::OnClickIdbBtnSetTimer()
{
	if (theApp.m_iUserClass != USER_MAKER)
	{
		theApp.getMsgBox(MS_OK, _T("관리자만 사용가능 합니다."), _T("Maker is USE"), _T("Maker is USE"));
		return;
	}

	m_SetTimerDlg->ShowWindow(SW_SHOW);
}

void CAni_Data_Serever_PCView::OnClickIdbBtnSetRank()
{
	if (theApp.m_iUserClass != USER_MAKER)
	{
		theApp.getMsgBox(MS_OK, _T("관리자만 사용가능 합니다."), _T("Maker is USE"), _T("Maker is USE"));
		return;
	}

	m_SetRank->StringChanged();
	m_SetRank->ShowWindow(SW_SHOW);
}

void CAni_Data_Serever_PCView::ClickBtnSetsystem()
{
	if (theApp.m_iUserClass != USER_MAKER)
	{
		theApp.getMsgBox(MS_OK, _T("관리자만 사용가능 합니다."), _T("Maker is USE"), _T("Maker is USE"));
		return;
	}

	m_SetSystem->ShowWindow(SW_SHOW);
}

void CAni_Data_Serever_PCView::ClickBtnSetVision()
{
	if (theApp.m_iUserClass != USER_MAKER)
	{
		theApp.getMsgBox(MS_OK, _T("관리자만 사용가능 합니다."), _T("Maker is USE"), _T("Maker is USE"));
		return;
	}

	m_SetVision->ShowWindow(SW_SHOW);
}

bool sortTest(const TestStruct& a, const TestStruct& b)
{
	return a.iTest < b.iTest;
}


void CAni_Data_Serever_PCView::ClickBtnSetngrank2()
{
	TestStruct strucTest;
	for (int i = 10; i > 0; i--)
	{
		strucTest.iTest = i;
		for (int j = 0; j < 3; j++)
		{
			strucTest.sTest[j] = CStringSupport::FormatString(_T("%d_sTest_%d"), i + 1, j);
		}
		vTest.push_back(strucTest);
	}

	sort(vTest.begin(), vTest.end(), sortTest);

	//std::sort(vTest.begin(), vTest.end());


	int i = 0;



}


void CAni_Data_Serever_PCView::ClickBtnSetngrank3()
{
	// TODO: 여기에 메시지 처리기 코드를 추가합니다.

	


	DfsDataValue pTempDFSData;

	pTempDFSData.m_TypeNum = Machine_ULD;
	pTempDFSData.m_FpcID = _T("A7F59BW03CB48");
	pTempDFSData.m_PanelID = _T("A7F59BW03CB48");
	pTempDFSData.m_StartTime = _T("");
	pTempDFSData.m_EndTime = _T("");
	pTempDFSData.m_LoadHandlerTime = _T("");
	pTempDFSData.m_UnloadHandlerTime = _T("");
	pTempDFSData.m_TpTime = _T("");
	pTempDFSData.m_PreGammaTime = _T("");
	pTempDFSData.m_TactTime = _T("");
	pTempDFSData.m_PreGammaContactStatus = _T("3");
	pTempDFSData.m_ModelID = _T("");
	pTempDFSData.m_IndexNum = _T("3");
	pTempDFSData.m_ChNum = _T("1");
	pTempDFSData.m_TpResult = _T("0");

	theApp.m_pFTP->DfsAddTransferFile(pTempDFSData);

	/*SJobDataShop dd;
	theApp.m_pFS->AddTransferFile(dd);*/

	
}


void CAni_Data_Serever_PCView::SendPlcDefectCode(int iNum, DfsDataValue PanelData, int iType)
{
	map<CString, CString>::iterator iter;
	DefectCodeRank pDefectCodeRank;
	DefectGradeRank pDefectGradeRank;
	PLCSendDefect PlcSendDefect;
	CString strCodeGrade, strCode = _T(""), strGrade = _T(""), strPanelID = _T(""), strFpcID = _T("");
	strPanelID = PanelData.m_PanelID;
	strFpcID = PanelData.m_FpcID;
	int iCount = 0;
	if (iType == Machine_AOI)
	{
		if (_ttoi(PanelData.m_PreGammaContactStatus) == m_dfsContactNG)
			strCodeGrade = CStringSupport::FormatString(_T("%s^%s"), theApp.m_strContactNgCode, theApp.m_strContactNgGrade);
		else if (_ttoi(PanelData.m_TpResult) == m_dfsTpNG)
			strCodeGrade = CStringSupport::FormatString(_T("%s^%s"), _T("XIMXDE"), _T("R1"));
		else if (_ttoi(PanelData.m_PreGammaContactStatus) == m_dfsPreGammaNG)
			strCodeGrade = CStringSupport::FormatString(_T("%s^%s"), _T("XIMXPG"), _T("R1"));
		else
		{
			theApp.SetLoadResultCode(strPanelID, strFpcID);

			if (theApp.m_Send_Result_Code_Map.size() > 0)
			{
				for (auto Rank : theApp.m_VecRank[INDEX])
				{
					if (iCount == theApp.m_iNumberSendToPlc)
						break;

					iter = theApp.m_Send_Result_Code_Map.find(Rank.strCode);
					if (iter != theApp.m_Send_Result_Code_Map.end())
					{
						iCount++;
						if (iCount == 1)
							strGrade = iter->second;

						strCode.AppendFormat(_T("%s"), iter->first);
					}
				}
			}

			if (strCode.IsEmpty() == FALSE && strGrade.IsEmpty() == FALSE)
				strCodeGrade.Format(_T("%s^%s"), strCode, strGrade);
			else
				strCodeGrade = _T("");
		}
	}
	else
	{
		if (_ttoi(PanelData.m_PreGammaContactStatus) == m_dfsContactNG)
			strCodeGrade = CStringSupport::FormatString(_T("%s^%s"), theApp.m_strContactNgCode, theApp.m_strContactNgGrade);
		else if (_ttoi(PanelData.m_PreGammaContactStatus) == m_dfsPreGammaNG)
			strCodeGrade = CStringSupport::FormatString(_T("%s^%s"), _T("XIMXPG"), _T("R1"));
		else
			strCodeGrade = theApp.SetLoadOpvResultCode(strPanelID);
	}

	if (strCodeGrade.IsEmpty() == FALSE)
	{
		CStringArray responseTokens;
		CStringSupport::GetTokenArray(strCodeGrade, _T('^'), responseTokens);

		strCode = responseTokens[0];
		strGrade = responseTokens[1];

		PlcSendDefect.m_strCode = strCode;
		PlcSendDefect.m_strGrade = strGrade;
		PlcSendDefect.m_iCount = theApp.m_iNumberSendToPlc;
		theApp.SetSaveResultCode(strPanelID, strFpcID, _T("TotalDefectCode"), PlcSendDefect, iType);
	}
	else
	{
		PlcSendDefect.m_strCode = _T("");
		PlcSendDefect.m_strGrade = theApp.m_strOkGrade;
		PlcSendDefect.m_iCount = theApp.m_iNumberSendToPlc;
		theApp.SetSaveResultCode(strPanelID, strFpcID, _T("TotalDefectCode"), PlcSendDefect, iType);
	}

	theApp.m_Send_Result_Code_Map.clear();
}



#define From_Server    '@'

void IntToByte4(char* Buf, int Len, BOOL bLeftMost)
{
	if (!bLeftMost)
	{
		Buf[0] = (byte)((Len & 0xFF000000) >> 24);
		Buf[1] = (byte)((Len & 0x00FF0000) >> 16);
		Buf[2] = (byte)((Len & 0x0000FF00) >> 8);
		Buf[3] = (byte)(Len & 0x000000FF);
	}
	else
	{
		Buf[3] = (byte)((Len & 0xFF000000) >> 24);
		Buf[2] = (byte)((Len & 0x00FF0000) >> 16);
		Buf[1] = (byte)((Len & 0x0000FF00) >> 8);
		Buf[0] = (byte)(Len & 0x000000FF);
	}

}

void CAni_Data_Serever_PCView::OnBnClickedButton9()
{


	/*CString strFilePath, strTemp1;
	strTemp1 = DFS_SHARE_PATH + GetDateString2() + _T("\\") + _T("TEST");
	CreateFolders(strTemp1);
	strFilePath = CStringSupport::FormatString(_T("%s\\%s.txt"), strTemp1, _T("TEST"));
	EZIni ini(strFilePath);
	ini[_T("Sumeery_data")][_T("AOI")] = _T("AOI");
	ini[_T("Sumeery_data")][_T("CONTACT")] = _T("CONTACT");
	ini[_T("Sumeery_data")][_T("PREGAMMA")] = _T("PRE");
	ini[_T("Sumeery_data")][_T("TP")] = _T("TP");
	ini[_T("Sumeery_data")][_T("LUMITOP")] = _T("LUMI");


	CDFSInfo dfs;
	dfs.m_PanelSummaryInfo.strPanelID = _T("TEST");
	dfs.GetPanelSummaryInfo();*/
	CDFSInfo DfsInfo;
	DfsDataValue result;
	CString strFpcID, strPanelID, strFilePath, strCsvFilePath, strImageFilePath, strIndexFile, strIndexTempFilePath, strLinkFilePath, strAlramFilePath;
	CString strAOIPath, strViewingPath, strLumitopPath, strSumPath, strSumImagePath, strTemp1, strAoiImagePath, strViewingImagePath, strIndexFilePath, strOpvFilPath, strOriAlramPath;

	strPanelID = _T("TEST");
	strFpcID = _T("TEST");
	strAOIPath = DFS_SHARE_PATH + GetDateString2() + _T("\\") + strPanelID + _T("\\AOI\\") + strPanelID + _T(".csv");
	strViewingPath = DFS_SHARE_PATH + GetDateString2() + _T("\\") + strPanelID + _T("\\SUM\\") + strPanelID + _T(".csv");
	strOpvFilPath = DFS_SHARE_PATH + GetDateString2() + _T("\\") + strPanelID + _T("\\OPV\\") + strPanelID + _T(".csv");
	strSumPath = DFS_SHARE_PATH + GetDateString2() + _T("\\") + strPanelID + _T("\\SUM\\") + strPanelID + _T(".csv");
	strLumitopPath = DFS_SHARE_PATH + GetDateString2() + _T("\\") + strPanelID + _T("\\LUMITOP\\") + strPanelID + _T(".csv");

	theApp.m_pRankTread->AddRankCodeList(strPanelID, strPanelID, 1, 1, RankAOI);

	DfsInfo.AMTAFTSavePanelDFS_SUM(result, strPanelID, strFpcID, strAOIPath, strViewingPath, strLumitopPath, strOpvFilPath, strSumPath);
	DfsDataValue pDfsDateValue;

	pDfsDateValue.m_FpcID = strFpcID;
	pDfsDateValue.m_PanelID = strPanelID;

	pDfsDateValue.m_PreGammaContactStatus = _T("1");
	pDfsDateValue.m_TpResult = _T("1");

	SendPlcDefectCode(1, pDfsDateValue, Machine_AOI);

	/*map<int, int> Map;
	Map.insert(make_pair(1, 2));
	Map.insert(make_pair(1, 3));
	Map.insert(make_pair(1, 4));
	Map.insert(make_pair(1, 5));*/

	/*CDFSInfo dfs;
	CString strPanelID;
	strPanelID = _T("TEST");
	dfs.SetBCServerData(strPanelID, Machine_AOI, dfs);*/

}
