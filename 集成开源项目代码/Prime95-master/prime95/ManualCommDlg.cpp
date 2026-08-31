// ManualCommDlg.cpp : implementation file
//

#include "stdafx.h"
#include "Prime95.h"
#include "ManualCommDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CManualCommDlg dialog


CManualCommDlg::CManualCommDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CManualCommDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CManualCommDlg)
	m_manual_comm = FALSE;
	m_comm_now = FALSE;
	m_new_dates = FALSE;
	//}}AFX_DATA_INIT
}


void CManualCommDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CManualCommDlg)
	DDX_Control(pDX, IDC_NOW, c_comm_now);
	DDX_Check(pDX, IDC_MANUAL, m_manual_comm);
	DDX_Check(pDX, IDC_NOW, m_comm_now);
	DDX_Check(pDX, IDC_COMPLETION, m_new_dates);
	//}}AFX_DATA_MAP
//	c_comm_now.EnableWindow (m_manual_comm);
}


BEGIN_MESSAGE_MAP(CManualCommDlg, CDialog)
	//{{AFX_MSG_MAP(CManualCommDlg)
	ON_BN_CLICKED(IDC_MANUAL, OnManual)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CManualCommDlg message handlers

void CManualCommDlg::OnManual() 
{
	UpdateData ();	// Get the values from the dialog box
}
