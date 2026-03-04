#include "stdafx.h"
#include "MyListCtrl.h"
#include "InPlaceList.h"
#include "Resource.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CMyListCtrl

CMyListCtrl::CMyListCtrl()
{
	m_iControlID = -1;
	m_bEnableDblClick = FALSE;
	ResetComboItem();


	m_bEdited = FALSE;
	m_bLBtnDown = FALSE; //>>200407 hjjang 파라미터 창 Enter 버그 수정
	
}

CMyListCtrl::~CMyListCtrl()
{
	ResetComboItem();
}


BEGIN_MESSAGE_MAP(CMyListCtrl, CListCtrl)
	//{{AFX_MSG_MAP(CMyListCtrl)
	ON_WM_LBUTTONDBLCLK()
	ON_WM_LBUTTONDOWN()
	//}}AFX_MSG_MAP
	ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, &CMyListCtrl::OnNMCustomdraw)
	ON_NOTIFY_REFLECT(LVN_ITEMCHANGED, &CMyListCtrl::OnLvnItemchanged)	
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMyListCtrl message handlers
void CMyListCtrl::OnLButtonDown(UINT nFlags, CPoint point) 
{
	m_ctrEidt.DestroyWindow();

#if 0
	if (!m_bEnableDblClick)
	{
		POSITION pos = GetFirstSelectedItemPosition();    // 아이템 위치를 알아냄.
		if (pos != NULL)                // 정상 영역 선택시
		{
			LVHITTESTINFO lvhti;
			lvhti.pt = point;
			SubItemHitTest(&lvhti);
			if (lvhti.flags & LVHT_ONITEMLABEL)
			{
				m_nItem = lvhti.iItem;        // 아이템 위치를 찾아냄.				
			}
		}
	}
#endif
	m_bLBtnDown = FALSE; //>>200407 hjjang 파라미터 창 Enter 버그 수정
	CListCtrl::OnLButtonDown(nFlags, point);
}

void CMyListCtrl::OnLButtonDblClk(UINT nFlags, CPoint point) 
{
	if (!m_bEnableDblClick)
		return;

	m_ctrEidt.DestroyWindow();
    POSITION pos = GetFirstSelectedItemPosition();    // 아이템 위치를 알아냄.
    if( pos != NULL)                // 정상 영역 선택시
    {
        LVHITTESTINFO lvhti;
        lvhti.pt = point;
        SubItemHitTest(&lvhti);
        if (lvhti.flags & LVHT_ONITEMLABEL)
        {
            m_nItem = lvhti.iItem;        // 아이템 위치를 찾아냄.
            m_nSubItem = lvhti.iSubItem;     // 컬렘 위치를 찾아냄.     
			if (!IsComboItem(m_nItem))
				CreateEditBox();        // 정상 영역일때 에디트 박스 동작
			else
				ShowInPlaceList(m_nItem, m_nSubItem, m_strlistCombo, 0);
        }
		m_bLBtnDown = TRUE; //>>200407 hjjang 파라미터 창 Enter 버그 수정
    }
	
	CListCtrl::OnLButtonDblClk(nFlags, point);
}


void CMyListCtrl::CreateEditBox()
{
	
	CRect rect;
	CString strGetText=_T("");	
	GetSubItemRect(m_nItem, m_nSubItem, LVIR_LABEL, rect);   // 클릭한 곳에 테두리 값을 rect로 얻어옴	
	InvalidateRect(&rect);
	
	m_ctrEidt.Create(ES_LEFT | ES_AUTOHSCROLL | WS_BORDER | WS_CHILD , rect, this, 0); // 얻어온 rect 값으로 에디트 박스 생성
	
	strGetText = GetItemText(m_nItem, m_nSubItem);       //아이템 얻어오기
	m_ctrEidt.MoveWindow(rect);
	m_ctrEidt.SetWindowText(strGetText);	
	
	m_ctrEidt.Invalidate();	
	m_ctrEidt.ShowWindow(TRUE);	
	m_ctrEidt.SetSel(0,-1);	
	m_ctrEidt.SetFocus(); // 포커스 주기
}


void CMyListCtrl::EndModify()
{
	CString sGetText;
	m_ctrEidt.GetWindowText(sGetText); //수정된 아이템 얻어오기
	if (sGetText.GetLength() <= 0)
		return;
	SetItemText(m_nItem, m_nSubItem, sGetText);
	m_ctrEidt.DestroyWindow();
	Invalidate(TRUE);
	UpdateData(FALSE);
	m_bEdited = TRUE;
	m_bLBtnDown = FALSE; //>>200407 hjjang 파라미터 창 Enter 버그 수정
	return;
}

BOOL CMyListCtrl::PreTranslateMessage(MSG* pMsg) 
{
	if(pMsg->message == WM_KEYDOWN &&  pMsg->wParam ==VK_RETURN)
    {
		POSITION pos = GetFirstSelectedItemPosition();    // 아이템 위치를 알아냄.
		if(pos == NULL) {
			return TRUE;
		}
		if (m_bLBtnDown == TRUE)
			EndModify();
		UpdateData(FALSE);	
		return TRUE;
	}	

	//if(pMsg->wParam == VK_DELETE) {
	//	DeleteData();
	//}
	return CListCtrl::PreTranslateMessage(pMsg);
}

