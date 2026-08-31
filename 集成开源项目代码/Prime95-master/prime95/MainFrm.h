// MainFrm.h : interface of the CMainFrame class
// Copyright 1995-2020 Mersenne Research, Inc.  All rights reserved
//
/////////////////////////////////////////////////////////////////////////////
const int MYWM_TRAYMESSAGE = WM_APP + 100;

#define ID_WINDOW_POSITION              0xE136

class CMainFrame : public CMDIFrameWnd
{
	DECLARE_DYNCREATE(CMainFrame)
public:
	CMainFrame();

// Attributes
public:

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMainFrame)
	public:
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	virtual void OnUpdateFrameTitle(BOOL bAddToTitle);
	virtual BOOL DestroyWindow();
	protected:
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	//}}AFX_VIRTUAL
	afx_msg LRESULT OnPower( WPARAM, LPARAM );
	afx_msg LRESULT OnTrayMessage( WPARAM, LPARAM );
	LRESULT OnTaskBarCreated (WPARAM, LPARAM);

// Implementation
public:
	virtual ~CMainFrame();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

	CStatusBar  m_wndStatusBar;
protected:  // control bar embedded members

// Generated message map functions
protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	//{{AFX_MSG(CMainFrame)
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnEndSession(BOOL bEnding);
	afx_msg void OnActivateApp(BOOL bActive, DWORD hTask);
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnTrayOpenWindow();
	afx_msg void OnStopContinue();
	afx_msg void OnTile();
	afx_msg void OnPosition();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
public:
};

/////////////////////////////////////////////////////////////////////////////
