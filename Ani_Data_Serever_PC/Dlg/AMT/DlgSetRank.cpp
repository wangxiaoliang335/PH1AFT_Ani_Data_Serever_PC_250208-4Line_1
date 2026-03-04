// SetRank.cpp : 구현 파일입니다.
//

#include "stdafx.h"
#include "Ani_Data_Serever_PC.h"
#include "DlgSetRank.h"
#include "afxdialogex.h"
#include "GetNumDlg.h"


// CDlgSetRank 대화 상자입니다.

IMPLEMENT_DYNAMIC(CDlgSetRank, CDialogEx)

CDlgSetRank::CDlgSetRank(CWnd* pParent /*=NULL*/)
	: CDialogEx(CDlgSetRank::IDD, pParent)
{
	m_iFocusRowPosition = -1;
	m_RankCtrlList.m_bEnableDblClick = TRUE;

	CRect rect;
	m_RankCtrlList.m_cmbGrade.Create(CBS_DROPDOWN | WS_CHILD | WS_VSCROLL | WS_TABSTOP | WS_VISIBLE, rect, this, 0);


}

CDlgSetRank::~CDlgSetRank()
{
}

void CDlgSetRank::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);

	DDX_Control(pDX, IDC_AOI_RANK_LIST, m_RankCtrlList);
	DDX_Control(pDX, IDS_PLC_SEND_NUMBER, m_btnPlcSendNumber);
}


BEGIN_MESSAGE_MAP(CDlgSetRank, CDialogEx)
	ON_MESSAGE(WM_USER_LISTCTRLEX, ClickEvent)
END_MESSAGE_MAP()



// CDlgSetRank 메시지 처리기입니다.

BEGIN_EVENTSINK_MAP(CDlgSetRank, CDialogEx)


ON_EVENT(CDlgSetRank, IDC_RESETRANK, DISPID_CLICK, CDlgSetRank::ReLoadRankData, VTS_NONE)
ON_EVENT(CDlgSetRank, IDC_SAVERANK, DISPID_CLICK, CDlgSetRank::SaveChangedData, VTS_NONE)
ON_EVENT(CDlgSetRank, IDC_CHANGERANK, DISPID_CLICK, CDlgSetRank::ClickChange, VTS_NONE)
ON_EVENT(CDlgSetRank, IDC_QUITRANK, DISPID_CLICK, CDlgSetRank::OkayQuitFunction, VTS_NONE)
ON_EVENT(CDlgSetRank, IDB_BTN_AOI, DISPID_CLICK, CDlgSetRank::SelectPartFuncion, VTS_NONE)
ON_EVENT(CDlgSetRank, IDB_BTN_OPV, DISPID_CLICK, CDlgSetRank::SelectPartFuncion, VTS_NONE)
ON_EVENT(CDlgSetRank, IDB_BTN_GRADEFLOW, DISPID_CLICK, CDlgSetRank::SelectPartFuncion, VTS_NONE)
ON_EVENT(CDlgSetRank, IDC_RACK_ADD, DISPID_CLICK, CDlgSetRank::ClickRackAdd, VTS_NONE)
ON_EVENT(CDlgSetRank, IDC_RANK_DELETE, DISPID_CLICK, CDlgSetRank::ClickRankDelete, VTS_NONE)
ON_EVENT(CDlgSetRank, IDS_PLC_SEND_NUMBER, DISPID_CLICK, CDlgSetRank::ClickSetNumber, VTS_NONE)
END_EVENTSINK_MAP()


