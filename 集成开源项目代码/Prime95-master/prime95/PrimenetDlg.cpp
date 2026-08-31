// PrimenetDlg.cpp : implementation file
//

#include "stdafx.h"
#include "Prime95.h"
#include "PrimenetDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// PrimenetDlg dialog


PrimenetDlg::PrimenetDlg(CWnd* pParent /*=NULL*/)
	: CDialog(PrimenetDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(PrimenetDlg)
	m_primenet = FALSE;
	m_userid = _T("");
	m_compid = _T("");
	m_dialup = FALSE;
	m_proxyhost = _T("");
	m_proxyport = 8080;
	m_proxyuser = _T("");
	m_proxypassword = _T("");
	m_debug = 0;
	//}}AFX_DATA_INIT
}


void PrimenetDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(PrimenetDlg)
	DDX_Check(pDX, IDC_PRIMENET, m_primenet);
	DDX_Control(pDX, IDC_LINK, m_new_user_link);
	DDX_Text(pDX, IDC_USERID, m_userid);
	DDV_MaxChars(pDX, m_userid, 20);
	DDX_Text(pDX, IDC_COMPID, m_compid);
	DDV_MaxChars(pDX, m_compid, 20);
	DDX_Control(pDX, IDC_CONNECTION, c_connection);
	//}}AFX_DATA_MAP
	c_connection.EnableWindow (m_primenet);
}


BEGIN_MESSAGE_MAP(PrimenetDlg, CDialog)
	//{{AFX_MSG_MAP(PrimenetDlg)
	ON_BN_CLICKED(IDC_PRIMENET, OnPrimenet)
	ON_BN_CLICKED(IDC_CONNECTION, OnConnection)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()


/////////////////////////////////////////////////////////////////////////////
// PrimenetDlg message handlers

BOOL PrimenetDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	m_new_user_link.SetURL(_T("http://v5www.mersenne.org/update/"));
	// m_new_user_link.SetUnderline(CHyperLink::ulAlways);

	return TRUE;  // return TRUE  unless you set the focus to a control
}

void PrimenetDlg::OnPrimenet() 
{
	UpdateData ();		// Get the values from the dialog box
}

void PrimenetDlg::OnConnection() 
{
	PrimenetConnectionDlg dlg;

	dlg.m_dialup = m_dialup;
	dlg.m_proxyhost = m_proxyhost;
	dlg.m_proxyport = m_proxyport;
	dlg.m_proxyuser = m_proxyuser;
	dlg.m_proxypassword = m_proxypassword;
	dlg.m_debug = m_debug;
	if (dlg.DoModal () == IDOK) {
		m_dialup = dlg.m_dialup;
		m_proxyhost = dlg.m_proxyhost;
		m_proxyport = dlg.m_proxyport;
		m_proxyuser = dlg.m_proxyuser;
		m_proxypassword = dlg.m_proxypassword;
		m_debug = dlg.m_debug;
	}
}


/////////////////////////////////////////////////////////////////////////////
// PrimenetConnectionDlg dialog


PrimenetConnectionDlg::PrimenetConnectionDlg(CWnd* pParent /*=NULL*/)
	: CDialog(PrimenetConnectionDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(PrimenetConnectionDlg)
	m_dialup = FALSE;
	m_proxyhost = _T("");
	m_proxyport = 8080;
	m_proxyuser = _T("");
	m_proxypassword = _T("");
	m_debug = 0;
	//}}AFX_DATA_INIT
}


void PrimenetConnectionDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(PrimenetDlg)
	DDX_Check(pDX, IDC_DIALUP, m_dialup);
	DDX_Text(pDX, IDC_PROXYHOST, m_proxyhost);
	DDX_Control(pDX, IDC_PROXYPORT_TEXT, c_proxyport_text);
	DDX_Control(pDX, IDC_PROXYPORT, c_proxyport);
	DDX_Text(pDX, IDC_PROXYPORT, m_proxyport);
	DDV_MinMaxUInt(pDX, m_proxyport, 0, 65535);
	DDX_Control(pDX, IDC_PROXYUSER_TEXT, c_proxyuser_text);
	DDX_Control(pDX, IDC_PROXYUSER, c_proxyuser);
	DDX_Text(pDX, IDC_PROXYUSER, m_proxyuser);
	DDX_Control(pDX, IDC_PROXYPASSWORD_TEXT, c_proxypassword_text);
	DDX_Control(pDX, IDC_PROXYPASSWORD, c_proxypassword);
	DDX_Text(pDX, IDC_PROXYPASSWORD, m_proxypassword);
	DDX_Check(pDX, IDC_DEBUG, m_debug);
	//}}AFX_DATA_MAP
	c_proxyport_text.EnableWindow (m_proxyhost[0]);
	c_proxyport.EnableWindow (m_proxyhost[0]);
	c_proxyuser_text.EnableWindow (m_proxyhost[0]);
	c_proxyuser.EnableWindow (m_proxyhost[0]);
	c_proxypassword_text.EnableWindow (m_proxyhost[0]);
	c_proxypassword.EnableWindow (m_proxyhost[0]);
}


BEGIN_MESSAGE_MAP(PrimenetConnectionDlg, CDialog)
	//{{AFX_MSG_MAP(PrimenetConnectionDlg)
	ON_EN_CHANGE(IDC_PROXYHOST, &PrimenetConnectionDlg::OnEnChangeProxyHost)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// PrimenetConnectionDlg message handlers

void PrimenetConnectionDlg::OnEnChangeProxyHost() 
{
	UpdateData ();		// Get the values from the dialog box
}

