// CpuDlg.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CCpuDlg dialog

class CCpuDlg : public CDialog
{
// Construction
public:
	CCpuDlg(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CCpuDlg)
	enum { IDD = IDD_CPU };
	UINT	m_hours;
	CString	m_cpu_info;
	//}}AFX_DATA

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CCpuDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CCpuDlg)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};