BOOL CDlgSetRank::OnInitDialog()
{
	CDialog::OnInitDialog();

	//theApp.LoadRank();// 210226 yjlim  Test용도

	CRect Rc;
	GetDlgItem(IDC_AOI_RANK_LIST)->GetWindowRect(&Rc);



	m_RankCtrlList.SetExtendedStyle(m_RankCtrlList.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_SUBITEMIMAGES | LVS_EX_GRIDLINES);
	m_RankCtrlList.InsertColumn(eRnkNone, L"", LVCFMT_CENTER, 0, 0);
	/*m_RankCtrlList.InsertColumn(1, _T("No"), LVCFMT_CENTER, 60);
	m_RankCtrlList.InsertColumn(2, _T("DEFECT\nCODE"), LVCFMT_CENTER, 100);
	m_RankCtrlList.InsertColumn(3, _T("DESCRIBE"), LVCFMT_CENTER, 400);*/
	m_RankCtrlList.InsertColumn(eRnkPriority, _T("PRIORITY"), LVCFMT_CENTER, 100);
	m_RankCtrlList.InsertColumn(eRnkDefectCode, _T("DEFECT CODE"), LVCFMT_CENTER, 150);
	m_RankCtrlList.InsertColumn(eRnkDefectGrade, _T("DEFECT GRADE"), LVCFMT_CENTER, 150);
	m_RankCtrlList.InsertColumn(eRnkDescribe, _T("DESCRIBE"), LVCFMT_CENTER, 425);

	m_RankCtrlList.SetFontSize(17, 2.0, 17, 0);
	m_RankCtrlList.CreateImageList(77, 35);

	CBitmap bmpEmpty, bmpClear;
	m_RankCtrlList.GetCurImageList()->Add(&bmpEmpty, RGB(0, 0, 0));
	m_RankCtrlList.SetMainHandle(this);
	m_RankCtrlList.ShowSelectionBar(TRUE);
	m_RankCtrlList.RedrawWindow();

	CBtnEnh *pBtnEnh = (CBtnEnh*)GetDlgItem(IDB_BTN_AOI);
	pBtnEnh->SetValue(TRUE);
	m_nSelectParts = INDEX;
	m_nPreSelectParts = INDEX;
	m_btnPlcSendNumber.SetWindowTextW(CStringSupport::FormatString(_T("%d"), theApp.m_iNumberSendToPlc));
	SelectPartFuncion();
	UpdateData(FALSE);

	return TRUE;  // return TRUE unless you set the focus to a control
	// 예외: OCX 속성 페이지는 FALSE를 반환해야 합니다.
}

void CDlgSetRank::ReLoadRank()
{	
	m_RankCtrlList.DeleteAllItems();
	
	if (m_nSelectParts == GRADEFLOW)
	{
		vector<GradeFlow>* vecTemp = &theApp.m_VecGradeFlow;

		//>> List 형태 재설정
		ChangeContents();
		//<<

		for (int ii = 0; ii < vecTemp->size(); ii++)
		{
			GradeFlow* TempPos = &vecTemp->at(ii);

			m_RankCtrlList.InsertItem(ii, _T(""));
			m_RankCtrlList.SetItemText(ii, 1, TempPos->strGrade);
			m_RankCtrlList.SetItemText(ii, 2, CStringSupport::FormatString(_T("%s"), TempPos->iFlow == 1 ? _T("Flow_AfterMachine") : TempPos->iFlow == 2 ? _T("Flow_Operator") : _T("TextNG")));
		}
	}
	else
	{
		vector<RankStruct>* vecTemp = &theApp.m_VecRank[m_nSelectParts];

		//>> List 형태 재설정
		ChangeContents();
		//<<

		//우선순위, code, grade, description,  물류방향(하류, OPV)
		for (int ii = 0; ii < vecTemp->size(); ii++)
		{
			RankStruct* TempPos = &vecTemp->at(ii);

			m_RankCtrlList.InsertItem(ii, _T(""));
			m_RankCtrlList.SetItemText(ii, 1, CStringSupport::FormatString(_T("%d"), TempPos->iPriority));
			m_RankCtrlList.SetItemText(ii, 2, TempPos->strCode);
			m_RankCtrlList.SetItemText(ii, 3, TempPos->strGrade);
			m_RankCtrlList.SetItemText(ii, 4, TempPos->strDescription);
		}
	}

	

	m_RankCtrlList.UnlockWindowUpdate();
}

void CDlgSetRank::ChangeContents()
{
	//m_RankCtrlList.DeleteAllItems();

	m_RankCtrlList.SetListColumn(eRnkNone, L"", LVCFMT_CENTER, 0, 0);

	if (m_nSelectParts == GRADEFLOW)
	{
		m_RankCtrlList.SetColumnWidth(eRnkGrade, 420);
		m_RankCtrlList.SetColumnWidth(eRnkFlow, 420);
		m_RankCtrlList.SetColumnWidth(eRnkDefectGrade, 0);
		m_RankCtrlList.SetColumnWidth(eRnkDescribe, 0);

		m_RankCtrlList.SetListColumn(eRnkGrade, _T("DEFECT GRADE"), LVCFMT_CENTER, 400);
		m_RankCtrlList.SetListColumn(eRnkFlow, _T("FLOW"), LVCFMT_CENTER, 400);
	}
	else
	{
		m_RankCtrlList.SetColumnWidth(eRnkPriority, 100);
		m_RankCtrlList.SetColumnWidth(eRnkDefectCode, 150);
		m_RankCtrlList.SetColumnWidth(eRnkDefectGrade, 150);
		m_RankCtrlList.SetColumnWidth(eRnkDescribe, 425);

		m_RankCtrlList.SetListColumn(eRnkPriority, _T("PRIORITY"), LVCFMT_CENTER, 100);
		m_RankCtrlList.SetListColumn(eRnkDefectCode, _T("DEFECT CODE"), LVCFMT_CENTER, 150);
		m_RankCtrlList.SetListColumn(eRnkDefectGrade, _T("DEFECT GRADE"), LVCFMT_CENTER, 150);
		m_RankCtrlList.SetListColumn(eRnkDescribe, _T("DESCRIBE"), LVCFMT_CENTER, 425);
	}
	
	/*m_RankCtrlList.SetFontSize(17, 2.0, 17, 0);
	m_RankCtrlList.CreateImageList(77, 35);
	CBitmap bmpEmpty, bmpClear;
	m_RankCtrlList.GetCurImageList()->Add(&bmpEmpty, RGB(0, 0, 0));
	m_RankCtrlList.SetMainHandle(this);
	m_RankCtrlList.ShowSelectionBar(TRUE);*/
	m_RankCtrlList.RedrawWindow();
}

