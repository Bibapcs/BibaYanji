/* Copyright 1995-2023 Mersenne Research, Inc.  All rights reserved */

// Prime95View.cpp : implementation of the CPrime95View class
//

#include "stdafx.h"
#include "Prime95.h"

#include "afxpriv.h"
#include "ChildFrm.h"
#include "MainFrm.h"
#include "Prime95Doc.h"
#include "Prime95View.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#define MAX_VIEWS	(MAX_NUM_WORKERS+2)	/* MDI windows: Main_thread, comm_thread and workers. */


CPrime95View *Views[MAX_VIEWS] = {0};
char	ThreadTitles[MAX_VIEWS][80] = {0};
gwmutex	VIEW_MUTEX;		/* Lock for accessing Views Array */
gwmutex	VIEW_LINES_MUTEX;	/* Lock for accessing Lines array */
int	ViewNum = 2;		/* Create worker window first */
int	NumViews = 0;
int	charHeight = 0;
int	charWidth = 0;

/////////////////////////////////////////////////////////////////////////////
// CPrime95View

IMPLEMENT_DYNCREATE(CPrime95View, CScrollView)

BEGIN_MESSAGE_MAP(CPrime95View, CScrollView)
	//{{AFX_MSG_MAP(CPrime95View)
	ON_COMMAND(USR_SCROLL, OnScroll)
	ON_COMMAND(USR_TITLE, OnTitle)
	ON_COMMAND(USR_ICON, OnIcon)
	ON_COMMAND(ID_EDIT_COPY, OnEditCopy)
		// NOTE - the ClassWizard will add and remove mapping macros here.
		//    DO NOT EDIT what you see in these blocks of generated code!
	//}}AFX_MSG_MAP
	ON_WM_DESTROY()
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CPrime95View construction/destruction

CPrime95View::CPrime95View()
{
	int	i;

	BaseTitle[0] = 0;
	Title[0] = 0;
	for (i = 0; i < MAX_VIEW_LINES; i++) Lines[i] = LineData[i];
	NumLines = 0;
	MaxLineSize = 0;
	Lines[0][0] = 0;

	Views[ViewNum] = this;
	NumViews++;

	SetScrollSizes (MM_TEXT, CSize (0, 0));
}

CPrime95View::~CPrime95View()
{
	NumViews--;
}

BOOL CPrime95View::PreCreateWindow(CREATESTRUCT& cs)
{
	// TODO: Modify the Window class or styles here by modifying
	//  the CREATESTRUCT cs

	cs.style &= ~FWS_ADDTOTITLE;	// We'll control window titles!
	return CScrollView::PreCreateWindow(cs);
}

/////////////////////////////////////////////////////////////////////////////
// CPrime95View drawing

void CPrime95View::OnUpdate(CView* pSender, LPARAM lHint, CObject* pHint)
{
	CFrameWnd *parent;
	CSize	sz;
	CPoint	pos;
	int	new_scroll_height, new_scroll_width;

	parent = GetParentFrame();

	if (parent != NULL) {

		if (charHeight == 0) getCharSize ();

		sz = GetTotalSize ();

		new_scroll_height = NumLines * charHeight;
		new_scroll_width = MaxLineSize * charWidth;

		pos = GetScrollPosition ();
		pos.y += (new_scroll_height - sz.cy);
		if (pos.y < 0) pos.y = 0;
		sz.cx = new_scroll_width;
		sz.cy = new_scroll_height;
		SetScrollSizes (MM_TEXT, sz);
		ScrollToPosition (pos);
		parent->RecalcLayout ();
	}

	CScrollView::OnUpdate (pSender, lHint, pHint);
}

