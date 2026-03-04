// DLGSetSystem.cpp : 구현 파일입니다.
//

#include "stdafx.h"
#include "Ani_Data_Serever_PC.h"
#include "DLGSetSystem.h"
#include "afxdialogex.h"
#include "DlgAlignCount.h"


// CDLGSetSystem 대화 상자입니다.

IMPLEMENT_DYNAMIC(CDLGSetSystem, CDialogEx)

CDLGSetSystem::CDLGSetSystem(CWnd* pParent /*=NULL*/)
	: CDialogEx(CDLGSetSystem::IDD, pParent)
{

}

CDLGSetSystem::~CDLGSetSystem()
{
}

void CDLGSetSystem::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDS_MECHINE_TYPE, m_MechineType);
	DDX_Control(pDX, IDS_EQP_ID, m_EqpId);
	DDX_Control(pDX, IDS_EQP_LINE_NUM, m_EqpNum);
	DDX_Control(pDX, IDS_COMPANY_LINE, m_CompanyLine);
	DDX_Control(pDX, IDS_IMAGE_WIDTH, m_OPV_ImageWidth);
	DDX_Control(pDX, IDS_IMAGE_HEIGHT, m_OPV_ImageHeight);
	DDX_Control(pDX, IDS_OK_GRADE, m_OK_Grade);
	//>> Index ok grad 220112
	DDX_Control(pDX, IDS_OK_GRADE2, m_INDEX_OK_Grade);
	
	DDX_Control(pDX, IDS_CONTACT_GRADE, m_ContactNg_Grade);
	DDX_Control(pDX, IDS_CONTACT_CODE, m_ContactNg_Code);

	DDX_Control(pDX, IDC_LIST_CG16_PARAM, m_ctrlCG16Param);
	
}
// CDLGSetSystem 메시지 처리기입니다.



BEGIN_EVENTSINK_MAP(CDLGSetSystem, CDialogEx)
	ON_EVENT(CDLGSetSystem, IDS_MECHINE_TYPE, DISPID_CLICK, CDLGSetSystem::GetNewContent, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDS_EQP_ID, DISPID_CLICK, CDLGSetSystem::GetNewContent, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDS_EQP_LINE_NUM, DISPID_CLICK, CDLGSetSystem::GetNewContent, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDS_COMPANY_LINE, DISPID_CLICK, CDLGSetSystem::GetNewContent, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDS_FILESERVER_ID, DISPID_CLICK, CDLGSetSystem::GetNewContent, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDS_IMAGE_WIDTH, DISPID_CLICK, CDLGSetSystem::GetNewContent, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDS_IMAGE_HEIGHT, DISPID_CLICK, CDLGSetSystem::GetNewContent, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDS_OK_GRADE, DISPID_CLICK, CDLGSetSystem::GetNewContent, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDS_OK_GRADE2, DISPID_CLICK, CDLGSetSystem::GetNewContent, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDS_CONTACT_GRADE, DISPID_CLICK, CDLGSetSystem::GetNewContent, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDS_CONTACT_CODE, DISPID_CLICK, CDLGSetSystem::GetNewContent, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDS_OK_PROCESSID, DISPID_CLICK, CDLGSetSystem::GetNewContent, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDS_NG_PROCESSID, DISPID_CLICK, CDLGSetSystem::GetNewContent, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDB_BTN_RELOAD, DISPID_CLICK, CDLGSetSystem::ClickBtnReload, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDB_BTN_OK, DISPID_CLICK, CDLGSetSystem::ClickBtnOk, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDB_BTN_SAVE, DISPID_CLICK, CDLGSetSystem::ClickBtnSave, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDS_ALIGN_COUNT, DISPID_CLICK, CDLGSetSystem::ClickAlignCount, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDS_SET_AMT, DISPID_CLICK, CDLGSetSystem::ClickMachinType, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDS_SET_AFT, DISPID_CLICK, CDLGSetSystem::ClickMachinType, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDS_SET_GAMMA, DISPID_CLICK, CDLGSetSystem::ClickMachinType, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDS_SET_PGCODE_USE, DISPID_CLICK, CDLGSetSystem::ClickPGCodeUsable, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDS_SET_PGCODE_UNUSE, DISPID_CLICK, CDLGSetSystem::ClickPGCodeUsable, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDS_SET_CG16, DISPID_CLICK, CDLGSetSystem::ClickSetCg16, VTS_NONE)
	ON_EVENT(CDLGSetSystem, IDS_SET_LUMITOP, DISPID_CLICK, CDLGSetSystem::ClickSetLumitop, VTS_NONE)