void CDlgSetRank::SaveRank()
{
	CString strPath = _T("D:\\ANI\\Dataserver\\Model\\");
	strPath = strPath + _T("Setrank_") + theApp.m_CurrentModel.m_AlignPcCurrentModelName + _T(".ini");
	EZIni ini(strPath);
	CString strValue, strTemp;
	if (m_nSelectParts == GRADEFLOW)
	{
		vector<GradeFlow>* vecTemp = &theApp.m_VecGradeFlow;
		vecTemp->clear();

		for (int ii = 0; ii < m_RankCtrlList.GetItemCount(); ii++)
		{
			GradeFlow tempFlow;

			tempFlow.strGrade = m_RankCtrlList.GetItemText(ii, 1);
			tempFlow.iFlow = _ttoi(m_RankCtrlList.GetItemText(ii, 2));
			vecTemp->push_back(tempFlow);
		}
		if (ini[RankIniTital[m_nSelectParts]].Exists())
			ini[RankIniTital[m_nSelectParts]].Delete();

		for (int ii = 0; ii < vecTemp->size(); ii++)
		{
			GradeFlow* TempPos = &vecTemp->at(ii);

			strTemp.Format(_T("%d"), ii);
			strValue.Format(_T("%s^%d"), TempPos->strGrade,
				TempPos->iFlow);
			ini[RankIniTital[m_nSelectParts]][strTemp] = strValue;
		}
	}
	else
	{
		vector<RankStruct>* vecTemp = &theApp.m_VecRank[m_nSelectParts];
		vecTemp->clear();
		for (int ii = 0; ii < m_RankCtrlList.GetItemCount(); ii++)
		{
			RankStruct tempStruct;

			tempStruct.iPriority = _ttoi(m_RankCtrlList.GetItemText(ii, 1));
			tempStruct.strCode = m_RankCtrlList.GetItemText(ii, 2);
			tempStruct.strGrade = m_RankCtrlList.GetItemText(ii, 3);
			tempStruct.strDescription = m_RankCtrlList.GetItemText(ii, 4);
			vecTemp->push_back(tempStruct);
		}

		if (ini[RankIniTital[m_nSelectParts]].Exists())
			ini[RankIniTital[m_nSelectParts]].Delete();

		//>>sorting 기능 추가
		sort(vecTemp->begin(), vecTemp->end(), theApp.RankCompare);
		//<< 

		for (int ii = 0; ii < vecTemp->size(); ii++)
		{
			RankStruct* TempPos = &vecTemp->at(ii);

			strTemp.Format(_T("%d"), TempPos->iPriority);
			strValue.Format(_T("%s^%s^%s"), TempPos->strCode,
				TempPos->strGrade, TempPos->strDescription);

			ini[RankIniTital[m_nSelectParts]][strTemp] = strValue;
		}
	}

	theApp.getMsgBox(MS_OK, _T("Save finish"), _T("Save finish"), _T("保存完成"));
}


void CDlgSetRank::ReLoadRankData()
{
	for (int i = 0; i < RankListCount - 1; i++)
		theApp.m_VecRank[i].clear();
	theApp.m_VecGradeFlow.clear();
	theApp.LoadRank();
	ReLoadRank();
}


void CDlgSetRank::SaveChangedData()
{
	SaveRank();
}