void CPrime95View::OnDraw(CDC* pDC)
{
	RECT	r;
	CPoint	scroll_offset;
	int	ypos;
	int	first_line, skip_lines, i, j;

	pDC->SetBkMode (TRANSPARENT);
	pDC->SetTextColor (GetSysColor (COLOR_WINDOWTEXT));

	GetClientRect (&r);
	scroll_offset = GetScrollPosition ();

/* If Lines[0] is empty, then output lines NumLines to 1 */
/* If Lines[0] has text, then output lines NumLines-1 to 0 */

	gwmutex_lock (&VIEW_LINES_MUTEX);
	first_line = Lines[0][0] ? NumLines - 1 : NumLines;
	skip_lines = (r.top + scroll_offset.y) / charHeight;
	if (skip_lines < NumLines) {
		i = first_line - skip_lines;
		j = NumLines - skip_lines;
		ypos = skip_lines * charHeight;
		for ( ; j; i--, j--) {
			pDC->TextOut (0, ypos, Lines[i], (int) strlen (Lines[i]));
			ypos += charHeight;
			if (ypos > r.bottom + scroll_offset.y) break;
		}
	}
	gwmutex_unlock (&VIEW_LINES_MUTEX);
}

/////////////////////////////////////////////////////////////////////////////
// CPrime95View diagnostics

#ifdef _DEBUG
void CPrime95View::AssertValid() const
{
//	CScrollView::AssertValid();
}

void CPrime95View::Dump(CDumpContext& dc) const
{
	CScrollView::Dump(dc);
}

CPrime95Doc* CPrime95View::GetDocument() // non-debug version is inline
{
	ASSERT(m_pDocument->IsKindOf(RUNTIME_CLASS(CPrime95Doc)));
	return (CPrime95Doc*)m_pDocument;
}
#endif //_DEBUG

/////////////////////////////////////////////////////////////////////////////
// CPrime95View message handlers

void CPrime95View::OnEditCopy()
{
	int	i, j;

// Create a shared memory file

	CSharedFile sf(GMEM_MOVEABLE | GMEM_SHARE | GMEM_ZEROINIT);

// Place clipboard data in the shared memory file
/* If Lines[0] is empty, then output lines 1 to NumLines */
/* If Lines[0] has text, then output lines 0 to NumLines-1 */

	i = Lines[0][0] ? NumLines - 1 : NumLines;
	for (j = NumLines; j; i--, j--) {
		sf.Write (Lines[i], (UINT) strlen (Lines[i]));
		sf.Write ("\r\n", 2);
	}

// Put data in clipboard

    	OpenClipboard ();
	EmptyClipboard ();
	SetClipboardData (CF_TEXT, sf.Detach ());
	CloseClipboard ();
}

/////////////////////////////////////////////////////////////////////////////
// CPrime95View private routines


/* Internal routine to convert thread number into it's corresponding */
/* view number.  This routine handles merging MDI windows together. */

int map_thread_num_to_view_num (
	int	thread_num)
{

/* Merge main window into first worker window if so requested */

	if (thread_num == MAIN_THREAD_NUM) {
		if (MERGE_WINDOWS & MERGE_MAINCOMM_WINDOWS) return (1);
		if (MERGE_WINDOWS & MERGE_MAIN_WINDOW) return (2);
	}

/* Merge comm window into first worker window if so requested */

	if (thread_num == COMM_THREAD_NUM && MERGE_WINDOWS & MERGE_COMM_WINDOW)
		return (2);

/* Merge all worker windows into first worker window if so requested */

	if (thread_num >= 0 && MERGE_WINDOWS & MERGE_WORKER_WINDOWS)
		return (2);

/* No merging.  Add 2 to map thread numbers (-2...32) into */
/* view numbers (0..34) */

	return (thread_num+2);
}

/* Internal routine to convert thread number into it's corresponding */
/* CPrime95View */

CPrime95View *map_thread_num_to_view (
	int	thread_num)
{
	return (Views[map_thread_num_to_view_num (thread_num)]);
}

/* Create an MDI output window for the thread -- unless we created */
/* one earlier and the user has not closed it */

void create_window (
	int	thread_num)
{
	gwmutex_lock (&VIEW_MUTEX);
	ViewNum = map_thread_num_to_view_num (thread_num);
	if (Views[ViewNum] == NULL) {
		AfxGetApp()->m_pMainWnd->SendMessage (WM_COMMAND, ID_WINDOW_NEW, 0);
		PositionViews (FALSE);
	}
	gwmutex_unlock (&VIEW_MUTEX);
}