END_EVENTSINK_MAP()

BOOL CDLGSetSystem::OnInitDialog(){
	CDialog::OnInitDialog();
	CBtnEnh *pBtnEnh;

	IniSetDataText();

	if (theApp.m_iMachineType == SetAMT)
		pBtnEnh = (CBtnEnh*)GetDlgItem(IDS_SET_AMT);
	else if (theApp.m_iMachineType == SetAFT)
		pBtnEnh = (CBtnEnh*)GetDlgItem(IDS_SET_AFT);
	else
		pBtnEnh = (CBtnEnh*)GetDlgItem(IDS_SET_GAMMA);
	pBtnEnh->SetValue(TRUE);
	//>>210422
	if (theApp.m_bPGCodeUsable == TRUE)
		pBtnEnh = (CBtnEnh*)GetDlgItem(IDS_SET_PGCODE_USE);
	else
		pBtnEnh = (CBtnEnh*)GetDlgItem(IDS_SET_PGCODE_UNUSE);
	//<<

	pBtnEnh->SetValue(TRUE);
	ListBoxSetting();
	LoadeCG16PatData();
	
	int iID = m_ctrlCG16Param.GetDlgCtrlID();
	m_ctrlCG16Param.SetControlID(iID);
	m_ctrlCG16Param.m_bEnableDblClick = TRUE;
	m_ctrlCG16Param.DeleteAllItems();
	
	if (theApp.m_bCG_16_USE)
	{
		pBtnEnh = (CBtnEnh*)GetDlgItem(IDS_SET_CG16);
		GetDlgItem(IDC_LIST_CG16_PARAM)->EnableWindow(TRUE);
	}
	else
	{
		pBtnEnh = (CBtnEnh*)GetDlgItem(IDS_SET_LUMITOP);
		GetDlgItem(IDC_LIST_CG16_PARAM)->EnableWindow(FALSE);
	}
		
	pBtnEnh->SetValue(TRUE);

	for (int i = 0; i < CG16PAT_MAXNUM; i++)
	{
		m_ctrlCG16Param.InsertItem(i, CStringSupport::FormatString(_T("%d"), i));
		m_ctrlCG16Param.SetItemText(i, 1, CStringSupport::FormatString(_T("%d"), theApp.m_CG16Pat[i].bUse));
		m_ctrlCG16Param.SetItemText(i, 2, CStringSupport::FormatString(_T("%s"), theApp.m_CG16Pat[i].strPatternName));
		m_ctrlCG16Param.SetItemText(i, 3, CStringSupport::FormatString(_T("%d"), theApp.m_CG16Pat[i].iPGPatNum));
		m_ctrlCG16Param.SetItemText(i, 4, CStringSupport::FormatString(_T("%d"), theApp.m_CG16Pat[i].iCG16_WaitTime));
		m_ctrlCG16Param.SetItemText(i, 5, CStringSupport::FormatString(_T("")));
	}

	UpdateData(FALSE);
	return TRUE;
}
BOOL CDLGSetSystem::LoadeCG16PatData()
{
	CString strPath = _T("D:\\ANI\\DataServer\\Model\\");
	strPath = strPath + _T("CG16PatData") + theApp.m_CurrentModel.m_AlignPcCurrentModelName + _T(".txt");
	EZIni ini(strPath);
	CString strValue, strTemp;
	theApp.m_bCG_16_USE = ini[_T("CG16PatData_Systemp")][_T("CG16_USE")];

	for (int i = 0; i < CG16PAT_MAXNUM; i++)
	{
		if (ini["CG16PatData"][strValue].Exists())
		{
			CStringArray responseTokens;
			strTemp.Format(_T("%d"), i);
			strValue = ini[_T("CG16PatData")][strTemp];
			CStringSupport::GetTokenArray(strValue, _T(','), responseTokens);

			if (responseTokens.GetSize() == 3)
			{
				theApp.m_CG16Pat[i].iNum = i;
				theApp.m_CG16Pat[i].bUse = _ttoi(responseTokens[0]);
				theApp.m_CG16Pat[i].strPatternName = responseTokens[1];
				theApp.m_CG16Pat[i].iPGPatNum = _ttoi(responseTokens[2]);
				theApp.m_CG16Pat[i].iCG16_WaitTime = _ttoi(responseTokens[3]);
			}
		}		
	}
	return TRUE;
}
BOOL CDLGSetSystem::SaveCG16PatData()
{
	CString strPath = _T("D:\\ANI\\DataServer\\Model\\");
	strPath = strPath + _T("CG16PatData") + theApp.m_CurrentModel.m_AlignPcCurrentModelName + _T(".txt");
	EZIni ini(strPath);
	CString strValue, strTemp;

	for (int i = 0; i < CG16PAT_MAXNUM; i++)
	{
		strValue.Format(_T("%d"), theApp.m_CG16Pat[i].iNum);		
		if (ini["CG16PatData"][strValue].Exists())
			ini["CG16PatData"][strValue].Delete();
		else
		{
			strValue.Format(_T("%d,%s,%d,%d,"), theApp.m_CG16Pat[i].bUse, theApp.m_CG16Pat[i].strPatternName, theApp.m_CG16Pat[i].iPGPatNum, theApp.m_CG16Pat[i].iCG16_WaitTime);
			ini["CG16PatData"][strValue] = strValue;
		}
	}
	return TRUE;
}
BOOL CDLGSetSystem::ListBoxSetting()
{
	DWORD dwStyle;
	CString strText(_T(""));
	LV_COLUMN lvColumn;

	CRect Rc;
	GetDlgItem(IDC_LIST_CG16_PARAM)->GetWindowRect(&Rc);
	ScreenToClient(&Rc);

	wchar_t *sListName[6] = { _T("Num"), _T("USE"), _T("Pattern Name"), _T("PG Pattern Num"),_T("PG Pat WaitTime"), _T("") };
	int iWidth[6] = { 100, 100, 100, 100, 0 };

	dwStyle = m_ctrlCG16Param.GetExtendedStyle();
	dwStyle |= LVS_EX_GRIDLINES | LVS_EX_FULLROWSELECT;

	for (int i = 0; i < 6; i++)
	{
		lvColumn.mask = LVCF_FMT | LVCF_SUBITEM | LVCF_TEXT | LVCF_WIDTH;
		lvColumn.fmt = LVCFMT_LEFT;
		lvColumn.pszText = sListName[i];
		lvColumn.iSubItem = i;
		lvColumn.cx = iWidth[i];
		m_ctrlCG16Param.InsertColumn(i, &lvColumn);
	}

	m_ctrlCG16Param.SetExtendedStyle(dwStyle);

	int iID = m_ctrlCG16Param.GetDlgCtrlID();
	m_ctrlCG16Param.SetControlID(iID);
	m_ctrlCG16Param.m_bEnableDblClick = TRUE;
	m_ctrlCG16Param.ShowWindow(SW_SHOW);

	return TRUE;
}
void CDLGSetSystem::IniSetDataText()
{
	GetDlgItem(IDS_MECHINE_TYPE)->SetWindowTextW(theApp.m_strMachineType);
	GetDlgItem(IDS_EQP_ID)->SetWindowTextW(theApp.m_strEqpId);
	GetDlgItem(IDS_EQP_LINE_NUM)->SetWindowTextW(theApp.m_strEqpNum);
	GetDlgItem(IDS_FILESERVER_ID)->SetWindowTextW(theApp.m_strFileServerID);
	GetDlgItem(IDS_COMPANY_LINE)->SetWindowTextW(theApp.m_strCompanyLine);
	GetDlgItem(IDS_IMAGE_WIDTH)->SetWindowTextW(theApp.m_strOpvImageWidth);
	GetDlgItem(IDS_IMAGE_HEIGHT)->SetWindowTextW(theApp.m_strOpvImageHeight);
	GetDlgItem(IDS_OK_GRADE)->SetWindowTextW(theApp.m_strOkGrade);
	//>> Index ok grad 220112
	GetDlgItem(IDS_OK_GRADE2)->SetWindowTextW(theApp.m_strIndexOkGrade);
	GetDlgItem(IDS_CONTACT_GRADE)->SetWindowTextW(theApp.m_strContactNgGrade);
	GetDlgItem(IDS_CONTACT_CODE)->SetWindowTextW(theApp.m_strContactNgCode);
	GetDlgItem(IDS_ALIGN_COUNT)->SetWindowTextW(theApp.m_strAlignCount);
	GetDlgItem(IDS_OK_PROCESSID)->SetWindowTextW(theApp.m_strOKProcessID);
	GetDlgItem(IDS_NG_PROCESSID)->SetWindowTextW(theApp.m_strNGProcessID);
}


