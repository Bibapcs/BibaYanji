/* Copyright 1995-2017 Mersenne Research, Inc.  All rights reserved */

// Prime95View.h : interface of the CPrime95View class
//
/////////////////////////////////////////////////////////////////////////////

#define MAX_VIEW_LINES	1000

class CPrime95View : public CScrollView
{
protected: // create from serialization only
	CPrime95View();
	DECLARE_DYNCREATE(CPrime95View)

// Attributes
public:
	CPrime95Doc* GetDocument();

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CPrime95View)
	public:
	virtual void OnDraw(CDC* pDC);  // overridden to draw this view
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	virtual void OnUpdate(CView* pSender, LPARAM lHint, CObject* pHint);
	protected:
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~CPrime95View();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:
	char	BaseTitle[80];		// Base Title (prefix) of MDI window
	char	Title[80];		// Title of MDI window
	char	LineData[MAX_VIEW_LINES][200]; // Data area for lines of text
	char	*Lines[MAX_VIEW_LINES];	// Pointers to lines of text
	int	NumLines;		// Number of text lines we have
	int	MaxLineSize;		// Number of chars in widest line
	HICON	icon;			// Icon to display

public:
	void base_title (const char *);
	void title (const char *);
	void ChangeIcon (int);
	void getCharSize ();
	void position (int, int, BOOL);
	void LineFeed ();
	void RealOutputStr (const char *);

// Generated message map functions
protected:
	//{{AFX_MSG(CPrime95View)
	afx_msg void OnScroll();
	afx_msg void OnTitle();
	afx_msg void OnIcon();
	afx_msg void OnEditCopy();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnDestroy();
};

#ifndef _DEBUG  // debug version in Prime95View.cpp
inline CPrime95Doc* CPrime95View::GetDocument()
   { return (CPrime95Doc*)m_pDocument; }
#endif

void PositionViews (int forceTile);
void SaveViews (void);

/////////////////////////////////////////////////////////////////////////////

