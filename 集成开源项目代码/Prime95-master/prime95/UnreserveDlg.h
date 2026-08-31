// UnreserveDlg.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CUnreserveDlg dialog

class CUnreserveDlg : public CDialog
{
// Construction
public:
	CUnreserveDlg(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CUnreserveDlg)
	enum { IDD = IDD_UNRESERVE };
	UINT	m_p;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CUnreserveDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CUnreserveDlg)
		// NOTE: the ClassWizard will add member functions here
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};
