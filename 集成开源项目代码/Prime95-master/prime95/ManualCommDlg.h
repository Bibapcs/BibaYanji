// ManualCommDlg.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CManualCommDlg dialog

class CManualCommDlg : public CDialog
{
// Construction
public:
	CManualCommDlg(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CManualCommDlg)
	enum { IDD = IDD_MANUAL_COMM };
	CButton	c_comm_now;
	BOOL	m_manual_comm;
	BOOL	m_comm_now;
	BOOL	m_new_dates;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CManualCommDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CManualCommDlg)
	afx_msg void OnManual();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};