void CDlgSetRank::ClickChange()
{
	
	m_iFocusRowPosition = m_RankCtrlList.GetSelectionMark();
	
	if (m_iFocusRowPosition == -1){
		theApp.getMsgBox(MB_OK, _T("Please Select"), _T("Please Select"), _T("Please Select"));
		return;
	}

	CString strSelectCode,strSelectDescribe;

	if (m_iFocusRowPosition != -1){
		strSelectCode = m_RankCtrlList.GetItemText(m_iFocusRowPosition, 2);
		strSelectDescribe = m_RankCtrlList.GetItemText(m_iFocusRowPosition, 3);
	}

	CDlgAddRankCode dlg(ChangeRank, strSelectCode, strSelectDescribe);
	dlg.DoModal();
	CString strChangeRank = dlg.ReturnAddCode();
	if (strChangeRank==_T("")){
		strChangeRank = strSelectCode;
	}
	CString strChangeRankDescribe = dlg.ReturnAddDescribe();
	if (strChangeRankDescribe == _T("")){
		strChangeRankDescribe = strSelectDescribe;
	}

	m_RankCtrlList.SetItemText(m_iFocusRowPosition, 2, strChangeRank);
	m_RankCtrlList.SetItemText(m_iFocusRowPosition, 3, strChangeRankDescribe);
}


void CDlgSetRank::OkayQuitFunction()
{
	m_RankCtrlList.m_ctrEdit.DestroyWindow();
	CDialog::OnOK();
}

void CDlgSetRank::StringChanged()
{
	StringChnageMsg(IDC_RESETRANK, _T("Reload"), _T("Reload"), _T("Reload"));
	StringChnageMsg(IDC_RACK_ADD, _T("Add"), _T("Add"), _T("增加"));
	StringChnageMsg(IDC_RANK_DELETE, _T("Delete"), _T("Delete"), _T("删除"));
	StringChnageMsg(IDC_CHANGERANK, _T("Change"), _T("Change"), _T("更改"));
	StringChnageMsg(IDC_SAVERANK, _T("Save"), _T("Save"), _T("保存"));
	StringChnageMsg(IDC_QUITRANK, _T("OK"), _T("OK"), _T("确定"));
}

void CDlgSetRank::StringChnageMsg(int btn, CString strKor, CString strEng, CString strChi)
{
	CString msg;
	switch (theApp.m_iLanguageSelect)
	{
	case KOR:msg = strKor; break;
	case ENG:msg = strEng; break;
	case CHI:msg = strChi; break;
	}

	((CBtnEnh*)GetDlgItem(btn))->SetCaption(msg);
}

void CDlgSetRank::SelectPartFuncion()
{
	pBtnEnh = (CBtnEnh*)GetFocus();
	
	m_nPreSelectParts = m_nSelectParts;
	switch (pBtnEnh->GetDlgCtrlID())
	{
		case IDB_BTN_INDEX: m_nSelectParts = INDEX; break;
		case IDB_BTN_OPV: m_nSelectParts = OPV; break;
		case IDB_BTN_GRADEFLOW: m_nSelectParts = GRADEFLOW; break;
	}

	if (m_RankCtrlList.m_cmbGrade.m_hWnd != NULL)
		m_RankCtrlList.m_cmbGrade.ShowWindow(SW_HIDE);
	if (m_RankCtrlList.m_ctrEdit.m_hWnd != NULL)
		m_RankCtrlList.m_ctrEdit.ShowWindow(SW_HIDE);



	m_RankCtrlList.m_iSelectedRank = m_nSelectParts;
	pBtnEnh->SetValue(TRUE);
	ReLoadRank();
	UpdateData(FALSE);
}

void CDlgSetRank::ClickRackAdd()
{
	CDlgAddRankCode dlg(AddRank);

	if (dlg.DoModal() == DLG_OK)
	{
		CString strRankNo;
		CString strAddRank = dlg.ReturnAddCode();
		CString strAddDescribe = dlg.ReturnAddDescribe();

		if (strAddRank == _T("") || strAddDescribe == _T("")){
			theApp.getMsgBox(MB_OK, _T("Please Input Rank Code And Describe"), _T("Please Input Rank Code And Describe"), _T("Please Input Rank Code And Describe"));
			m_iFocusRowPosition = -1;
			return;
		}

		if (m_iFocusRowPosition == -1)
		{
			int nRowCount = m_RankCtrlList.GetItemCount();
			strRankNo.Format(_T("%d"), nRowCount + 1);

			m_RankCtrlList.InsertItem(nRowCount, _T(""));
			m_RankCtrlList.SetItemText(nRowCount, 1, strRankNo);
			m_RankCtrlList.SetItemText(nRowCount, 2, strAddRank);
			m_RankCtrlList.SetItemText(nRowCount, 3, strAddDescribe);
		}
		else
		{
			int nRowCount = m_iFocusRowPosition;
			strRankNo.Format(_T("%d"), nRowCount + 1);

			m_RankCtrlList.InsertItem(nRowCount, _T(""));
			m_RankCtrlList.SetItemText(nRowCount, 1, strRankNo);
			m_RankCtrlList.SetItemText(nRowCount, 2, strAddRank);
			m_RankCtrlList.SetItemText(nRowCount, 3, strAddDescribe);

			for (int ii = 0; ii < m_RankCtrlList.GetItemCount(); ii++)
			{
				strRankNo.Format(_T("%d"), ii + 1);
				m_RankCtrlList.SetItemText(ii, 1, strRankNo);
			}
		}

		m_iFocusRowPosition = -1;
	}
}


