// Prime95.h : main header file for the PRIME95 application
// 
//  Copyright 1995-2021 Mersenne Research, Inc.  All rights reserved.
//

#ifdef _WIN64
#define X86_64
#endif

#ifndef __AFXWIN_H__
	#error include 'stdafx.h' before including this file for PCH
#endif

#include "resource.h"		// main symbols
#include "hwloc.h"		// hwloc library
#include "gmp.h"		// GMP library
//#define SERVER_TESTING
#include "common.h"
#include "cpuid.h"
#include "gwnum.h"
#include "gwbench.h"
#include "gwini.h"
#include "gwutil.h"
#include "commona.h"
#include "commonb.h"
#include "commonc.h"
#include "comm95b.h"
#include "comm95c.h"
#include "primenet.h"

/////////////////////////////////////////////////////////////////////////////
// CPrime95App:
// See Prime95.cpp for the implementation of this class
//

class CPrime95App : public CWinApp
{
public:
	CPrime95App();
	void TrayMessage (UINT, LPCSTR, HICON);

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CPrime95App)
	public:
	virtual BOOL InitInstance();
	virtual int ExitInstance();
	//}}AFX_VIRTUAL

// Implementation

	//{{AFX_MSG(CPrime95App
	afx_msg void OnAppAbout();
		// NOTE - the ClassWizard will add and remove member functions here.
		//    DO NOT EDIT what you see in these blocks of generated code !
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};


/////////////////////////////////////////////////////////////////////////////
// My non-MFC stuff went here
/////////////////////////////////////////////////////////////////////////////

// Global variables

extern HANDLE g_hMutexInst;

extern HICON ICON_IDLE;			// Red = program stopped
extern HICON ICON_WORKING;		// Green = threads working

extern int EXIT_IN_PROGRESS;		// True if we are exiting

extern LONG WM_ENDSESSION_LPARAM;	// LPARAM of WM_ENDSESSION message
extern int WINDOWS95_TRAY_ADD;		// True if we need to add the icon
					// to the shell once the user logs in

extern gwmutex VIEW_MUTEX;		/* Lock for accessing Views Array */
extern gwmutex VIEW_LINES_MUTEX;	/* Lock for accessing Lines array */

// Internal routines

UINT threadDispatch (LPVOID);
int canModifyServices ();
void Service95 ();