/* Destroy an MDI output window for the thread */

void destroy_window (
	int	thread_num)
{
	CPrime95View *view;

	gwmutex_lock (&VIEW_MUTEX);
	view = map_thread_num_to_view (thread_num);
	if (view != NULL) view->GetParentFrame()->DestroyWindow ();
	gwmutex_unlock (&VIEW_MUTEX);
}

// We post a message to change a window title because Windows is much
// happier (strange hangs are avoided) when the title is updated by
// the main thread.

void CPrime95View::OnTitle ()
{
	char	buf[120];
	CPrime95App* pApp = (CPrime95App *) AfxGetApp();
static	int	was_iconic = TRUE;

// Ignore changes while exiting

	if (EXIT_IN_PROGRESS) return;

// Create the full window title

	if (BaseTitle[0] && Title[0])
		sprintf (buf, "%s - %s", BaseTitle, Title);
	else if (Title[0])
		strcpy (buf, Title);
	else
		strcpy (buf, BaseTitle);

	GetParent()->SetWindowText (buf);

// Set the main window's title too.
// I'm going to try setting it to a worker window's title.  If workers
// aren't running I'll set it to the main or comm window title.

	if (pApp->m_pMainWnd &&
	    (! WORKERS_ACTIVE ||
	     (this != Views[0] && this != Views[1]))) {
		sprintf (buf, "Prime95 - %s", Title);
		if (TRAY_ICON) {
			int	i;
			char	buf[1600];
			buf[0] = 0;
			for (i = 2; i < MAX_VIEWS; i++) {
				if (strlen (buf) > sizeof (buf) - 100) break;	// Quick and dirty check to make sure buf has room
				if (buf[0] && ThreadTitles[i][0]) strcat (buf, "\n");
				strcat (buf, ThreadTitles[i]);
			}
			pApp->TrayMessage (NIM_MODIFY, buf[0] ? buf : "Not running", 0);
		}
		if (pApp->m_pMainWnd->IsIconic ()) {
			pApp->m_pMainWnd->SetWindowText (buf);
			was_iconic = TRUE;
		} else if (was_iconic) {
			pApp->m_pMainWnd->SetWindowText ("Prime95");
			was_iconic = FALSE;
		}
	}

// Call base class routine to refresh screen

	OnUpdate (NULL, 0, NULL);
}

/* Set the title prefix for this MDI window - only called once */

void base_title (
	int	thread_num,
	const char *str)
{
	CPrime95View *view;

	if (EXIT_IN_PROGRESS) return;

/* When merging windows, apply some arbitrary rules to decide which */
/* base title to use. */

	if (MERGE_WINDOWS & MERGE_MAIN_WINDOW &&
	    MERGE_WINDOWS & MERGE_COMM_WINDOW &&
	    (MERGE_WINDOWS & MERGE_WORKER_WINDOWS || NUM_WORKERS == 1))
		str = "";
	else if ((thread_num == MAIN_THREAD_NUM &&
		  MERGE_WINDOWS & MERGE_MAIN_WINDOW) ||
		 (thread_num == COMM_THREAD_NUM &&
		  MERGE_WINDOWS & MERGE_COMM_WINDOW) ||
		 (thread_num >= 0 &&
		  MERGE_WINDOWS & MERGE_WORKER_WINDOWS)) {
		if (MERGE_WINDOWS & MERGE_WORKER_WINDOWS &&
		    NUM_WORKERS > 1)
			str = "Workers";
		else
			str = "Worker";
	}

/* Pass the base title on to the view */

	gwmutex_lock (&VIEW_MUTEX);
	view = map_thread_num_to_view (thread_num);
	if (view != NULL) view->base_title (str);
	gwmutex_unlock (&VIEW_MUTEX);
}

void CPrime95View::base_title (
	const char *str)
{
	strcpy (BaseTitle, str);
	PostMessage (WM_COMMAND, USR_TITLE, 0);
}

