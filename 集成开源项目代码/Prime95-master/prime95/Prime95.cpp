/* Copyright 1995-2023 Mersenne Research, Inc.  All rights reserved */

// Prime95.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "Prime95.h"

#include "MainFrm.h"
#include "ChildFrm.h"
#include "Prime95Doc.h"
#include "Prime95View.h"
#include "hyperlink.h"

#include <aclapi.h>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// We need a global mutex handle (could be a member of WinApp)
HANDLE	g_hMutexInst = NULL;
LONG	g_MutexNum = 0;
BOOL CALLBACK MyEnumProc (
	HWND	hwnd,	// handle to parent window
	LPARAM	lParam)	// application-defined value
{

// Return true to continue enumerating Windows

	if (GetWindowLongPtr (hwnd, GWLP_USERDATA) != g_MutexNum)
		return (TRUE);

// We've found the instance, return the window handle

	* (HWND *) lParam = hwnd;
	return (FALSE);
}

/////////////////////////////////////////////////////////////////////////////
// CPrime95App

BEGIN_MESSAGE_MAP(CPrime95App, CWinApp)
	//{{AFX_MSG_MAP(CPrime95App)
	ON_COMMAND(ID_APP_ABOUT, OnAppAbout)
		// NOTE - the ClassWizard will add and remove mapping macros here.
		//    DO NOT EDIT what you see in these blocks of generated code!
	//}}AFX_MSG_MAP
	// Standard file based document commands
	ON_COMMAND(ID_FILE_NEW, CWinApp::OnFileNew)
	ON_COMMAND(ID_FILE_OPEN, CWinApp::OnFileOpen)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CPrime95App construction

CPrime95App::CPrime95App()
{
	EnableHtmlHelp ();

	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}

/////////////////////////////////////////////////////////////////////////////
// The one and only CPrime95App object

CPrime95App theApp;

/////////////////////////////////////////////////////////////////////////////
// CPrime95App initialization

BOOL CPrime95App::InitInstance()
{
	int	orig_cmdShow;
	int	named_ini_files = -1;
	int	torture_test = 0;
	char	*p;

	// VS2019 standard initialization
	
	// InitCommonControlsEx() is required on Windows XP if an application
	// manifest specifies use of ComCtl32.dll version 6 or later to enable
	// visual styles.  Otherwise, any window creation will fail.
	INITCOMMONCONTROLSEX InitCtrls;
	InitCtrls.dwSize = sizeof(InitCtrls);
	// Set this to include all the common control classes you want to use
	// in your application.
	InitCtrls.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&InitCtrls);

	CWinApp::InitInstance ();

	// Standard initialization from MSVC 2005
	// If you are not using these features and wish to reduce the size
	//  of your final executable, you should remove from the following
	//  the specific initialization routines you do not need.

	//SetRegistryKey(_T("GIMPS"));
	//LoadStdProfileSettings(0);  // Load standard INI file options (including MRU)

	// Register the application's document templates.  Document templates
	//  serve as the connection between documents, frame windows and views.

	CMultiDocTemplate* pDocTemplate;
	pDocTemplate = new CMultiDocTemplate(
		IDR_MDITYPE,
		RUNTIME_CLASS(CPrime95Doc),
		RUNTIME_CLASS(CChildFrame),       // custom MDI child frame
		RUNTIME_CLASS(CPrime95View));
	if (!pDocTemplate)
		return FALSE;
	AddDocTemplate(pDocTemplate);

	// Init our view mutexes
	gwmutex_init (&VIEW_MUTEX);
	gwmutex_init (&VIEW_LINES_MUTEX);

	// create main MDI Frame window
	CMainFrame* pMainFrame = new CMainFrame;
	if (!pMainFrame || !pMainFrame->LoadFrame(IDR_MAINFRAME))
	{
		delete pMainFrame;
		return FALSE;
	}
	m_pMainWnd = pMainFrame;

	// Parse command line for standard shell commands, DDE, file open
	CCommandLineInfo cmdInfo;
	ParseCommandLine(cmdInfo);

	// Dispatch commands specified on the command line
	orig_cmdShow = m_nCmdShow;
	if (!ProcessShellCommand(cmdInfo))
		return FALSE;