void CDLGSetSystem::SelectChangePart()
{
	CBtnEnh *pBtnEnh = (CBtnEnh*)GetFocus();

	switch (pBtnEnh->GetDlgCtrlID())
	{
	case IDS_MECHINE_TYPE: theApp.m_iSysPartSelect = MechineType; break;
	case IDS_EQP_ID: theApp.m_iSysPartSelect = DfsId; break;
	case IDS_EQP_LINE_NUM: theApp.m_iSysPartSelect = EqpNum; break;
	case IDS_COMPANY_LINE: theApp.m_iSysPartSelect = CompanyLine; break;
	case IDS_FILESERVER_ID: theApp.m_iSysPartSelect = FileServerID; break;
	case IDS_IMAGE_WIDTH: theApp.m_iSysPartSelect = OpvImageWidth; break;
	case IDS_IMAGE_HEIGHT: theApp.m_iSysPartSelect = OpvImageHeight; break;
	case IDS_OK_GRADE: theApp.m_iSysPartSelect = OkGrade; break;
	case IDS_OK_GRADE2: theApp.m_iSysPartSelect = IndexOkGrade; break;
	case IDS_CONTACT_GRADE: theApp.m_iSysPartSelect = ContactNgGrade; break;
	case IDS_CONTACT_CODE: theApp.m_iSysPartSelect = ContactNgCode; break;
	case IDS_OK_PROCESSID: theApp.m_iSysPartSelect = OK_ProcessID; break;
	case IDS_NG_PROCESSID: theApp.m_iSysPartSelect = NG_ProcessID; break;

	}
}

