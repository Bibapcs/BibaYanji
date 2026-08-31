// PrimenetDlg.h : header file
//

#include "hyperlink.h"

/////////////////////////////////////////////////////////////////////////////
// PrimenetDlg dialog

class PrimenetDlg : public CDialog
{
// Construction
public:
	PrimenetDlg(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(PrimenetDlg)
	enum { IDD = IDD_PRIMENET };
	BOOL	m_primenet;
	CHyperLink m_new_user_link;
	CString	m_userid;
	CString	m_compid;
	CButton	c_connection;
	BOOL	m_dialup;
	CString	m_proxyhost;
	UINT	m_proxyport;
	CString	m_proxyuser;
	CString	m_proxypassword;
	BOOL	m_debug;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(PrimenetDlg)
	protected:
	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(PrimenetDlg)
	afx_msg void OnPrimenet();
	afx_msg void OnConnection();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};


/////////////////////////////////////////////////////////////////////////////
// PrimenetConnectionDlg dialog

class PrimenetConnectionDlg : public CDialog
{
// Construction
public:
	PrimenetConnectionDlg(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(PrimenetDlg)
	enum { IDD = IDD_PRIMENET_CONNECTION };
	BOOL	m_dialup;
	CString	m_proxyhost;
	CStatic	c_proxyport_text;
	CEdit	c_proxyport;
	UINT	m_proxyport;
	CStatic	c_proxyuser_text;
	CEdit	c_proxyuser;
	CString	m_proxyuser;
	CStatic	c_proxypassword_text;
	CEdit	c_proxypassword;
	CString	m_proxypassword;
	BOOL	m_debug;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(PrimenetConnectionDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(PrimenetConnectionDlg)
	afx_msg void OnEnChangeProxyHost();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};
