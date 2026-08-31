// MainFrm.cpp : implementation of the CMainFrame class
// Copyright 1995-2023 Mersenne Research, Inc.  All rights reserved
//

#include "stdafx.h"
#include "Prime95.h"

#include "MainFrm.h"
#include "Prime95Doc.h"
#include "Prime95View.h"

#include <winreg.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

const UINT WM_TASKBARCREATED = ::RegisterWindowMessage(_T("TaskbarCreated"));

/////////////////////////////////////////////////////////////////////////////
// CMainFrame

IMPLEMENT_DYNAMIC(CMainFrame, CMDIFrameWnd)

BEGIN_MESSAGE_MAP(CMainFrame, CMDIFrameWnd)
	//{{AFX_MSG_MAP(CMainFrame)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_ENDSESSION()
	ON_WM_ACTIVATEAPP()
	//}}AFX_MSG_MAP
	ON_WM_SYSCOMMAND()
	// Global help commands
	ON_COMMAND(ID_HELP_FINDER, CMDIFrameWnd::OnHelpFinder)
	ON_COMMAND(ID_HELP, CMDIFrameWnd::OnHelp)
	ON_COMMAND(ID_CONTEXT_HELP, CMDIFrameWnd::OnContextHelp)
	ON_COMMAND(ID_DEFAULT_HELP, CMDIFrameWnd::OnHelpFinder)
	ON_COMMAND(IDM_TRAY_OPEN, OnTrayOpenWindow)
	ON_COMMAND(IDM_STOP_CONTINUE, OnStopContinue)
	ON_COMMAND(ID_WINDOW_TILE_HORZ, OnTile)
	ON_COMMAND(ID_WINDOW_POSITION, OnPosition)
	ON_MESSAGE(WM_POWERBROADCAST, OnPower)
	ON_MESSAGE(MYWM_TRAYMESSAGE, OnTrayMessage)
	ON_REGISTERED_MESSAGE(WM_TASKBARCREATED, OnTaskBarCreated)
END_MESSAGE_MAP()

static UINT indicators[] =
{
	ID_SEPARATOR,           // status line indicator
	ID_INDICATOR_CAPS,
	ID_INDICATOR_NUM,
	ID_INDICATOR_SCRL,
};

/////////////////////////////////////////////////////////////////////////////
// CMainFrame construction/destruction

CMainFrame::CMainFrame()
{
	// TODO: add member initialization code here
}

CMainFrame::~CMainFrame()
{
}

BOOL CMainFrame::PreCreateWindow(CREATESTRUCT& cs)
{
	// TODO: Modify the Window class or styles here by modifying
	//  the CREATESTRUCT cs

//	cs.style &= ~FWS_ADDTOTITLE;	// We'll control window titles!
	return CMDIFrameWnd::PreCreateWindow(cs);
}

void CMainFrame::OnUpdateFrameTitle(BOOL bAddToTitle)
{
}

int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CMDIFrameWnd::OnCreate(lpCreateStruct) == -1)
		return -1;

	if (!m_wndStatusBar.Create(this) ||
		!m_wndStatusBar.SetIndicators(indicators,
		  sizeof(indicators)/sizeof(UINT)))
	{
		TRACE0("Failed to create status bar\n");
		return -1;      // fail to create
	}

	return 0;
}

/////////////////////////////////////////////////////////////////////////////
// CMainFrame diagnostics

#ifdef _DEBUG
void CMainFrame::AssertValid() const
{
	CMDIFrameWnd::AssertValid();
}

void CMainFrame::Dump(CDumpContext& dc) const
{
	CMDIFrameWnd::Dump(dc);
}

#endif //_DEBUG

/////////////////////////////////////////////////////////////////////////////
// CMainFrame message handlers
LRESULT CMainFrame::OnPower(WPARAM uID, LPARAM uMouseMsg)
{
// We used to check battery status only after we got this message.  However,
// on my laptop prime95 sometimes would not restart when AC power resumed.
// Now we'll test using a timer as well as this Windows message (so we don't
// have a delay in shutting threads down when we go on battery)
	if (uID == PBT_APMPOWERSTATUSCHANGE) {
		if (is_timed_event_active (TE_BATTERY_CHECK)) test_battery ();
	}
	return 0;
}