CString CDLGSetSystem::GetStringFromDialog()
{
	SelectChangePart();

	CString strSysContent;
	switch (theApp.m_iSysPartSelect){
	case MechineType: GetDlgItem(IDS_MECHINE_TYPE)->GetWindowTextW(strSysContent); break;
	case DfsId: GetDlgItem(IDS_EQP_ID)->GetWindowTextW(strSysContent); break;
	case EqpNum: GetDlgItem(IDS_EQP_LINE_NUM)->GetWindowTextW(strSysContent); break;
	case CompanyLine: GetDlgItem(IDS_COMPANY_LINE)->GetWindowTextW(strSysContent); break;
	case FileServerID: GetDlgItem(IDS_FILESERVER_ID)->GetWindowTextW(strSysContent); break;
	case OpvImageWidth: GetDlgItem(IDS_IMAGE_WIDTH)->GetWindowTextW(strSysContent); break;
	case OpvImageHeight: GetDlgItem(IDS_IMAGE_HEIGHT)->GetWindowTextW(strSysContent); break;
	case OkGrade: GetDlgItem(IDS_OK_GRADE)->GetWindowTextW(strSysContent); break;
	case IndexOkGrade: GetDlgItem(IDS_OK_GRADE2)->GetWindowTextW(strSysContent); break;
	case ContactNgGrade: GetDlgItem(IDS_CONTACT_GRADE)->GetWindowTextW(strSysContent); break;
	case ContactNgCode: GetDlgItem(IDS_CONTACT_CODE)->GetWindowTextW(strSysContent); break;
	case OK_ProcessID: GetDlgItem(IDS_OK_PROCESSID)->GetWindowTextW(strSysContent); break;
	case NG_ProcessID: GetDlgItem(IDS_NG_PROCESSID)->GetWindowTextW(strSysContent); break;
	}
	return strSysContent;
}

