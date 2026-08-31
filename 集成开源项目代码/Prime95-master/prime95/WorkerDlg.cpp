// WorkerDlg.cpp : implementation file
//
// Copyright 1995-2023 Mersenne Research, Inc.  All rights reserved
//

#include "stdafx.h"
#include "Prime95.h"
#include "WorkerDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// In theory, the maximum number of workers should be number of logical cpus.  However, local.txt could specify more workers than logical
// cpus (for example, when local.txt is copied from a dual-core to a single-core machine).  We must let the user manipulate the options on these
// workers that don't have a CPU to run on.

unsigned int max_num_workers (void)
{
	return (max (NUM_WORKERS, HW_NUM_CORES));
}

/////////////////////////////////////////////////////////////////////////////
// CWorkerDlg dialog


CWorkerDlg::CWorkerDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CWorkerDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CWorkerDlg)
	m_num_workers = 1;
	worker_num = 0;
	memset (m_work_pref, 0, sizeof (m_work_pref));
	memset (m_numcpus, 0, sizeof (m_numcpus));
	m_cert_work = 1;
	//}}AFX_DATA_INIT
}

void CWorkerDlg::DoDataExchange(CDataExchange* pDX)
{

	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CWorkerDlg)
	DDX_Control(pDX, IDC_WORKER_TEXT, c_num_workers_text);
	DDX_Control(pDX, IDC_WORKER, c_num_workers);
	DDX_Text(pDX, IDC_WORKER, m_num_workers);
	DDV_MinMaxUInt(pDX, m_num_workers, 1, max_num_workers ());
	DDX_Control(pDX, IDC_WORKGROUP, c_workgroup);

	DDX_Control(pDX, IDC_WORKERNUM_TEXT, c_workernum_text);
	DDX_Control(pDX, IDC_COMBO1, c_workernum);

	DDX_Control(pDX, IDC_WORK_TYPE_TEXT, c_work_pref_text);
	DDX_Control(pDX, IDC_COMBO2, c_work_pref);

	DDX_Control(pDX, IDC_NUMCPUS_TEXT, c_numcpus_text);
	DDX_Control(pDX, IDC_NUMCPUS, c_numcpus);

	DDX_Control(pDX, IDC_CERT, c_cert_work);
	DDX_Check(pDX, IDC_CERT, m_cert_work);
	//}}AFX_DATA_MAP
	c_num_workers_text.EnableWindow (max_num_workers () > 1);
	c_num_workers.EnableWindow (max_num_workers () > 1);
	c_workgroup.EnableWindow (USE_PRIMENET || HW_NUM_CORES > 1);
	c_workernum_text.EnableWindow (m_num_workers > 1);
	c_workernum.EnableWindow (m_num_workers > 1);
	c_work_pref_text.EnableWindow (USE_PRIMENET);
	c_work_pref.EnableWindow (USE_PRIMENET);
	c_numcpus_text.EnableWindow (m_num_workers < HW_NUM_CORES || !AreAllTheSame (m_numcpus) || m_numcpus[0] != 1);
	c_numcpus.EnableWindow (m_num_workers < HW_NUM_CORES || !AreAllTheSame (m_numcpus) || m_numcpus[0] != 1);
	c_cert_work.EnableWindow (USE_PRIMENET);
}


BEGIN_MESSAGE_MAP(CWorkerDlg, CDialog)
	//{{AFX_MSG_MAP(CWorkerDlg)
	ON_EN_KILLFOCUS(IDC_WORKER, &CWorkerDlg::OnEnKillfocusNumWorkers)
	ON_CBN_SELCHANGE(IDC_COMBO1, &CWorkerDlg::OnCbnKillfocusWorkerNum)
	ON_CBN_KILLFOCUS(IDC_COMBO1, &CWorkerDlg::OnCbnKillfocusWorkerNum)
	ON_CBN_KILLFOCUS(IDC_COMBO2, &CWorkerDlg::OnCbnKillfocusWorkType)
	ON_EN_KILLFOCUS(IDC_NUMCPUS, &CWorkerDlg::OnEnKillfocusNumCpus)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CWorkerDlg internal routines

int CWorkerDlg::AreAllTheSame (
	int	*array)
{
	int	i;

	for (i = 1; i < (int) m_num_workers; i++)
		if (array[i-1] != array[i]) return (FALSE);
	return (TRUE);
}

