// UnreserveDlg.cpp : implementation file
//

#include "stdafx.h"
#include "Prime95.h"
#include "UnreserveDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CUnreserveDlg dialog


CUnreserveDlg::CUnreserveDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CUnreserveDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CUnreserveDlg)
	m_p = 0;
	//}}AFX_DATA_INIT
}


void CUnreserveDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CUnreserveDlg)
	DDX_Text(pDX, IDC_P, m_p);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CUnreserveDlg, CDialog)
	//{{AFX_MSG_MAP(CUnreserveDlg)
		// NOTE: the ClassWizard will add message map macros here
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CUnreserveDlg message handlers