/* Put a title on the MDI window */

void title (
	int	thread_num,
	const char *str)
{
	CPrime95View *view;
	char	merged_title[160];

	if (EXIT_IN_PROGRESS) return;

/* When merging windows, apply some arbitrary rules to decide which */
/* what the title should be. */

	if (thread_num == COMM_THREAD_NUM && MERGE_WINDOWS & MERGE_COMM_WINDOW)
		return;

	strcpy (ThreadTitles[thread_num+2], str);
	if (thread_num == MAIN_THREAD_NUM && MERGE_WINDOWS & MERGE_MAIN_WINDOW)
		thread_num = 0;
	if (thread_num >= 0 && MERGE_WINDOWS & MERGE_WORKER_WINDOWS) {
		int	i;
		merged_title[0] = 0;
		for (i = 2; i < MAX_VIEWS; i++) {
			if (merged_title[0] && ThreadTitles[i][0])
				strcat (merged_title, ",");
			strcat (merged_title, ThreadTitles[i]);
			if (strlen (merged_title) > 80) {
				merged_title[77] = 0;
				strcat (merged_title, "...");
				break;
			}
		}
		str = merged_title[0] ? merged_title : "Not running";
	}

/* Pass the title on to the view */

	gwmutex_lock (&VIEW_MUTEX);
	view = map_thread_num_to_view (thread_num);
	if (view != NULL) view->title (str);
	gwmutex_unlock (&VIEW_MUTEX);
}

void CPrime95View::title (
	const char *str)
{
	strcpy (Title, str);
	PostMessage (WM_COMMAND, USR_TITLE, 0);
}

/* Change the Icon for an MDI window */

void ChangeIcon (
	int	thread_num,
	int	icon_id)
{
	if (EXIT_IN_PROGRESS) return;

/* When merging windows, apply some arbitrary rules to decide */
/* what the icon should be. */

	if (thread_num == COMM_THREAD_NUM && MERGE_WINDOWS & MERGE_COMM_WINDOW)
		return;

/* Pass the request along to the view object */

	if (icon_id != -1) {
		CPrime95View *view;
		gwmutex_lock (&VIEW_MUTEX);
		view = map_thread_num_to_view (thread_num);
		if (view != NULL) view->ChangeIcon (icon_id);
		gwmutex_unlock (&VIEW_MUTEX);
	}

/* If this is the main thread, then also change the icon of the application */
/* Handle the special icon_id value of -1, meaning leave the icon unchanged */
/* but set the tray icon.  This happens when we choose the Tray Icon */
/* menu choice. */

	if (thread_num == MAIN_THREAD_NUM) {
		CPrime95App *app;
		static HICON main_icon = 0;
		app = (CPrime95App *) AfxGetApp();
		if (icon_id == WORKING_ICON)
			main_icon = ICON_WORKING;
		else if (icon_id == IDLE_ICON || main_icon == 0)
			main_icon = ICON_IDLE;
		app->m_pMainWnd->SetIcon (main_icon, 1);
		if (TRAY_ICON) app->TrayMessage (NIM_MODIFY, NULL, main_icon);
	}
}


// We post a message to change a window icon because Windows is much
// happier (strange hangs are avoided) when icon title is updated by
// the main thread.

void CPrime95View::OnIcon ()
{

// Ignore changes while exiting

	if (EXIT_IN_PROGRESS) return;

// Load the icon.  Alas, despite all attempts I cannot avoid the deadlock
// that is occurring between _beginthreadex and SetIcon on WinXP 64-bit.
// Thus, icons in MDI windows is disabled

#ifndef X86_64 
	GetParent()->SetIcon (icon, 1);
#endif

// Call base class routine to refresh screen

	OnUpdate (NULL, 0, NULL);
}

// Change a view's icon

void CPrime95View::ChangeIcon (
	int	icon_id)
{
	icon = (icon_id == WORKING_ICON) ? ICON_WORKING : ICON_IDLE;
	PostMessage (WM_COMMAND, USR_ICON, 0);
}

/* Blink the title bar of an MDI window */