/* Change the working directory to the same directory that the executable is located. */

	{
		char	buf[256];
		GetModuleFileName (NULL, buf, sizeof (buf));
		strrchr (buf, '\\')[1] = 0;
		_chdir (buf);
	}

/* Initialize gwnum call back routines.  Using callback routines lets the */
/* gwnum library have a nice clean interface for users that do not need */
/* additional functionality that only prime95 uses. */

	StopCheckRoutine = stopCheck;
	OutputBothRoutine = OutputBoth;

// Process command line switches

	for (p = m_lpCmdLine; *p == '//' || *p == '-'; ) {
		p++;
		switch (*p++) {

// Accept a -A switch indicating an alternate set of INI files
// are to be used.

		case 'A':
		case 'a':
			named_ini_files = 0;
			while (isspace (*p)) p++;
			while (isdigit (*p)) {
				named_ini_files = named_ini_files * 10 + (*p - '0');
				p++;
			}
			break;

// Accept a -T switch to run the torture test.

		case 'T':
		case 't':
			torture_test = 1;
			break;

// Accept a -W switch indicating an alternate working directory.

		case 'W':
		case 'w':
			{
			char	buf[256];
			char	*bufp = buf;
			while (isspace (*p)) p++;
			while (*p && !isspace (*p)) *bufp++ = *p++;
			*bufp = 0;
			_chdir (buf);
			}
			break;
		}

// Skip whitespace between switches

		while (isspace (*p)) p++;
	}