void CDlgSetRank::ClickRankDelete()
{
	CString strRankNo;
	
	m_iFocusRowPosition = m_RankCtrlList.GetSelectionMark();
	
	if (m_iFocusRowPosition == -1){
		theApp.getMsgBox(MB_OK,_T("Please Select"), _T("Please Select"), _T("Please Select"));
		return;
	}

	m_RankCtrlList.DeleteItem(m_iFocusRowPosition);

	for (int ii = 0; ii <m_RankCtrlList.GetItemCount(); ii++)
	{
		strRankNo.Format(_T("%d"), ii + 1);
		m_RankCtrlList.SetItemText(ii, 1, strRankNo);
	}

	m_RankCtrlList.SetSelectionMark(-1);
}

LRESULT CDlgSetRank::ClickEvent(WPARAM wParam, LPARAM lParam)
{
	CListCtrlEx *pParam = (CListCtrlEx*)wParam;
	m_iFocusRowPosition = pParam->m_nItem;

	return 0;
}
void CDlgSetRank::ClickSetNumber()
{
	CString strPath = _T("D:\\ANI\\Dataserver\\Model\\");
	strPath = strPath + _T("Setrank_") + theApp.m_CurrentModel.m_AlignPcCurrentModelName + _T(".ini");
	EZIni ini(strPath);
	CGetNumDlg Dlg;

	if (Dlg.DoModal() == DLG_OK)
	{
		theApp.m_iNumberSendToPlc = _ttoi(Dlg.GetstrNum());

		if (theApp.m_iNumberSendToPlc == 0)
			return;

		if (theApp.m_iNumberSendToPlc > 16)
		{
			theApp.getMsgBox(MB_OK, _T("Maximum input value is 16"), _T("Maximum input value is 16"), _T("Maximum input value is 16"));
			return;
		}
		
		m_btnPlcSendNumber.SetWindowTextW(CStringSupport::FormatString(_T("%d"), theApp.m_iNumberSendToPlc));

		ini[_T("SYSTEM")][_T("PlcSendNumber")] = theApp.m_iNumberSendToPlc;
	}
}

BOOL CDlgSetRank::PreTranslateMessage(MSG* pMsg)
{
	// TODO: 여기에 특수화된 코드를 추가 및/또는 기본 클래스를 호출합니다.

	if (pMsg->message == WM_KEYDOWN)
	{
		if (pMsg->wParam == VK_ESCAPE)
		{
			m_RankCtrlList.m_ctrEdit.DestroyWindow();
		}
	}
	else if (pMsg->message == WM_LBUTTONDOWN)
	{
		if (m_RankCtrlList.m_nSubItem != 3/*해당 부분 3이아니라 변수형태로 해야됌..*/)
		{
			if (m_RankCtrlList.m_cmbGrade.m_hWnd != NULL)
				m_RankCtrlList.m_cmbGrade.ShowWindow(SW_HIDE);
		}
	}
	else if (pMsg->message == WM_LBUTTONDBLCLK)
	{
		if (m_RankCtrlList.m_nSubItem == 3/*해당 부분 3이아니라 변수형태로 해야됌..*/)
		{
			m_RankCtrlList.CreateComboBox();
			m_RankCtrlList.m_cmbGrade.SetDlgCtrlID(IDC_RANK_GRADE_COMBO);
			//
			////m_RankCtrlList.m_cmbGrade.ResetContent();
			//CString sTest;
			//m_RankCtrlList.m_cmbGrade.GetWindowText(sTest);// 해당 부분에서 받아온 것을 랭크 등급 부분에.. 등록해야됌.
			//m_RankCtrlList.m_cmbGrade.ShowWindow(SW_HIDE);
		}
	}

	return CDialogEx::PreTranslateMessage(pMsg);
}