void CALLBACK EXPORT TimerCallback (
	HWND	hWnd,		//handle of CWnd that called SetTimer
	UINT	nMsg,		//WM_TIMER
	UINT_PTR nIDEvent,	//timer identification
	DWORD	dwTime)		//system time
{
	BlinkIcon (MAIN_THREAD_NUM, -2);
}

void BlinkIcon (
	int	thread_num,
	int	duration)		/* -2 = change icon (called from timer) */
					/* -1 = blinking off, 0 = indefinite */
{
static	int	state = -1;		/* Current duration state */
static	int	icon = WORKING_ICON;	/* Current icon */

/* If this is the first blinking call, start the timer */

	if (state == -1) {
		if (duration < 0) return;
		AfxGetApp()->m_pMainWnd->SetTimer (363, 1000, &TimerCallback);
	}

/* Remember how long we are to blink */

	if (duration >= 0)
		state = duration;

/* If this is the last blinking call, kill the timer */

	else if (duration == -1 || (state && --state == 0)) {
		AfxGetApp()->m_pMainWnd->KillTimer (363);
		state = -1;
		icon = IDLE_ICON;
	}

/* Toggle the icon */

	icon = (icon == IDLE_ICON) ? WORKING_ICON : IDLE_ICON;
	ChangeIcon (MAIN_THREAD_NUM, icon);
}

/* Restore position or horizontally tile all MDI windows */

void PositionViews (int forceTile)
{
	int	i, vnum;

	gwmutex_lock (&VIEW_MUTEX);
	for (i = vnum = 0; i < MAX_VIEWS; i++)
		if (Views[i] != NULL)
			Views[i]->position (vnum++, i, (BOOL)!!forceTile);
	gwmutex_unlock (&VIEW_MUTEX);
}

static const int P95_WP_MINIMIZED= 1;
static const int P95_WP_MAXIMIZED= 2;

BOOL getSubWindowPlacement(
	CWnd	*pwnd,
	int	vnum)
{
	BOOL	handled = FALSE;
	char	name[16];
	char	rgch[80];

	wsprintf(name, "W%d", vnum);
	rgch[0] = 0;
	IniSectionGetString (INI_FILE, SEC_Windows, name, rgch, sizeof (rgch), NULL);
	if (0 != rgch[0])
	{
		int state = 0;
		WINDOWPLACEMENT wp = {0};
		RECT *prc	= &wp.rcNormalPosition;
		POINT *pptMin= &wp.ptMinPosition;
		POINT *pptMax= &wp.ptMaxPosition;
		int count = sscanf (rgch, "%d %ld %ld %ld %ld %ld %ld %ld %ld",
				    &state, &prc->top, &prc->right, &prc->bottom, &prc->left, &pptMin->x, &pptMin->y, &pptMax->x, &pptMax->y);
		if (9 == count)
		{
			BOOL fMinimized = (0 != (state & P95_WP_MINIMIZED));
			BOOL fMaximized = (0 != (state & P95_WP_MAXIMIZED));

			wp.length = sizeof(wp);
			if (fMinimized)
			{
				wp.showCmd = SW_SHOWMINNOACTIVE;
			}
			else
			{
				wp.showCmd = (fMaximized) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
			}
			wp.flags = (fMaximized) ? WPF_RESTORETOMAXIMIZED : WPF_SETMINPOSITION;
			if (0 != pwnd->SetWindowPlacement(&wp))
			{
				handled = TRUE;
			}
		}
	}

	return handled;
}

