// StartDlg.cpp : implementation file
//
//  Copyright 1995-2023 Mersenne Research, Inc. All rights reserved.
//

#include "stdafx.h"
#include "Prime95.h"
#include "StartDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CStartDlg dialog


CStartDlg::CStartDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CStartDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CStartDlg)
	m_worker = 1;
	m_all_workers = 1;
	//}}AFX_DATA_INIT
}

void CStartDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CStartDlg)
	DDX_Check(pDX, IDC_RADIO1, m_all_workers);
	DDX_Control(pDX, IDC_WORKER_TEXT, c_worker_text);
	DDX_Control(pDX, IDC_WORKER, c_worker);
	DDX_Text(pDX, IDC_WORKER, m_worker);
	DDV_MinMaxUInt(pDX, m_worker, 1, WORKERS_ACTIVE > NUM_WORKERS ? WORKERS_ACTIVE : NUM_WORKERS);
	//}}AFX_DATA_MAP
	c_worker_text.EnableWindow (!m_all_workers);
	c_worker.EnableWindow (!m_all_workers);
}


BEGIN_MESSAGE_MAP(CStartDlg, CDialog)
	//{{AFX_MSG_MAP(CStartDlg)
	ON_BN_CLICKED(IDC_RADIO1, OnBnClickedRadio1)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()


/////////////////////////////////////////////////////////////////////////////
// CStartDlg message handlers

void CStartDlg::OnBnClickedRadio1()
{
	UpdateData ();
	c_worker_text.EnableWindow (!m_all_workers);
	c_worker.EnableWindow (!m_all_workers);
}

