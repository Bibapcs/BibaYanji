// PreferencesDlg.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CPreferencesDlg dialog

class CPreferencesDlg : public CDialog
{
// Construction
public:
	CPreferencesDlg(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CPreferencesDlg)
	enum { IDD = IDD_PREFERENCES };
	CStatic	c_modem_text;
	CEdit	c_modem;
	CStatic	c_work_text;
	CEdit	c_work;
	CStatic	c_end_dates_text;
	CEdit	c_end_dates;
	CStatic	c_network_text;
	CEdit	c_network;
	UINT	m_iter;
	UINT	m_disk_write_time;
	UINT	m_backup;
	BOOL	m_noise;
	UINT	m_retry;
	UINT	m_r_iter;
	UINT	m_work;
	float	m_end_dates;
	UINT	m_modem;
	BOOL	m_battery;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CPreferencesDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CPreferencesDlg)
		// NOTE: the ClassWizard will add member functions here
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};