void CDLGSetSystem::GetNewContent()
{
	CString strSysContent = GetStringFromDialog();
	CDLGChangeSySemData ChangeContent(theApp.m_iSysPartSelect, strSysContent, SetSystemParm);
	ChangeContent.DoModal();
	CString strNewContent = ChangeContent.GetNewContent();

	switch (theApp.m_iSysPartSelect){
	case MechineType: GetDlgItem(IDS_MECHINE_TYPE)->SetWindowTextW(strNewContent); break;
	case DfsId: GetDlgItem(IDS_EQP_ID)->SetWindowTextW(strNewContent); break;
	case EqpNum: GetDlgItem(IDS_EQP_LINE_NUM)->SetWindowTextW(strNewContent); break;
	case CompanyLine: GetDlgItem(IDS_COMPANY_LINE)->SetWindowTextW(strNewContent); break;
	case FileServerID: GetDlgItem(IDS_FILESERVER_ID)->SetWindowTextW(strNewContent); break;
	case OpvImageWidth: GetDlgItem(IDS_IMAGE_WIDTH)->SetWindowTextW(strNewContent); break;
	case OpvImageHeight: GetDlgItem(IDS_IMAGE_HEIGHT)->SetWindowTextW(strNewContent); break;
	case OkGrade: GetDlgItem(IDS_OK_GRADE)->SetWindowTextW(strNewContent); break;
	case IndexOkGrade: GetDlgItem(IDS_OK_GRADE2)->SetWindowTextW(strNewContent); break;
	case ContactNgGrade: GetDlgItem(IDS_CONTACT_GRADE)->SetWindowTextW(strNewContent); break;
	case ContactNgCode: GetDlgItem(IDS_CONTACT_CODE)->SetWindowTextW(strNewContent); break;
	case OK_ProcessID: GetDlgItem(IDS_OK_PROCESSID)->SetWindowTextW(strNewContent); break;
	case NG_ProcessID: GetDlgItem(IDS_NG_PROCESSID)->SetWindowTextW(strNewContent); break;
	}
}

void CDLGSetSystem::ClickBtnReload()
{
	// TODO: 여기에 메시지 처리기 코드를 추가합니다.
	IniSetDataText();
	LoadeCG16PatData();
}

void CDLGSetSystem::ClickBtnOk()
{
	// TODO: 여기에 메시지 처리기 코드를 추가합니다.
	CDialog::OnOK();

}

void CDLGSetSystem::ClickBtnSave()
{
	// TODO: 여기에 메시지 처리기 코드를 추가합니다.
	EZIni ini(DATA_SYSTEM_DATA_PATH);
	CString strSysContent;

	theApp.m_pMsgBox->WaitShowHide(SW_SHOWNORMAL, _T("Waiting"));

	GetDlgItem(IDS_MECHINE_TYPE)->GetWindowTextW(strSysContent);
	theApp.m_strMachineType = ini[_T("DATA")][_T("MACHINE_TYPE")] = strSysContent;

	GetDlgItem(IDS_EQP_ID)->GetWindowTextW(strSysContent);
	theApp.m_strEqpId = ini[_T("EQP")][_T("ID")] = strSysContent;

	GetDlgItem(IDS_EQP_LINE_NUM)->GetWindowTextW(strSysContent);
	theApp.m_strEqpNum = ini[_T("EQP")][_T("EQP_NUM")] = strSysContent;

	GetDlgItem(IDS_FILESERVER_ID)->GetWindowTextW(strSysContent);
	theApp.m_strFileServerID = ini[_T("EQP")][_T("FILESERVER_ID")] = strSysContent;

	GetDlgItem(IDS_COMPANY_LINE)->GetWindowTextW(strSysContent);
	theApp.m_strCompanyLine = ini[_T("DATA")][_T("COMPANY_LINE")] = strSysContent;

	GetDlgItem(IDS_IMAGE_WIDTH)->GetWindowTextW(strSysContent);
	theApp.m_strOpvImageWidth = ini[_T("DATA")][_T("OPV_IMAGE_WIDTH")] = strSysContent;

	GetDlgItem(IDS_IMAGE_HEIGHT)->GetWindowTextW(strSysContent);
	theApp.m_strOpvImageHeight = ini[_T("DATA")][_T("OPV_IMAGE_HEIGHT")] = strSysContent;

	GetDlgItem(IDS_OK_GRADE)->GetWindowTextW(strSysContent);
	theApp.m_strOkGrade = ini[_T("DATA")][_T("OK_GRADE")] = strSysContent;

	GetDlgItem(IDS_OK_GRADE2)->GetWindowTextW(strSysContent);
	theApp.m_strOkGrade = ini[_T("DATA")][_T("INDEX_OK_GRADE")] = strSysContent;

	GetDlgItem(IDS_CONTACT_GRADE)->GetWindowTextW(strSysContent);
	theApp.m_strContactNgGrade = ini[_T("DATA")][_T("CONTACT_NG_GRADE")] = strSysContent;

	GetDlgItem(IDS_CONTACT_CODE)->GetWindowTextW(strSysContent);
	theApp.m_strContactNgCode = ini[_T("DATA")][_T("CONTACT_NG_CODE")] = strSysContent;

	GetDlgItem(IDS_OK_PROCESSID)->GetWindowTextW(strSysContent);
	theApp.m_strOKProcessID = ini[_T("DATA")][_T("OK_PROCESSID")] = strSysContent;

	GetDlgItem(IDS_NG_PROCESSID)->GetWindowTextW(strSysContent);
	theApp.m_strNGProcessID = ini[_T("DATA")][_T("NG_PROCESSID")] = strSysContent;

	int OldAlignCnt = _ttoi(theApp.m_strAlignCount);
	GetDlgItem(IDS_ALIGN_COUNT)->GetWindowTextW(strSysContent);
	theApp.m_strAlignCount = ini[_T("DATA")][_T("ALIGN_COUNT")] = strSysContent;

	ini[_T("DATA")][_T("MACHINE_NAME")] = theApp.m_iMachineType;

	
	ini[_T("PG")][_T("PGCODE_USABLE")] = theApp.m_bPGCodeUsable; //210422

	for (int ii = 0; ii < _ttoi(theApp.m_strAlignCount); ii++){
		ini[_T("DATA")][CStringSupport::FormatString(_T("ALIGN_TYPE_%d"), ii + 1)] = theApp.m_iAlignInspectType[ii];
	}

	theApp.ThreadCreateDelete(TRUE, OldAlignCnt);

	theApp.m_pMsgBox->WaitShowHide(SW_HIDE);

	theApp.getMsgBox(MS_OK, _T("SAVE FINISHED"), _T("SAVE FINISHED"), _T("SAVE FINISHED"));
	SaveCG16PatData();
}

