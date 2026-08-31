// WorkerDlg.h : header file
//
// Copyright 1995-2023 Mersenne Research, Inc.  All rights reserved
//

/////////////////////////////////////////////////////////////////////////////
// CWorkerDlg dialog

class CWorkerDlg : public CDialog
{
// Construction
public:
	CWorkerDlg(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CWorkerDlg)
	enum { IDD = IDD_WORKERS };
	CStatic	c_num_workers_text;
	CEdit	c_num_workers;
	UINT	m_num_workers;
	CButton	c_workgroup;
	CStatic	c_workernum_text;
	CComboBox c_workernum;
	CStatic	c_work_pref_text;
	CComboBox c_work_pref;
	int	m_work_pref[MAX_NUM_WORKERS];
	CStatic	c_numcpus_text;
	CEdit	c_numcpus;
	int	m_numcpus[MAX_NUM_WORKERS];
	CButton	c_cert_work;
	int	m_cert_work;
	//}}AFX_DATA

private:
	int	worker_num;
public:
	int AreAllTheSame (int *);
	void InitComboBoxText (void);

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CWorkerDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CWorkerDlg)
	virtual BOOL OnInitDialog();
	afx_msg void OnEnKillfocusNumWorkers();
	afx_msg void OnCbnKillfocusWorkerNum();
	afx_msg void OnCbnKillfocusWorkType();
	afx_msg void OnEnKillfocusNumCpus();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

