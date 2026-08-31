// ResourcesDlg.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CResourcesDlg dialog

class CResourcesDlg : public CDialog
{
// Construction
public:
	CResourcesDlg(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CResourcesDlg)
	enum { IDD = IDD_RESOURCES };
	float	m_disk;
	float	m_day_memory;
	float	m_night_memory;
	CString	m_start_time;
	CString	m_end_time;
	float	m_upload_bandwidth;
	CString m_upload_start;
	CString m_upload_end;
	UINT	m_download_mb;
	BOOL	m_can_upload;
	CStatic	c_upload_bandwidth_text;
	CEdit	c_upload_bandwidth;
	CStatic	c_upload_start_text;
	CEdit	c_upload_start;
	CStatic	c_upload_end_text;
	CEdit	c_upload_end;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CResourcesDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CResourcesDlg)
	afx_msg void OnAdvanced();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};


/////////////////////////////////////////////////////////////////////////////
// CResourcesAdvancedDlg dialog

class CResourcesAdvancedDlg : public CDialog
{
// Construction
public:
	CResourcesAdvancedDlg(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(PrimenetDlg)
	enum { IDD = IDD_ADVANCED_RESOURCES };
	CString	m_temp_dir;
	CStatic	c_archive_dir_text;
	CEdit	c_archive_dir;
	CString	m_archive_dir;
	float	m_emergency_mem;
	UINT	m_priority;
	CStatic	c_cert_cpu_text;
	CEdit	c_cert_cpu;
	UINT	m_cert_cpu;
	CButton c_hyper_tf;
	BOOL	m_hyper_tf;
	CButton c_hyper_ll;
	BOOL	m_hyper_ll;
	BOOL	m_can_upload;
	BOOL	m_can_download;
	//}}AFX_DATA

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CResourcesAdvancedDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CResourcesAdvancedDlg)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};