void CDLGSetSystem::ClickAlignCount()
{
	CDlgAlignCount dlg;
	
	if (dlg.DoModal() == DLG_OK)
		GetDlgItem(IDS_ALIGN_COUNT)->SetWindowTextW(CStringSupport::FormatString(_T("%d"), dlg.m_iAlignCount));

}

void CDLGSetSystem::ClickMachinType()
{
	CBtnEnh *pBtnEnh = (CBtnEnh*)GetFocus();

	switch (pBtnEnh->GetDlgCtrlID())
	{
	case IDS_SET_AMT: theApp.m_iMachineType = SetAMT; break;
	case IDS_SET_AFT: theApp.m_iMachineType = SetAFT; break;
	case IDS_SET_GAMMA: theApp.m_iMachineType = SetGAMMA; break;
	}
}

void CDLGSetSystem::ClickPGCodeUsable()
{
	// TODO: 여기에 메시지 처리기 코드를 추가합니다.
	CBtnEnh *pBtnEnh = (CBtnEnh*)GetFocus();

	switch (pBtnEnh->GetDlgCtrlID())
	{
	case IDS_SET_PGCODE_USE: theApp.m_bPGCodeUsable = TRUE; break;
	case IDS_SET_PGCODE_UNUSE: theApp.m_bPGCodeUsable = FALSE; break;
	}

}



void CDLGSetSystem::ClickSetCg16()
{
	// TODO: Add your message handler code here
	theApp.m_bCG_16_USE = TRUE;
	GetDlgItem(IDC_LIST_CG16_PARAM)->EnableWindow(TRUE);
}


void CDLGSetSystem::ClickSetLumitop()
{
	theApp.m_bCG_16_USE = FALSE;
	GetDlgItem(IDC_LIST_CG16_PARAM)->EnableWindow(FALSE);
	// TODO: Add your message handler code here
}


BOOL CDLGSetSystem::PreTranslateMessage(MSG* pMsg)
{
	// TODO: 여기에 특수화된 코드를 추가 및/또는 기본 클래스를 호출합니다.
	if (pMsg->message == WM_KEYDOWN){
		if (pMsg->wParam == VK_RETURN || pMsg->wParam == VK_ESCAPE)
		{
			return TRUE;
		}
	}

	return CDialogEx::PreTranslateMessage(pMsg);
}
