#if !defined(AFX_LISTCTRLEX_H__3E6294FD_2953_4C48_A347_6D300ED5B606__INCLUDED_)
#define AFX_LISTCTRLEX_H__3E6294FD_2953_4C48_A347_6D300ED5B606__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// ListCtrlEx.h : header file
//
#define WM_USER_LISTCTRLEX WM_USER+1
#include "HeaderCtrlEx.h"
/////////////////////////////////////////////////////////////////////////////
// CListCtrlEx window

class CListCtrlEx : public CListCtrl
{
	// Construction
public:
	CListCtrlEx();
	// Attributes
public:
	int m_nItem;																					// 急琶等 Row Index 蔼
	int m_nSubItem;																					// 急琶等 Col Index 蔼
	
	
	//>> 210227 yjlim dblClick시 EditBox 추가
	BOOL		m_bEnableDblClick;
	CEdit		m_ctrEdit;
	CComboBox   m_cmbGrade;
	int			m_iSelectedRank;
	void CreateEditBox();
	void CreateComboBox();
	void EndModify();
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 瘤盔涝聪促.
	void OnComboClicked();
	//<<


	void SetFontSize(int iHeaderSize, double iHeaderGab, int iSize, double iGab);					// ListCtrl 农扁 炼沥 
																									// iHeaderSize绰 惑窜 迄飘, iHeaderGab篮 1.0老锭 扁夯 2.0老锭 滴临啊瓷
																									// iSize绰 Item 臂磊 农扁 炼例啊瓷, iGab篮 1.0老锭 扁夯 2.0老锭 滴临啊瓷
	void ShowSelectionBar(BOOL bShow);																// SelectBar TRUE 焊捞扁, FALSE 见扁扁
	void CreateImageList(int iCx, int iCy);															// ImageList 眠啊矫 荤侩 
	void SetHeaderEnable(BOOL bEnable);
	void SetMainHandle(CWnd* wnd);																	// 皋矫瘤 罐阑 勤甸
	void SetSelectListPos(int iPos);																// 伎 急琶
	void SetListColumn(int nCol, CString lpszColumnHeading, int nFormat = LVCFMT_LEFT, int nWidth = -1, int nSubItem = -1);
	void DeleteImageList();
	void SetListItem(int iItem, int iSubitem, CString strText);
	void SetListItemTime(int iItem, int iSubitem, DWORD dwTime);
	//afx_msg void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
	//afx_msg void MeasureItem(LPMEASUREITEMSTRUCT lpMeasureItemStruct);
	// Operations
public:
	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CListCtrlEx)
	//}}AFX_VIRTUAL

	// Implementation
public:
	virtual ~CListCtrlEx();

	// Generated message map functions
//protected:
public:
	COLORREF		m_colRow1, m_colRow2;
	CFont			m_NewHeaderFont, m_NewDataFont;
	CHeaderCtrlEx	m_HeaderCtrlEx;
	CFont *m_pFont;
	BOOL bSelectionBar;
	CImageList m_imgList;
	CWnd* wndResponse;
	//int m_iListMode;
	BOOL Initializing(int nPointSize, int iHeaderHeight, LPCTSTR lpszFaceName, CDC *pDC = NULL);
	//{{AFX_MSG(CListCtrlEx)
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	//}}AFX_MSG
	afx_msg void CListCtrlEx::OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult);

	DECLARE_MESSAGE_MAP()
public:
	CImageList* GetCurImageList() { return &m_imgList; }
	afx_msg void OnNMClick(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnItemchanging(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnDestroy();
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	virtual BOOL PreTranslateMessage(MSG* pMsg);
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_LISTCTRLEX_H__3E6294FD_2953_4C48_A347_6D300ED5B606__INCLUDED_)
