// ChildFrm.cpp : implementation of the CChildFrame class
//
#include "stdafx.h"
#include "prime95.h"

#include "ChildFrm.h"
#include "Prime95Doc.h"
#include "Prime95View.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CChildFrame

IMPLEMENT_DYNCREATE(CChildFrame, CMDIChildWnd)

BEGIN_MESSAGE_MAP(CChildFrame, CMDIChildWnd)
END_MESSAGE_MAP()


// CChildFrame construction/destruction

CChildFrame::CChildFrame()
{
	// TODO: add member initialization code here
}

CChildFrame::~CChildFrame()
{
}


BOOL CChildFrame::PreCreateWindow(CREATESTRUCT& cs)
{
	// TODO: Modify the Window class or styles here by modifying the CREATESTRUCT cs

//	cs.style &= ~FWS_ADDTOTITLE;	// We'll control window titles!
	cs.style &= ~WS_MINIMIZEBOX;	// Disable minimize button
	if( !CMDIChildWnd::PreCreateWindow(cs) )
		return FALSE;

	return TRUE;
}


void CChildFrame::OnUpdateFrameTitle(BOOL bAddToTitle)
{
//	GetMDIFrame()->OnUpdateFrameTitle(bAddToTitle);

// For lack of a better place to do this, disable SC_CLOSE system menu here

	CMenu *pSysMenu = GetSystemMenu(FALSE);
	ASSERT(pSysMenu != NULL);
	pSysMenu->EnableMenuItem(SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
}

// CChildFrame diagnostics

#ifdef _DEBUG
void CChildFrame::AssertValid() const
{
	CMDIChildWnd::AssertValid();
}

void CChildFrame::Dump(CDumpContext& dc) const
{
	CMDIChildWnd::Dump(dc);
}

#endif //_DEBUG


// CChildFrame message handlers