int map_work_pref_to_sel (
	int	work_pref)
{
	switch (work_pref) {
	case PRIMENET_WP_WHATEVER:
		return (0);
	case PRIMENET_WP_PRP_FIRST:
		return (1);
	case PRIMENET_WP_PRP_WORLD_RECORD:
		return (2);
	case PRIMENET_WP_PRP_DBLCHK:
		return (3);
	case PRIMENET_WP_FACTOR:
		return (4);
	case PRIMENET_WP_PFACTOR:
		return (5);
	case PRIMENET_WP_PRP_100M:
		return (6);
	case PRIMENET_WP_PRP_COFACTOR:
		return (7);
	case PRIMENET_WP_PRP_COFACTOR_DBLCHK:
		return (8);
	case PRIMENET_WP_ECM_SMALL:
		return (9);
	case PRIMENET_WP_ECM_COFACTOR:
		return (10);
	case PRIMENET_WP_ECM_FERMAT:
		return (11);
	case PRIMENET_WP_FACTOR_LMH:
		return (12);
	default:
		return (13);
	}
}

int map_sel_to_work_pref (
	int	sel)
{
	switch (sel) {
	case 0:
		return (PRIMENET_WP_WHATEVER);
	case 1:
		return (PRIMENET_WP_PRP_FIRST);
	case 2:
		return (PRIMENET_WP_PRP_WORLD_RECORD);
	case 3:
		return (PRIMENET_WP_PRP_DBLCHK);
	case 4:
		return (PRIMENET_WP_FACTOR);
	case 5:
		return (PRIMENET_WP_PFACTOR);
	case 6:
		return (PRIMENET_WP_PRP_100M);
	case 7:
		return (PRIMENET_WP_PRP_COFACTOR);
	case 8:
		return (PRIMENET_WP_PRP_COFACTOR_DBLCHK);
	case 9:
		return (PRIMENET_WP_ECM_SMALL);
	case 10:
		return (PRIMENET_WP_ECM_COFACTOR);
	case 11:
		return (PRIMENET_WP_ECM_FERMAT);
	case 12:
		return (PRIMENET_WP_FACTOR_LMH);
	}
	return (-1);
}

void CWorkerDlg::InitComboBoxText (void)
{
	int	i, sel;
	char	buf[80];

// Populate the worker number combo box

	c_workernum_text.EnableWindow (m_num_workers > 1);
	c_workernum.EnableWindow (m_num_workers > 1);
	c_workernum.ResetContent ();
	if (m_num_workers == 1) {
		worker_num = 1;
		c_workernum.AddString ("Worker #1");
		c_workernum.SetCurSel (0);
	} else {
		if (worker_num > (int) m_num_workers) worker_num = 0;
		c_workernum.AddString ("All workers");
		for (i = 1; i <= (int) m_num_workers; i++) {
			sprintf (buf, "Worker #%d", i);
			c_workernum.AddString (buf);
		}
		c_workernum.SetCurSel (worker_num);
	}

// Populate the work type combo box

	c_work_pref.ResetContent ();
	if (worker_num > 0)
		sel = map_work_pref_to_sel (m_work_pref[worker_num-1]);
	else if (AreAllTheSame (m_work_pref))
		sel = map_work_pref_to_sel (m_work_pref[0]);
	else {
		c_work_pref.AddString ("Mixed Settings");
		sel = 0;
	}
	c_work_pref.AddString ("Whatever makes the most sense");
	c_work_pref.AddString ("First time prime tests");
	c_work_pref.AddString ("World record sized numbers to prime test");
	c_work_pref.AddString ("Double-check prime tests");
	c_work_pref.AddString ("Trial factoring");
	c_work_pref.AddString ("P-1 factoring");
	c_work_pref.AddString ("100,000,000 digit numbers to prime test");
	c_work_pref.AddString ("First time PRP on Mersenne cofactors");
	c_work_pref.AddString ("Double-check PRP on Mersenne cofactors");
	c_work_pref.AddString ("ECM for first factors of Mersenne numbers");
	c_work_pref.AddString ("ECM on Mersenne cofactors");
	c_work_pref.AddString ("ECM on Fermat numbers");
	c_work_pref.AddString ("Trial factoring to low limits");
	if (sel == 13) c_work_pref.AddString ("Other");
	c_work_pref.SetCurSel (sel);

// Populate the num cpus edit box.

	if (worker_num > 0) {
		sprintf (buf, "%d", m_numcpus[worker_num-1]);
		c_numcpus.SetWindowText (buf);
	} else if (AreAllTheSame (m_numcpus)) {
		sprintf (buf, "%d", m_numcpus[0]);
		c_numcpus.SetWindowText (buf);
	} else {
		c_numcpus.SetWindowText ("Mixed");
	}
	c_numcpus_text.EnableWindow (m_num_workers < HW_NUM_CORES || !AreAllTheSame (m_numcpus) || m_numcpus[0] != 1);
	c_numcpus.EnableWindow (m_num_workers < HW_NUM_CORES || !AreAllTheSame (m_numcpus) || m_numcpus[0] != 1);
}