// Make sure only one copy of prime95 is running at a time.
// This code is courtesy of Jeroen C. van Gelderen
// I enhanced it to allow multiple copies if they are running
// from different directories or use different -A switches.

	{
		char	buf[256];
		char	*p;
		DWORD mutex_error_code;
		PSID pEveryoneSID = NULL, pAdminSID = NULL;
		PACL pACL = NULL;
		PSECURITY_DESCRIPTOR pSD = NULL;
		EXPLICIT_ACCESS ea[1];
		SID_IDENTIFIER_AUTHORITY SIDAuthWorld = SECURITY_WORLD_SID_AUTHORITY;
		SECURITY_ATTRIBUTES sa;

// Turn directory name into a (likely) unique integer
// Add in the -A value.  Use this integer to create a mutex name.

		_getcwd (buf, 255);
		for (p = buf; *p; p++)
			g_MutexNum = g_MutexNum * 17 + *p;
		g_MutexNum += named_ini_files;
		sprintf (buf, "Global\\GIMPS%ld", g_MutexNum);

/* Create a world access security descriptor to share the Mutex we */
/* are about to create.  If we run into any troubles, assume this is */
/* a Windows 95/98/Me system and create a simple Mutex. */
		
// Create a well-known SID for the Everyone group.

		if (! AllocateAndInitializeSid (
				&SIDAuthWorld, 1, SECURITY_WORLD_RID,
				0, 0, 0, 0, 0, 0, 0, &pEveryoneSID))
			goto simple_mutex;

// Initialize an EXPLICIT_ACCESS structure for an ACE.
// The ACE will allow the Administrators group full access to the key.

		ZeroMemory (&ea, sizeof (EXPLICIT_ACCESS));
		ea[0].grfAccessPermissions = EVENT_ALL_ACCESS;
		ea[0].grfAccessMode = SET_ACCESS;
		ea[0].grfInheritance= NO_INHERITANCE;
		ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
		ea[0].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
		ea[0].Trustee.ptstrName  = (LPTSTR) pEveryoneSID;

// Create a new ACL that contains the new ACEs.

		if (SetEntriesInAcl (1, ea, NULL, &pACL) != ERROR_SUCCESS)
			goto simple_mutex;

// Initialize a security descriptor.

		pSD = (PSECURITY_DESCRIPTOR)
			LocalAlloc (LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
		if (pSD == NULL) goto simple_mutex;
		if (! InitializeSecurityDescriptor (pSD, SECURITY_DESCRIPTOR_REVISION))
			goto simple_mutex;

// Add the ACL to the security descriptor.

		if (! SetSecurityDescriptorDacl (pSD, TRUE, pACL, FALSE))
			goto simple_mutex;

// Initialize a security attributes structure.

		sa.nLength = sizeof (SECURITY_ATTRIBUTES);
		sa.lpSecurityDescriptor = pSD;
		sa.bInheritHandle = FALSE;

// Create our mutex.  Windows XP uses terminal services to support user
// switching.  The "Global\" prefix is required so that this mutex is
// is created in the global kernel objects namespace.  Unfortunately,
// the "\" character raises an error on Windows 95/98/Me systems.

		g_hMutexInst = CreateMutex (
			&sa,   // World access
			FALSE, // Not owned !!
			buf);  // Unique name
		if (g_hMutexInst == NULL)
simple_mutex:	 	g_hMutexInst = CreateMutex (
				NULL,  // No security stuff
				FALSE, // Not owned !!
				buf+7);  // Unique name
		mutex_error_code = GetLastError ();

// Cleanup all the security structures we initialized

		if (pEveryoneSID) FreeSid (pEveryoneSID);
		if (pACL) LocalFree (pACL);
	        if (pSD) LocalFree (pSD);

// Test for failure

		if (g_hMutexInst == NULL)
			return 0;

// If mutex already exists then another instance is already running

		if (mutex_error_code == ERROR_ALREADY_EXISTS) {
			HWND	hwndPrevInst = 0;

// Give other instance a little time to display it's main window

			Sleep (750);

// Find the window handle

			EnumWindows (&MyEnumProc, (LPARAM) &hwndPrevInst);

// Unhide the other instance's window

			if (hwndPrevInst) {
				ShowWindow (hwndPrevInst, SW_HIDE);
				ShowWindow (hwndPrevInst, SW_SHOWMINIMIZED);
				ShowWindow (hwndPrevInst, SW_SHOWNORMAL);
			}
			CloseHandle (g_hMutexInst);
			g_hMutexInst = NULL;
			return 0;
		}

// Set the window user data so we can be identified by
// another instance of this program.

		SetWindowLongPtr (m_pMainWnd->m_hWnd, GWLP_USERDATA, (LONG_PTR) g_MutexNum);
	}

/* Cache icon handles to work around Windows deadlocking bug */

	ICON_IDLE = LoadIcon (IDI_YELLOW_ICON);
	ICON_WORKING = LoadIcon (IDR_MAINFRAME);

/* Name and read the INI files.  Perform some other startup initializations. */

	nameAndReadIniFiles (named_ini_files);
	initCommCode ();

/* Before processing the INI file, hide and/or position the */
/* main window so that we can display error messages */

	m_pMainWnd->SetWindowText ("Prime95");

	WINDOWPLACEMENT wp;
	m_pMainWnd->GetWindowPlacement (&wp);
	int left = IniSectionGetInt (INI_FILE, SEC_Windows, KEY_Left, 0);
	int top = IniSectionGetInt (INI_FILE, SEC_Windows, KEY_Top, 0);
	int right = IniSectionGetInt (INI_FILE, SEC_Windows, KEY_Right, 0);
	int bottom = IniSectionGetInt (INI_FILE, SEC_Windows, KEY_Bottom, 0);
	if (right + left + top + bottom != 0) {
		wp.rcNormalPosition.left = left;
		wp.rcNormalPosition.top = top;
		wp.rcNormalPosition.right = right;
		wp.rcNormalPosition.bottom = bottom;
	}
	wp.showCmd = HIDE_ICON ? SW_HIDE : SW_SHOWMINIMIZED;
	m_pMainWnd->SetWindowPlacement (&wp);

/* Now show the main window and post initial messages */

	// Put prime95 in the system tray
	if (TRAY_ICON) TrayMessage (NIM_ADD, "Prime95", 0);

	// Run the torture test if asked to by a command line argument
	if (torture_test) {
		m_pMainWnd->ShowWindow (orig_cmdShow);
		m_pMainWnd->PostMessage (WM_COMMAND, USR_TORTURE, 0);
	}

	// On first run, see if this is a stress tester.  If not, step
	// user throught the primenet dialog boxes.
	else if (STRESS_TESTER == 99) {
		m_pMainWnd->ShowWindow (orig_cmdShow);
		m_pMainWnd->PostMessage (WM_COMMAND, USR_WELCOME, 0);
	}

	// Take stress testers straight to the torture dialog box
	else if (STRESS_TESTER) {
		m_pMainWnd->ShowWindow (orig_cmdShow);
		m_pMainWnd->PostMessage (WM_COMMAND, IDM_TORTURE, 0);
	}

	// Auto-continue if there is any work to do.
	else if (USE_PRIMENET || WELL_BEHAVED_WORK || WORKTODO_COUNT) {
		m_pMainWnd->PostMessage (WM_COMMAND, IDM_CONTINUE, 0);
	}

	// Otherwise, show the window
	else if (!HIDE_ICON) {
		m_pMainWnd->ShowWindow (orig_cmdShow);
		ChangeIcon (MAIN_THREAD_NUM, IDLE_ICON);
	}

	// Initialization complete
	return TRUE;
}

