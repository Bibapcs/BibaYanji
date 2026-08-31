// Prime95Doc.h : interface of the CPrime95Doc class
//
/////////////////////////////////////////////////////////////////////////////

class CPrime95Doc : public CDocument
{
protected: // create from serialization only
	CPrime95Doc();
	DECLARE_DYNCREATE(CPrime95Doc)

// Attributes
public:

// Operations
public:
	void rangeStatus ();

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CPrime95Doc)
	public:
	virtual BOOL OnNewDocument();
	virtual void Serialize(CArchive& ar);
	virtual void OnCloseDocument();
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~CPrime95Doc();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:

// Generated message map functions
protected:
	//{{AFX_MSG(CPrime95Doc)
	afx_msg void OnPrimenet();
	afx_msg void OnUpdateWorkers(CCmdUI* pCmdUI);
	afx_msg void OnWorkers();
	afx_msg void OnUpdateContinueSwitcher(CCmdUI* pCmdUI);
	afx_msg void OnContinueSwitcher();
	afx_msg void OnContinue();
	afx_msg void OnUpdateStopSwitcher(CCmdUI* pCmdUI);
	afx_msg void OnStopSwitcher();
	afx_msg void OnStop();
	afx_msg void OnUpdateErrchk(CCmdUI* pCmdUI);
	afx_msg void OnErrchk();
	afx_msg void OnCpu();
	afx_msg void OnResources();
	afx_msg void OnPreferences();
	afx_msg void OnUpdateTest(CCmdUI* pCmdUI);
	afx_msg void OnTest();
	afx_msg void OnUpdateTime(CCmdUI* pCmdUI);
	afx_msg void OnTime();
	afx_msg void OnRangeStatus();
	afx_msg void OnUpdateHelpFinder(CCmdUI* pCmdUI);
	afx_msg void OnTray();
	afx_msg void OnUpdateTray(CCmdUI* pCmdUI);
	afx_msg void OnHide();
	afx_msg void OnUpdateHide(CCmdUI* pCmdUI);
	afx_msg void OnTorture();
	afx_msg void OnUpdateTorture(CCmdUI* pCmdUI);
	afx_msg void OnServer();
	afx_msg void OnUpdateServer(CCmdUI* pCmdUI);
	afx_msg void OnQuitGimps();
	afx_msg void OnService();
	afx_msg void OnUpdateService(CCmdUI* pCmdUI);
	afx_msg void OnManualcomm();
	afx_msg void OnUpdateManualcomm(CCmdUI* pCmdUI);
	afx_msg void OnEcm();
	afx_msg void OnUpdateEcm(CCmdUI* pCmdUI);
	afx_msg void OnPminus1();
	afx_msg void OnUpdatePminus1(CCmdUI* pCmdUI);
	afx_msg void OnWelcome();
	afx_msg void OnUsrTorture();
	afx_msg void OnTitle();
	afx_msg void OnUnreserve();
	afx_msg void OnUpdateUnreserve(CCmdUI* pCmdUI);
	afx_msg void OnUpdateQuit(CCmdUI* pCmdUI);
	afx_msg void OnBenchmark();
	afx_msg void OnUpdateBenchmark(CCmdUI* pCmdUI);
	afx_msg void OnMergeMain();
	afx_msg void OnUpdateMergeMain(CCmdUI* pCmdUI);
	afx_msg void OnMergeComm();
	afx_msg void OnUpdateMergeComm(CCmdUI* pCmdUI);
	afx_msg void OnMergeAll();
	afx_msg void OnUpdateMergeAll(CCmdUI* pCmdUI);
	afx_msg void OnForum();
	afx_msg void OnWiki();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////