void CMyListCtrl::SetControlID(int nID)
{
	m_iControlID = nID;
}

void CMyListCtrl::DeleteData()
{
	int nCount = GetSelectedCount();
	int nItem = -1;
	for(int i = 0 ; i < nCount ; i++) {
		nItem = GetNextItem(nItem, LVNI_SELECTED);
		if (nItem == -1)
			break;
		DeleteItem(nItem);
		nItem = -1;
	}
	ResetIndex();
}

void CMyListCtrl::ResetIndex()
{
	int iTotal = GetItemCount();
	CString sIndex;
	for(int i = 0; i < iTotal; i++) {
		sIndex.Format(L"%d", i + 1);
		SetItemText(i, 0, sIndex);
	}
}


void CMyListCtrl::OnNMCustomdraw(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMLVCUSTOMDRAW pNMLVCUSTOMDRAW = (LPNMLVCUSTOMDRAW)pNMHDR;
	COLORREF cr = (COLORREF)pNMLVCUSTOMDRAW->nmcd.lItemlParam;
	switch (pNMLVCUSTOMDRAW->nmcd.dwDrawStage)
	{
	case CDDS_PREPAINT:
		*pResult = CDRF_NOTIFYITEMDRAW;
		break;
	case CDDS_ITEMPREPAINT:
		if ( pNMLVCUSTOMDRAW->nmcd.dwItemSpec == 0 )
			pNMLVCUSTOMDRAW->clrTextBk = RGB(255, 255, 255);
		else
			pNMLVCUSTOMDRAW->clrTextBk = cr;
		//pNMLVCUSTOMDRAW->clrText = cr;
		break;
	}
}


int CMyListCtrl::InsertItem(int pos, LPCTSTR text, COLORREF colorBk)
{
	LVITEMW item = { 0 };
	item.mask = LVIF_TEXT | LVIF_PARAM;
	item.iItem = pos;
	item.pszText = (LPWSTR)text;
	item.lParam = (LPARAM)colorBk;
	return CListCtrl::InsertItem(&item);

}


// ShowInPlaceList        – Drop Down List 를 생성키키는 함수명 
// Returns            – ComboBox Ctrl에 대한 포인터
// nItem            – 위치한 Cell 의 줄 인덱스
// nCol                – 위치한 Cell 의 컬럼 인덱스 
// lstItems            – A list of strings to populate the control with
// nSel                – drop down list 에서 초기에 설정될 인덱스 

CComboBox* CMyListCtrl::ShowInPlaceList(int nItem, int nCol, CStringList &lstItems, int nSel)
{
	// The returned pointer should not be saved

	// 선택된 아이템(Cell) 이 Visible 인가를 확인 
	if (!EnsureVisible(nItem, TRUE))
		return NULL;

	// 컬럼이 유효한가를 확인  
	CHeaderCtrl* pHeader = (CHeaderCtrl*)GetDlgItem(0);
	int nColumnCount = pHeader->GetItemCount();
	if (nCol >= nColumnCount || GetColumnWidth(nCol) < 10)
		return NULL;

	// 모든 컬럼의 길이를 얻어냄 
	int offset = 0;
	for (int i = 0; i < nCol; i++)
		offset += GetColumnWidth(i);

	CRect rect;
	GetItemRect(nItem, &rect, LVIR_BOUNDS);

	CRect rcClient;
	GetClientRect(&rcClient);
	if (offset + rect.left < 0 || offset + rect.left > rcClient.right)
	{
		CSize size;
		size.cx = offset + rect.left;
		size.cy = 0;
		Scroll(size);
		rect.left -= size.cx;
	}

	rect.left += offset + 4;
	rect.right = rect.left + GetColumnWidth(nCol) - 3;
	int height = rect.bottom - rect.top;
	rect.bottom += 5 * height;
	if (rect.right > rcClient.right) rect.right = rcClient.right;

	DWORD dwStyle = WS_BORDER | WS_CHILD | WS_VISIBLE | WS_VSCROLL
		| CBS_DROPDOWNLIST | CBS_DISABLENOSCROLL;
	CComboBox *pList = new CInPlaceList(nItem, nCol, &lstItems, nSel);
	pList->Create(dwStyle, rect, this, 0);
	pList->SetItemHeight(-1, height);
	pList->SetHorizontalExtent(GetColumnWidth(nCol));


	CRect rctDropDown;

	pList->GetDroppedControlRect(&rctDropDown);
	pList->GetParent()->ScreenToClient(&rctDropDown);
	rctDropDown.bottom = rctDropDown.top + rect.Height() + height * lstItems.GetCount();
	pList->MoveWindow(&rctDropDown);


	return pList;
}

void CMyListCtrl::ResetComboItem()
{
	m_arrayComboItem.RemoveAll();
}

BOOL CMyListCtrl::IsComboItem(int nItem)
{
	if (m_arrayComboItem.GetCount() == 0)
		return FALSE;

	int iCount = m_arrayComboItem.GetCount();
	for (int i = 0; i < iCount; i++)
	{
		if (m_arrayComboItem.GetAt(i) == nItem)
			return TRUE;
	}

	return FALSE;

}

void CMyListCtrl::SetComboItem(int nItem)
{
	m_arrayComboItem.Add(nItem);
}

void CMyListCtrl::OnLvnItemchanged(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	// TODO: Add your control notification handler code here
	*pResult = 0;
}