// In ExitInstance clean up previously allocated mutex
int CPrime95App::ExitInstance() 
{
	if( NULL != g_hMutexInst ) {
		CloseHandle( g_hMutexInst );
	}
	
	return CWinApp::ExitInstance();
}

void CPrime95App::TrayMessage (UINT message, LPCSTR prompt, HICON icon)
{
	NOTIFYICONDATA tnd;

	switch (message) {
	case NIM_ADD :
		WINDOWS95_TRAY_ADD = 0;
		tnd.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
		break;
	case NIM_MODIFY :
		tnd.uFlags = 0;
		if (prompt != NULL) tnd.uFlags |= NIF_TIP;
		if (icon) tnd.uFlags |= NIF_ICON;
		break;
	case NIM_DELETE :
		tnd.uFlags = 0;
	}

	if (prompt)
		truncated_strcpy (tnd.szTip, sizeof (tnd.szTip), prompt);
	else
		lstrcpy (tnd.szTip, "Prime95");

	tnd.uID = 352;
	tnd.cbSize = sizeof(tnd);
	tnd.hWnd = m_pMainWnd->m_hWnd;
	tnd.uCallbackMessage = MYWM_TRAYMESSAGE;
	tnd.hIcon = icon ? icon : ICON_IDLE;

	if (Shell_NotifyIcon(message, &tnd)) return;

// The shell command failed.  This could be due to running
// as a Windows 95 service.  Set a flag to try and add the
// icon at a later time

	if (message == NIM_ADD)
		WINDOWS95_TRAY_ADD = 1;
}

/////////////////////////////////////////////////////////////////////////////
// CAboutDlg dialog used for App About

class CAboutDlg : public CDialog
{
public:
	CAboutDlg();

// Dialog Data
	//{{AFX_DATA(CAboutDlg)
	enum { IDD = IDD_ABOUTBOX };
	CHyperLink m_web_site_link;
	CString m_app_string;
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAboutDlg)
	protected:
	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	//{{AFX_MSG(CAboutDlg)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
public:
    afx_msg void OnStnClickedLink();
};

CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
	//{{AFX_DATA_INIT(CAboutDlg)
	//}}AFX_DATA_INIT
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAboutDlg)
	DDX_Control(pDX, IDC_LINK, m_web_site_link);
	DDX_Text(pDX, IDC_APP_STRING, m_app_string);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
	//{{AFX_MSG_MAP(CAboutDlg)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

BOOL CAboutDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	m_web_site_link.SetURL(_T("http://mersenne.org/"));
	// m_web_site_link.SetUnderline(CHyperLink::ulAlways);

	return TRUE;  // return TRUE  unless you set the focus to a control
}

// App command to run the dialog
void CPrime95App::OnAppAbout()
{
	CAboutDlg aboutDlg;
	char	app_string[120];

	generate_application_string (app_string);
	aboutDlg.m_app_string = app_string;
	aboutDlg.DoModal();
}


/////////////////////////////////////////////////////////////////////////////
// CPrime95App commands


/////////////////////////////////////////////////////////////////////////////
// My application-wide stuff went here
/////////////////////////////////////////////////////////////////////////////

int	EXIT_IN_PROGRESS = 0;

LONG	WM_ENDSESSION_LPARAM = 0;
int	WINDOWS95_TRAY_ADD = 0;

// We cache these handles because frigging Win64 deadlocks if you load an icon
// in the main thread while the Curl library launches a thread to communicate
// with the server (C runtime calls GetModuleHandle ("KERNEL32.DLL"))

HICON	ICON_IDLE = 0;			// Red = program stopped
HICON	ICON_WORKING = 0;		// Green = threads working

void CAboutDlg::OnStnClickedLink()
{
    // TODO: Add your control notification handler code here
}
