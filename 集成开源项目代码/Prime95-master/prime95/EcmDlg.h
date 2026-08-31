// EcmDlg.h : header file
//
//  Copyright 2000-2021 Mersenne Research, Inc. All rights reserved.
//

/////////////////////////////////////////////////////////////////////////////
// CEcmDlg dialog

class CEcmDlg : public CDialog
{
// Construction
public:
	CEcmDlg(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CEcmDlg)
	enum { IDD = IDD_ECM };
	CStatic	c_worker_text;
	CEdit	c_worker;
	UINT	m_worker;
	double	m_k;
	UINT	m_b;
	UINT	m_n;
	long	m_c;
	uint64_t m_bound1;
	uint64_t m_bound2;
	UINT	m_num_curves;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CEcmDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CEcmDlg)
		// NOTE: the ClassWizard will add member functions here
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};
