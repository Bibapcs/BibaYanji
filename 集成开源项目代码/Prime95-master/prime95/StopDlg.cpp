// StopDlg.cpp : implementation file
//
//  Copyright 1995-2023 Mersenne Research, Inc. All rights reserved.
//

#include "stdafx.h"
#include "Prime95.h"
#include "StopDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CStopDlg dialog


CStopDlg::CStopDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CStopDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CStopDlg)
	m_worker = 1;
	m_all_workers = 1;
	//}}AFX_DATA_INIT
}

void CStopDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CStopDlg)
	DDX_Check(pDX, IDC_RADIO1, m_all_workers);
	DDX_Control(pDX, IDC_WORKER_TEXT, c_worker_text);
	DDX_Control(pDX, IDC_WORKER, c_worker);
	DDX_Text(pDX, IDC_WORKER, m_worker);
	DDV_MinMaxUInt(pDX, m_worker, 1, WORKERS_ACTIVE);
	//}}AFX_DATA_MAP
	c_worker_text.EnableWindow (!m_all_workers);
	c_worker.EnableWindow (!m_all_workers);
}


BEGIN_MESSAGE_MAP(CStopDlg, CDialog)
	//{{AFX_MSG_MAP(CStopDlg)
	ON_BN_CLICKED(IDC_RADIO1, OnBnClickedRadio1)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()


/////////////////////////////////////////////////////////////////////////////
// CStopDlg message handlers

void CStopDlg::OnBnClickedRadio1()
{
	UpdateData ();
	c_worker_text.EnableWindow (!m_all_workers);
	c_worker.EnableWindow (!m_all_workers);
}