BOOL setSubWindowPlacement(
	CWnd	*pwnd,
	int	vnum)
{
	BOOL	handled = FALSE;
	char	name[16];
	char	rgch[80];
	WINDOWPLACEMENT wp = {0};

	wp.length = sizeof(wp);
	if (0 != pwnd->GetWindowPlacement(&wp))
	{
		RECT r = {0};
		RECT *prc	= &wp.rcNormalPosition;
		POINT *pptMin= &wp.ptMinPosition;
		POINT *pptMax= &wp.ptMaxPosition;
		int state = 0;

		handled = TRUE;

		if (wp.showCmd == SW_SHOWMAXIMIZED)
		{
			state = P95_WP_MAXIMIZED;
		}
		else if (wp.showCmd == SW_SHOWMINIMIZED)
		{
			state = P95_WP_MINIMIZED;
		}
		
		wsprintf (rgch, "%d %ld %ld %ld %ld %ld %ld %ld %ld",
			  state, prc->top, prc->right, prc->bottom, prc->left, pptMin->x, pptMin->y, pptMax->x, pptMax->y);

		wsprintf (name, "W%d", vnum);
		IniSectionWriteString (INI_FILE, SEC_Windows, name, rgch);
	}

	return handled;
}

void SaveViews (void)
{
	int	i, vnum;

	gwmutex_lock (&VIEW_MUTEX);
	for (i = vnum = 0; i < MAX_VIEWS; i++)
	{
		if (Views[i] != NULL)
		{
			setSubWindowPlacement(Views[i]->GetParent(), i);
			Views[i] = NULL;
		}
	}
	gwmutex_unlock (&VIEW_MUTEX);
}

/* Restore position of or horizontally-tile an MDI window */

void CPrime95View::position (
	int	vnum,
	int	iview,
	BOOL	forceTile)
{
	CMainFrame *mainframe;
	CRect	frame_rect;
	int	frame_width, frame_height;
	BOOL handled = FALSE;

	if (!forceTile)
	{
		handled = getSubWindowPlacement(GetParent(), iview);
	}
	if (!handled)
	{
		mainframe = (CMainFrame *) AfxGetApp()->m_pMainWnd;
		mainframe->GetClientRect (&frame_rect);
		frame_height = frame_rect.bottom - frame_rect.top;
		frame_width = frame_rect.right - frame_rect.left;

		mainframe->m_wndStatusBar.GetClientRect (&frame_rect);
		frame_height -= frame_rect.bottom - frame_rect.top;

		frame_height -= 2;
		frame_width -= 2;

		GetParent()->MoveWindow (0,
					 vnum * frame_height / NumViews,
					 frame_width,
					 (vnum+1) * frame_height / NumViews -
					  vnum * frame_height / NumViews);
	}
}

void CPrime95View::LineFeed ()
{
	char	*p;
	int	i, len, lines_to_size;

// Decrease the line count if we are scrolling a line off the top.  Also,
// see if we need to test every line to determine a new MaxLineSize.
	
	lines_to_size = 1;
	if (NumLines == MAX_VIEW_LINES) {
		NumLines--;
		if (MaxLineSize == strlen (Lines[NumLines])) {
			MaxLineSize = 0;
			lines_to_size = NumLines;
		}
	}

// Scroll the line in memory

	p = Lines[NumLines];
	memmove (&Lines[1], &Lines[0], NumLines * sizeof(char *));
	Lines[0] = p;
	*p = 0;

// Compute the new max line size

	for (i = 1; i <= lines_to_size; i++) {
		len = (int) strlen (Lines[i]);
		if (len > MaxLineSize) MaxLineSize = len;
	}
}


void CPrime95View::getCharSize ()
{
	if (charHeight == 0) {
		CDC *dc = GetDC ();
		CSize size;
		size = dc->GetTextExtent ("A", 1);
		charHeight = size.cy;
		charWidth = size.cx;
	}
}

void CPrime95View::OnScroll ()
{

// Scroll the line on the screen

	if (charHeight == 0) getCharSize ();
	ScrollWindow (0, -charHeight, NULL, NULL);

// Call base class routine to refresh screen

	OnUpdate (NULL, 0, NULL);
}

