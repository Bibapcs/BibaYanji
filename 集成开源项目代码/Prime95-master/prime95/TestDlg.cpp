// TestDlg.cpp : implementation file
//
//  Copyright 1995-2023 Mersenne Research, Inc. All rights reserved.
//

#include "stdafx.h"
#include "Prime95.h"
#include "TestDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CTestDlg dialog


CTestDlg::CTestDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CTestDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CTestDlg)
	m_worker = 1;
	m_p = 0;
	//}}AFX_DATA_INIT
}


// make sure number is a prime

char NOTPRIMEERR[] = "This number is not prime, there is no need to test it.";
void DDV_prime (
	CDataExchange* pDX,
	long	p)
{
	if (! isPrime (p)) {
		AfxMessageBox (NOTPRIMEERR, MB_ICONEXCLAMATION);
		pDX->Fail ();
	}
}

void CTestDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CTestDlg)
	DDX_Control(pDX, IDC_WORKER_TEXT, c_worker_text);
	DDX_Control(pDX, IDC_WORKER, c_worker);
	DDX_Text(pDX, IDC_WORKER, m_worker);
	DDV_MinMaxUInt(pDX, m_worker, 1, NUM_WORKERS);
	DDX_Text(pDX, IDC_P, m_p);
	//}}AFX_DATA_MAP
	c_worker_text.EnableWindow (NUM_WORKERS > 1);
	c_worker.EnableWindow (NUM_WORKERS > 1);
	DDV_MinMaxUInt(pDX, m_p, MIN_PRIME,
		       CPU_FLAGS & CPU_FMA3 ? MAX_PRIME_FMA3 :
		       CPU_FLAGS & CPU_SSE2 ? MAX_PRIME_SSE2 : MAX_PRIME);
	DDV_prime(pDX, m_p);
}


BEGIN_MESSAGE_MAP(CTestDlg, CDialog)
	//{{AFX_MSG_MAP(CTestDlg)
		// NOTE: the ClassWizard will add message map macros here
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CTestDlg message handlers
