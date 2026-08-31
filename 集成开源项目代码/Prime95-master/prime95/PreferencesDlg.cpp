// PreferencesDlg.cpp : implementation file
//

#include "stdafx.h"
#include "Prime95.h"
#include "PreferencesDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CPreferencesDlg dialog


CPreferencesDlg::CPreferencesDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CPreferencesDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CPreferencesDlg)
	m_iter = 0;
	m_disk_write_time = 0;
	m_backup = FALSE;
	m_noise = FALSE;
	m_retry = 0;
	m_r_iter = 0;
	m_work = 0;
	m_end_dates = 0;
	m_modem = 0;
	m_battery = FALSE;
	//}}AFX_DATA_INIT
}


void CPreferencesDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CPreferencesDlg)
	DDX_Control(pDX, IDC_MODEM_TEXT, c_modem_text);
	DDX_Control(pDX, IDC_MODEM, c_modem);
	DDX_Control(pDX, IDC_WORK_TEXT, c_work_text);
	DDX_Control(pDX, IDC_WORK, c_work);
	DDX_Control(pDX, IDC_END_DATES_TEXT, c_end_dates_text);
	DDX_Control(pDX, IDC_END_DATES, c_end_dates);
	DDX_Control(pDX, IDC_NETWORK_TEXT, c_network_text);
	DDX_Control(pDX, IDC_NETWORK, c_network);
	DDX_Text(pDX, IDC_P, m_iter);
	DDV_MinMaxUInt(pDX, m_iter, 1, 999999999);
	DDX_Text(pDX, IDC_DISK, m_disk_write_time);
	DDV_MinMaxUInt(pDX, m_disk_write_time, 10, 999999);
	DDX_Text(pDX, IDC_BACKUP, m_backup);
	DDV_MinMaxUInt(pDX, m_backup, 1, 3);
	DDX_Check(pDX, IDC_NOISE, m_noise);
	DDX_Text(pDX, IDC_NETWORK, m_retry);
	DDV_MinMaxUInt(pDX, m_retry, 1, 300);
	DDX_Text(pDX, IDC_R_ITER, m_r_iter);
	DDV_MinMaxUInt(pDX, m_r_iter, 10000, 999999999);
	DDX_Text(pDX, IDC_WORK, m_work);
	DDV_MinMaxUInt(pDX, m_work, 0, 90);
	DDX_Text(pDX, IDC_END_DATES, m_end_dates);
	DDV_MinMaxFloat(pDX, m_end_dates, 0.125, 7.0);
	DDX_Text(pDX, IDC_MODEM, m_modem);
	DDV_MinMaxUInt(pDX, m_modem, 1, 300);
	DDX_Check(pDX, IDC_BATTERY, m_battery);
	//}}AFX_DATA_MAP
	c_modem_text.EnableWindow (USE_PRIMENET && DIAL_UP);
	c_modem.EnableWindow (USE_PRIMENET && DIAL_UP);
	c_network_text.EnableWindow (USE_PRIMENET);
	c_network.EnableWindow (USE_PRIMENET);
	c_work_text.EnableWindow (USE_PRIMENET);
	c_work.EnableWindow (USE_PRIMENET);
	c_end_dates_text.EnableWindow (USE_PRIMENET);
	c_end_dates.EnableWindow (USE_PRIMENET);
}


BEGIN_MESSAGE_MAP(CPreferencesDlg, CDialog)
	//{{AFX_MSG_MAP(CPreferencesDlg)
		// NOTE: the ClassWizard will add message map macros here
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CPreferencesDlg message handlers