void RealOutputStr (
	int	thread_num,
	const char *str)
{
	CPrime95View *view;
static	int	partial_line_output[MAX_VIEWS] = {FALSE};

// Avoid bizarre deadlocks and race conditions by ignoring output while exiting

	if (EXIT_IN_PROGRESS) return;

// Find the view (window) to output to

	gwmutex_lock (&VIEW_MUTEX);
	view = map_thread_num_to_view (thread_num);

// Shouldn't happen, but catch it just in case

	if (view == NULL);

// When merging windows output a prefix so user knows which thread is
// responsible for this line of output

	else if (!(MERGE_WINDOWS & MERGE_NO_PREFIX) &&
		 !partial_line_output[thread_num+2] &&
		 ((thread_num == MAIN_THREAD_NUM &&
		   MERGE_WINDOWS & MERGE_MAIN_WINDOW) ||
		  (thread_num == MAIN_THREAD_NUM &&
		   MERGE_WINDOWS & MERGE_MAINCOMM_WINDOWS) ||
		  (thread_num == COMM_THREAD_NUM &&
		   MERGE_WINDOWS & MERGE_COMM_WINDOW) ||
		  (thread_num == COMM_THREAD_NUM &&
		   MERGE_WINDOWS & MERGE_MAINCOMM_WINDOWS) ||
		  (thread_num == 0 &&
		   MERGE_WINDOWS & (MERGE_MAIN_WINDOW | MERGE_COMM_WINDOW)) ||
		  (thread_num == 0 &&
		   MERGE_WINDOWS & MERGE_WORKER_WINDOWS &&
		   NUM_WORKERS > 1) ||
		  (thread_num >= 1 &&
		   MERGE_WINDOWS & MERGE_WORKER_WINDOWS))) {
		char	prefix[50];
		if (thread_num == MAIN_THREAD_NUM)
			strcpy (prefix, "[Main thread");
		else if (thread_num == COMM_THREAD_NUM)
			strcpy (prefix, "[Comm thread");
		else if (! (MERGE_WINDOWS & MERGE_WORKER_WINDOWS) ||
			 NUM_WORKERS == 1)
			strcpy (prefix, "[Work thread");
		else
			sprintf (prefix, "[Worker #%d", thread_num+1);
		if (str[0] == '[') {
			strcat (prefix, " ");
			str++;
		} else
			strcat (prefix, "] ");
		view->RealOutputStr (prefix);
		view->RealOutputStr (str);
	}

// No prefix is required.  Simply output the text to the view.

	 else
		view->RealOutputStr (str);

// Remember if we are in the middle of outputting a line

	partial_line_output[thread_num+2] = (str[strlen(str)-1] != '\n');

// Free resources and return

	gwmutex_unlock (&VIEW_MUTEX);
}

/* Copy a string to be output to our array of lines */

void CPrime95View::RealOutputStr (
	const char *str)
{
	char	*p;

	gwmutex_lock (&VIEW_LINES_MUTEX);
	p = Lines[0] + strlen (Lines[0]);
	for ( ; *str; str++) {
		if (*str == '\r') continue;

// When we output the first character of a line start displaying that line.
// We do this by incrementing the count of lines and scrolling the window
// We cannot scroll the screen here because the OnUpdate method calls
// SendMessage which must be processed in the main thread.  If the
// main thread has called OutputStr a deadlock will occur awaiting
// the OutputStr mutex.

		if (p == Lines[0]) {
			if (NumLines++)
				PostMessage (WM_COMMAND, USR_SCROLL, 0);
		}

// Shift the lines in memory when a newline is seen.  Otherwise, copy
// the character to the line.

		if (*str == '\n') *p = 0, LineFeed (), p = Lines[0];
		else if (p - Lines[0] < 199) *p++ = *str;
	}
	*p = 0;
	gwmutex_unlock (&VIEW_LINES_MUTEX);

// Call base class routine to refresh screen

	OnUpdate (NULL, 0, NULL);
}

/* Destroy a window.  The user has decided to close the MDI window. */
/* We use a mutex to make sure a worker is not writing to this MDI window while we are destroying it. */

void CPrime95View::OnDestroy()
{
	int	i;

	gwmutex_lock (&VIEW_MUTEX);
	for (i = 0; i < MAX_VIEWS; i++)
		if (Views[i] == this) Views[i] = NULL;
	gwmutex_unlock (&VIEW_MUTEX);

	CScrollView::OnDestroy();
}