/////////////////////////////////////////////////////////////////////////////
// CWorkerDlg message handlers

BOOL CWorkerDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	InitComboBoxText ();

	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CWorkerDlg::OnEnKillfocusNumWorkers()
{
	char	buf[80];
	unsigned int num_workers;

	c_num_workers.GetWindowText (buf, sizeof (buf));
	num_workers = atoi (buf);
	if (num_workers < 1) num_workers = 1;
	if (num_workers > max_num_workers ()) num_workers = max_num_workers ();
	sprintf (buf, "%d", num_workers);
	c_num_workers.SetWindowText (buf);
	if (num_workers != m_num_workers) {
		m_num_workers = num_workers;
		InitComboBoxText ();		// Might change thread_num if we reduced number of workers
	}
}

void CWorkerDlg::OnCbnKillfocusWorkerNum()
{
	worker_num = c_workernum.GetCurSel ();
	InitComboBoxText ();	// Display data for entered worker num
}

void CWorkerDlg::OnCbnKillfocusWorkType()
{
	int	sel, i, work_pref, min_cores;
	char	buf[10];

	sel = c_work_pref.GetCurSel ();
	if (worker_num) {
		work_pref = map_sel_to_work_pref (sel);
		min_cores = min_cores_for_work_pref (work_pref);
		if (work_pref != -1) {
			m_work_pref[worker_num-1] = work_pref;
			if (m_numcpus[worker_num-1] < min_cores) {
				m_numcpus[worker_num-1] = min_cores;
				sprintf (buf, "%d", m_numcpus[worker_num-1]);
				c_numcpus.SetWindowText (buf);
			}
		}
	}
	else if (AreAllTheSame (m_work_pref)) {
		work_pref = map_sel_to_work_pref (sel);
		min_cores = min_cores_for_work_pref (work_pref);
		if (work_pref != -1) {
			for (i = 0; i < MAX_NUM_WORKERS; i++) {
				m_work_pref[i] = work_pref;
				if (m_numcpus[i] < min_cores) m_numcpus[i] = min_cores;
			}
			InitComboBoxText ();
		}
	} else if (sel) {
		work_pref = map_sel_to_work_pref (sel-1);
		min_cores = min_cores_for_work_pref (work_pref);
		for (i = 0; i < MAX_NUM_WORKERS; i++) {
			m_work_pref[i] = work_pref;
			if (m_numcpus[i] < min_cores) m_numcpus[i] = min_cores;
		}
		InitComboBoxText ();
	}
}

void CWorkerDlg::OnEnKillfocusNumCpus()
{
	char	buf[80];
	unsigned int num_cpus, min_cores;
	int	i;

	c_numcpus.GetWindowText (buf, sizeof (buf));
	if (buf[0] >= '0' && buf[0] <= '9') {
		num_cpus = atoi (buf);
		if (num_cpus < 1) num_cpus = 1;
		if (num_cpus > HW_NUM_CORES) num_cpus = HW_NUM_CORES;
		if (worker_num) {
			min_cores = min_cores_for_work_pref (m_work_pref[worker_num-1]);
			if (num_cpus < min_cores) num_cpus = min_cores;
			m_numcpus[worker_num-1] = num_cpus;
			sprintf (buf, "%d", num_cpus);
			c_numcpus.SetWindowText (buf);
		} else {
			for (i = 0; i < MAX_NUM_WORKERS; i++) {
				min_cores = min_cores_for_work_pref (m_work_pref[i]);
				m_numcpus[i] = (num_cpus > min_cores ? num_cpus : min_cores);
			}
			if (AreAllTheSame (m_numcpus)) {
				sprintf (buf, "%d", m_numcpus[0]);
				c_numcpus.SetWindowText (buf);
			} else {
				c_numcpus.SetWindowText ("Mixed");
			}
		}
	}
	c_numcpus_text.EnableWindow (m_num_workers < HW_NUM_CORES || !AreAllTheSame (m_numcpus) || m_numcpus[0] != 1);
	c_numcpus.EnableWindow (m_num_workers < HW_NUM_CORES || !AreAllTheSame (m_numcpus) || m_numcpus[0] != 1);
}

