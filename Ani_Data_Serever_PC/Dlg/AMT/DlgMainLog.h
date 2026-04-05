#pragma once
#if _SYSTEM_AMTAFT_

// CDlgMainLog ??? ????????.
#include "resource.h"
#include "btnenh.h"
#include "ClrListBox.h"

// ????????
#define WM_PLC_LOG (WM_USER + 101)
#define WM_TOUCH_LOG (WM_USER + 102)
#define WM_ALIGN_LOG (WM_USER + 103)
#define WM_PG_LOG (WM_USER + 104)

// CDlgMainLog ??? ????????.
class CDlgMainLog : public CDialogEx
{
	DECLARE_DYNAMIC(CDlgMainLog)

public:
	CDlgMainLog(CWnd* pParent = NULL);   // ??? ??????????.
	virtual ~CDlgMainLog();

// ??? ???? ??????????.
	enum { IDD = DLG_MAIN_LOG_BOX };
	CClrListBox m_PlcListBox;
	CClrListBox m_TouchListBox;
	CClrListBox m_AlignListBox;
	CClrListBox m_PgListBox;
	void StringChanged();
	void StringChnageMsg(int btn, CString strKor, CString strEng, CString strChi);

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV ????????.

	DECLARE_MESSAGE_MAP()
public:
	// ���?����?
	LRESULT OnPlcLog(WPARAM wParam, LPARAM lParam);
	LRESULT OnTouchLog(WPARAM wParam, LPARAM lParam);
	LRESULT OnAlignLog(WPARAM wParam, LPARAM lParam);
	LRESULT OnPgLog(WPARAM wParam, LPARAM lParam);
	
	virtual BOOL OnInitDialog();
	DECLARE_EVENTSINK_MAP()
	void OnClickIdbAlignLogClean();
	void OnClickIdbTouchLogClean();
	void OnClickIdbPgClean();
	void OnClickIdbPlcLogClean();
	CBtnEnh m_btnAlignModelChange;
	
	afx_msg void OnTimer(UINT_PTR nIDEvent);

};
extern CDlgMainLog *g_MainLog;

#endif