LRESULT CMainFrame::OnTrayMessage(WPARAM uID, LPARAM uMouseMsg)
{
	if (uID == 352 && uMouseMsg == WM_LBUTTONDBLCLK) {
		if (IsWindowVisible())
			ShowWindow(FALSE);	// hide it
		else {
			ShowWindow(TRUE);	// show it
			SetForegroundWindow();
		}
	}

	if (uID == 352 && uMouseMsg == WM_RBUTTONUP) {
		POINT	point;
		HMENU	hMenu, hSubMenu;
		MENUITEMINFO m;

		GetCursorPos (&point);		// Get cursor location
		hMenu = LoadMenu (AfxGetApp()->m_hInstance,
				  MAKEINTRESOURCE (IDR_TRAYMENU));
		hSubMenu = GetSubMenu (hMenu, 0);
		m.cbSize = sizeof (m);
		if (!IsIconic ()) {
			m.fMask = MIIM_STRING;
			m.fType = MFT_STRING;
			m.dwTypeData = "Minimize Window";
			m.cch = (UINT) strlen (m.dwTypeData);
			SetMenuItemInfo (hSubMenu, IDM_TRAY_OPEN, FALSE, &m);
		}
		if (!WORKERS_ACTIVE) {
			m.fMask = MIIM_STRING;
			m.fType = MFT_STRING;
			m.dwTypeData = "Continue";
			m.cch = (UINT) strlen (m.dwTypeData);
			SetMenuItemInfo (hSubMenu, IDM_STOP_CONTINUE, FALSE, &m);
		}
		if ((WORKERS_ACTIVE && WORKERS_STOPPING) ||
		    (!WORKERS_ACTIVE &&
		     !USE_PRIMENET && WORKTODO_COUNT == 0)) {
			m.fMask = MIIM_STATE;
			m.fState = MFS_DISABLED;
			SetMenuItemInfo (hSubMenu, IDM_STOP_CONTINUE, FALSE, &m);
		}
		SetMenuDefaultItem (hSubMenu, IDM_TRAY_OPEN, FALSE);
		SetForegroundWindow ();	// Per KB Article Q135788
		TrackPopupMenu (hSubMenu,
			TPM_LEFTBUTTON | TPM_RIGHTBUTTON | TPM_LEFTALIGN,
			point.x, point.y, 0, m_hWnd, NULL);
		PostMessage (WM_NULL, 0, 0);// Per KB Article Q135788
		DestroyMenu (hMenu);
	}

	return 0;
}

LRESULT CMainFrame::OnTaskBarCreated (WPARAM wp, LPARAM lp)
{
	if (!HIDE_ICON)
		((CPrime95App *)AfxGetApp())->TrayMessage (NIM_ADD, "Prime95", 0);
	return 0;
}

void CMainFrame::OnTrayOpenWindow()
{
	if (IsIconic ())
		PostMessage (WM_SYSCOMMAND, SC_RESTORE, 0L);
	else
		PostMessage (WM_SYSCOMMAND, SC_MINIMIZE, 0L);
}

void CMainFrame::OnStopContinue()
{
	if (WORKERS_ACTIVE)
		PostMessage (WM_COMMAND, IDM_STOP, 0);
	else
		PostMessage (WM_COMMAND, IDM_CONTINUE, 0);
}

BOOL CMainFrame::DestroyWindow() 
{
	if (TRAY_ICON) ((CPrime95App *)AfxGetApp())->TrayMessage (NIM_DELETE, NULL, 0);
	return CMDIFrameWnd::DestroyWindow();
}

void CMainFrame::OnSize(UINT nType, int cx, int cy) 
{
static	int first_sizing = 0;

	if (nType == SIZE_MINIMIZED) {
		if (TRAY_ICON) ShowWindow (FALSE);// hide it
		if (HIDE_ICON) ShowWindow (FALSE);
	}
	CMDIFrameWnd::OnSize(nType, cx, cy);

// On Windows XP, we need to tile on the second message, while on 64-bit
// windows tiling on the first size message works.  Go figure.

	if (nType != SIZE_MINIMIZED && first_sizing < 2) {
		first_sizing++;
		PostMessage (WM_COMMAND, ID_WINDOW_POSITION, 0);
	}
}

LRESULT CMainFrame::WindowProc (UINT message, WPARAM wParam, LPARAM lParam)
{
	WM_ENDSESSION_LPARAM = (LONG) lParam;
	return CMDIFrameWnd::WindowProc(message, wParam, lParam);
}


// Special WM_ENDSESSION code from knowledge base article Q164166

static const TCHAR szAfxOldWndProc[] = _T("AfxOldWndProc423");
BOOL CALLBACK EnumProc (HWND hWnd, LPARAM lParam)
{
	//check for property and unsubclass if necessary
	WNDPROC oldWndProc = (WNDPROC)::GetProp (hWnd, szAfxOldWndProc);
	if (oldWndProc!=NULL) {
		SetWindowLongPtr (hWnd, GWLP_WNDPROC, (LONG_PTR) oldWndProc);
		RemoveProp (hWnd, szAfxOldWndProc);
	}
	return TRUE;
}

void CMainFrame::OnEndSession (BOOL bEnding)
{
	CMDIFrameWnd::OnEndSession (bEnding);
}

void CMainFrame::OnActivateApp(BOOL bActive, DWORD hTask) 
{
	CMDIFrameWnd::OnActivateApp(bActive, hTask);
}

// Override the Upper Right X to do a minimize instead of a close.
// This has become common practice for tray applications.

void CMainFrame::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == SC_CLOSE && ! IniSectionGetInt (INI_FILE, SEC_Windows, KEY_ExitOnX, 0) && (TRAY_ICON || HIDE_ICON)) {
		SendMessage (WM_SYSCOMMAND, SC_MINIMIZE, 0);
		return;
	}
	CMDIFrameWnd::OnSysCommand (nID, lParam);
}

// Override the MFC implementation of tile MDI windows horizontally.

void CMainFrame::OnTile()
{

// Call default implementation to de-maximize windows if necessary

	MDITile (MDITILE_HORIZONTAL);

// Now call our implementation of horizontal tiling

	PositionViews (TRUE);
}

void CMainFrame::OnPosition()
{

// Call default implementation to de-maximize windows if necessary

	MDITile (MDITILE_HORIZONTAL);

// Now call our implementation of horizontal tiling

	PositionViews (FALSE);
}

