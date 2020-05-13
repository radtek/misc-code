// MSJ Visual Developer Column
//
// This source code is a small sub-set of one of 50 classes
// found in Stingray's Objective Toolkit product
// For more information: http://www.stingsoft.com

//  Author:		Scot Wingo and George Shepherd.
//  Description:	Declarations for MSJSuperView
//  Created:		9/1/97
//

#include "stdafx.h"
#include "cloud.h"
#include "afxpriv.h" //STATUSBAR
#include "mainfrm.h"

#ifdef _DEBUG
#undef THIS_FILE
static char BASED_CODE THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CMainFrame

IMPLEMENT_DYNAMIC(CMainFrame, CMDIFrameWnd)

BEGIN_MESSAGE_MAP(CMainFrame, CMDIFrameWnd)
	//{{AFX_MSG_MAP(CMainFrame)
		// NOTE - the ClassWizard will add and remove mapping macros here.
		//    DO NOT EDIT what you see in these blocks of generated code !
    ON_WM_CREATE()
	//}}AFX_MSG_MAP
    ON_UPDATE_COMMAND_UI(ID_INDICATOR_LOCATION,OnUpdateLocation)
    ON_UPDATE_COMMAND_UI(ID_INDICATOR_ZOOMP,OnUpdatePercent)
    ON_UPDATE_COMMAND_UI(ID_INDICATOR_ZOOMF,OnUpdateLevel)
    ON_UPDATE_COMMAND_UI(ID_INDICATOR_POINT,OnUpdatePoint)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// arrays of IDs used to initialize control bars
	
// toolbar buttons - IDs are command buttons
static UINT BASED_CODE buttons[] =
{
	// same order as in the bitmap 'toolbar.bmp'
	ID_FILE_NEW,
	ID_FILE_OPEN,
	ID_FILE_SAVE,
	ID_SEPARATOR,
	ID_EDIT_CUT,
	ID_EDIT_COPY,
	ID_EDIT_PASTE,
	ID_SEPARATOR,
	ID_FILE_PRINT,
	ID_APP_ABOUT,
};

static UINT BASED_CODE indicators[] =
{
    ID_SEPARATOR,           // status line indicator
    ID_INDICATOR_POINT,
    ID_INDICATOR_ZOOMP,
    ID_INDICATOR_ZOOMF,
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

int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    const int nDropHeight = 100;
    if (CMDIFrameWnd::OnCreate(lpCreateStruct) == -1)
	return -1;
    
    if (!m_wndToolBar.Create(this) ||
	!m_wndToolBar.LoadBitmap(IDR_MAINFRAME) ||
	!m_wndToolBar.SetButtons(buttons,
				 sizeof(buttons)/sizeof(UINT)))
	{
	    TRACE0("Failed to create toolbar\n");
	    return -1;      // fail to create
	}
    
    if (!m_wndStatusBar.Create(this) ||
	!m_wndStatusBar.SetIndicators(indicators,
				      sizeof(indicators)/sizeof(UINT)))
	{
	    TRACE0("Failed to create status bar\n");
	    return -1;      // fail to create
	}
    
    m_wndToolBar.EnableDocking(CBRS_ALIGN_ANY);
    EnableDocking(CBRS_ALIGN_ANY);
    DockControlBar(&m_wndToolBar);
    
    // TODO: Remove this if you don't want tool tips
    m_wndToolBar.SetBarStyle(m_wndToolBar.GetBarStyle() |
			     CBRS_TOOLTIPS | CBRS_FLYBY);

 
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
// CMainFrame support for on demand updating of the STATUSBAR


void CMainFrame::MyUpdatePane ()
{
    m_wndStatusBar.SendMessage(WM_IDLEUPDATECMDUI); // Include afxpriv.h 
    m_wndStatusBar.UpdateWindow();
}                                 

void CMainFrame::MyChangePane(int nPane, LPCTSTR text )
{ 
    m_wndStatusBar.SetPaneText(nPane,text,TRUE) ;
    MyUpdatePane();
}


void CMainFrame::OnUpdatePercent(CCmdUI * pCmdUI)
{
    pCmdUI->Enable(TRUE);
}

void CMainFrame::OnUpdateLevel(CCmdUI * pCmdUI)
{
    pCmdUI->Enable(TRUE);
}

void CMainFrame::OnUpdateLocation(CCmdUI * pCmdUI)
{
    pCmdUI->Enable(TRUE);
    pCmdUI->SetText(_T("foobar"));
}

void CMainFrame::OnUpdatePoint(CCmdUI * pCmdUI)
{
    pCmdUI->Enable(TRUE);
}

