// BenchmarkDlg.cpp : implementation file
//
// Copyright 2017-2023 Mersenne Research, Inc.  All rights reserved
//

#include "stdafx.h"
#include "Prime95.h"
#include "BenchmarkDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CBenchmarkDlg dialog


CBenchmarkDlg::CBenchmarkDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CBenchmarkDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CBenchmarkDlg)
	m_bench_type = 0;
	m_minFFT = 0;
	m_maxFFT = 0;
	m_errchk = 0;
	m_negacyclic = 0;
	m_limit_FFT_sizes = 0;
	m_hyperthreading = 0;
	m_all_FFT_impl = 0;
	m_bench_time = 0;
	//}}AFX_DATA_INIT
}


void CBenchmarkDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CBenchmarkDlg)
	DDX_Control(pDX, IDC_COMBO1, c_bench_type);

	DDX_Control(pDX, IDC_MINFFT_TEXT, c_minFFT_text);
	DDX_Control(pDX, IDC_MINFFT, c_minFFT);
	DDX_Text(pDX, IDC_MINFFT, m_minFFT);
	DDV_MinMaxUInt(pDX, m_minFFT, 1, 65536);
	DDX_Control(pDX, IDC_MAXFFT_TEXT, c_maxFFT_text);
	DDX_Control(pDX, IDC_MAXFFT, c_maxFFT);
	DDX_Text(pDX, IDC_MAXFFT, m_maxFFT);
	DDV_MinMaxUInt(pDX, m_maxFFT, 1, 65536);
	DDX_Control(pDX, IDC_ERRCHK, c_errchk);
	DDX_Check(pDX, IDC_ERRCHK, m_errchk);
	DDX_Control(pDX, IDC_NEGACYCLIC, c_negacyclic);
	DDX_Check(pDX, IDC_NEGACYCLIC, m_negacyclic);
	DDX_Control(pDX, IDC_LIMIT_FFT_SIZES, c_limit_FFT_sizes);
	DDX_Check(pDX, IDC_LIMIT_FFT_SIZES, m_limit_FFT_sizes);

	DDX_Control(pDX, IDC_CORES_TEXT, c_bench_cores_text);
	DDX_Control(pDX, IDC_CORES, c_bench_cores);
	DDX_Text(pDX, IDC_CORES, m_bench_cores);
	DDX_Control(pDX, IDC_HYPERTHREADING, c_hyperthreading);
	DDX_Check(pDX, IDC_HYPERTHREADING, m_hyperthreading);

	DDX_Control(pDX, IDC_WORKERS_TEXT, c_bench_workers_text);
	DDX_Control(pDX, IDC_WORKERS, c_bench_workers);
	DDX_Text(pDX, IDC_WORKERS, m_bench_workers);
	DDX_Control(pDX, IDC_ALL_FFT_IMPL, c_all_FFT_impl);
	DDX_Check(pDX, IDC_ALL_FFT_IMPL, m_all_FFT_impl);
	DDX_Control(pDX, IDC_TIMEFFT_TEXT, c_bench_time_text);
	DDX_Control(pDX, IDC_TIMEFFT, c_bench_time);
	DDX_Text(pDX, IDC_TIMEFFT, m_bench_time);
	DDV_MinMaxUInt(pDX, m_bench_time, 5, 60);
	//}}AFX_DATA_MAP
	c_minFFT_text.EnableWindow (m_bench_type != 2);
	c_minFFT.EnableWindow (m_bench_type != 2);
	c_maxFFT_text.EnableWindow (m_bench_type != 2);
	c_maxFFT.EnableWindow (m_bench_type != 2);
	c_errchk.EnableWindow (m_bench_type != 2);
	c_negacyclic.EnableWindow (m_bench_type != 2);
	c_limit_FFT_sizes.EnableWindow (m_minFFT != m_maxFFT && ((m_bench_type == 0 && !m_all_FFT_impl) || m_bench_type == 1));
	c_bench_cores_text.EnableWindow (HW_NUM_CORES > 1);
	c_bench_cores.EnableWindow (HW_NUM_CORES > 1);
	c_hyperthreading.EnableWindow (HW_NUM_CORES != HW_NUM_THREADS);
	c_bench_workers_text.EnableWindow (m_bench_type == 0 && (HW_NUM_CORES > 1 || m_hyperthreading));
	c_bench_workers.EnableWindow (m_bench_type == 0 && (HW_NUM_CORES > 1 || m_hyperthreading));
	c_all_FFT_impl.EnableWindow (m_bench_type == 0);
	c_bench_time_text.EnableWindow (m_bench_type == 0);
	c_bench_time.EnableWindow (m_bench_type == 0);
}


BEGIN_MESSAGE_MAP(CBenchmarkDlg, CDialog)
	//{{AFX_MSG_MAP(CBenchmarkDlg)
	ON_EN_CHANGE(IDC_COMBO1, &CBenchmarkDlg::OnEnChangeBenchType)
	ON_CBN_KILLFOCUS(IDC_COMBO1, &CBenchmarkDlg::OnCbnKillfocusBenchType)
	ON_EN_KILLFOCUS(IDC_MINFFT, &CBenchmarkDlg::OnEnKillfocusMinFFT)
	ON_EN_KILLFOCUS(IDC_MAXFFT, &CBenchmarkDlg::OnEnKillfocusMaxFFT)
	ON_BN_CLICKED(IDC_ALL_FFT_IMPL, &CBenchmarkDlg::OnBnClickedAllFFTImpl)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CBenchmarkDlg message handlers

BOOL CBenchmarkDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

// Populate the benchmark type combo box

	c_bench_type.ResetContent ();
	c_bench_type.AddString ("Throughput benchmark");
	c_bench_type.AddString ("FFT timings benchmark");
	c_bench_type.AddString ("Trial factoring benchmark");
	c_bench_type.SetCurSel (0);

	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CBenchmarkDlg::OnEnChangeBenchType() 
{
	m_bench_type = c_bench_type.GetCurSel ();
	if (m_bench_type != 0) c_all_FFT_impl.SetCheck (BST_UNCHECKED);
	UpdateData ();		// Get the values from the dialog box
}

void CBenchmarkDlg::OnCbnKillfocusBenchType()
{
	m_bench_type = c_bench_type.GetCurSel ();
	if (m_bench_type != 0) c_all_FFT_impl.SetCheck (BST_UNCHECKED);
	UpdateData ();		// Get the values from the dialog box
}

void CBenchmarkDlg::OnEnKillfocusMinFFT()
{
	UpdateData ();		// Get the values from the dialog box
}

void CBenchmarkDlg::OnEnKillfocusMaxFFT()
{
	UpdateData ();		// Get the values from the dialog box
}

void CBenchmarkDlg::OnBnClickedAllFFTImpl()
{
	UpdateData ();		// Get the values from the dialog box
	if (m_all_FFT_impl) c_limit_FFT_sizes.SetCheck (BST_UNCHECKED);
	UpdateData ();		// Get the values from the dialog box
}

