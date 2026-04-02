#pragma once
#if _SYSTEM_AMTAFT_

#include "Ani_Data_Serever_PC.h"
#include "afxwin.h"
#include "VisionThread.h"
#include "ViewingAngleThread.h"
#include "LumitopThread.h"
#include "ClrListBox.h"

// 自定义消息：Vision 日志
#define WM_VISION_LOG (WM_USER + 100)

// CDlgMainView �� ���Դϴ�.

class CDlgMainView : public CDialog
{
	DECLARE_DYNAMIC(CDlgMainView)

public:
	CDlgMainView(CWnd* pParent = NULL);   // ǥ�� �������Դϴ�.
	virtual ~CDlgMainView();

	// ��ȭ ���� �������Դϴ�.
	enum { IDD = DLG_MAIN_BOX };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV �����Դϴ�.

	DECLARE_MESSAGE_MAP()
public:
	virtual BOOL OnInitDialog();
	void SetWindowPosition();
	DECLARE_EVENTSINK_MAP()
	void OnClickIdbSystem();
	void SocketServerOpen();

public:

	CClrListBox m_ViewingAngleListBox[2];
	CClrListBox m_VisionListBox[2];

	virtual void OnCancel();
	virtual void OnOK();
	void OnClickIdbViewingAngleLogClean();
	void OnClickIdbViewingAngleLogClean2();
	void OnClickIdbVisionPcLogClean();
	void OnClickIdbVisionPcLogClean3();
	void StringChanged();
	void StringChnageMsg(int btn, CString strKor, CString strEng, CString strChi);

	CBtnEnh m_btnViewingAngleModelChange1;
	CBtnEnh m_btnViewingAngleModelChange2;
	CBtnEnh m_btnViewingAngleModelChange3;
	CBtnEnh m_btnViewingAngleModelChange4;
	CBtnEnh m_btnVisionModelChange1;
	CBtnEnh m_btnVisionModelChange2;
	afx_msg void OnTimer(UINT_PTR nIDEvent);

	// ICW ?���������?��
	afx_msg LRESULT OnICWConnected(WPARAM wParam, LPARAM lParam);

	// Vision 线程日志消息处理
	afx_msg LRESULT OnVisionLog(WPARAM wParam, LPARAM lParam);
};
extern CDlgMainView *g_DlgMainView;

#endif