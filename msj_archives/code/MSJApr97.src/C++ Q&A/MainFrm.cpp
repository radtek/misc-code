////////////////////////////////////////////////////////////////
// Copyright 1996 Microsoft Systems Journal. 
// If this program works, it was written by Paul DiLascia.
// If not, I don't know who wrote it.
// This program compiles with Visual C++ 4.1 on Windows 95
//
// Main window for TRACEWIN
//
#include "StdAfx.h"
#include "MfxTrace.h"
#include "MainFrm.h"
#include "WinPlace.h"
#include "FontUI.h"
#include "resource.h"
#include <direct.h>

// Registry key for profile settings
//
const char* CMainFrame::REGKEY = "Settings";

IMPLEMENT_DYNAMIC(CMainFrame, CFrameWnd)

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
	//{{AFX_MSG_MAP(CMainFrame)
	ON_MESSAGE(WM_COPYDATA, OnTraceMsg)
	ON_WM_CREATE()
	ON_WM_SETFOCUS()
	ON_COMMAND(ID_FILE_SAVE_AS,					OnFileSaveAs)
	ON_COMMAND(ID_OUTPUT_OFF,						OnOutputOff)
	ON_COMMAND(ID_OUTPUT_TO_FILE,					OnOutputToFile)
	ON_COMMAND(ID_OUTPUT_TO_WINDOW,				OnOutputToWindow)
	ON_COMMAND(ID_KEEP_ON_TOP,						OnKeepOnTop)
	ON_COMMAND(ID_EDIT_FONT,						OnFontChange)
	ON_UPDATE_COMMAND_UI(ID_FILE_SAVE_AS,		OnUpdateFileSaveAs)
	ON_UPDATE_COMMAND_UI(ID_OUTPUT_OFF,			OnUpdateOutputOff)
	ON_UPDATE_COMMAND_UI(ID_OUTPUT_TO_FILE,	OnUpdateOutputToFile)
	ON_UPDATE_COMMAND_UI(ID_OUTPUT_TO_WINDOW, OnUpdateOutputToWindow)
	ON_UPDATE_COMMAND_UI(ID_KEEP_ON_TOP,		OnUpdateKeepOnTop)
	ON_WM_CLOSE()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

CMainFrame::CMainFrame() 
{
	// Load state from profile
	m_nOutputWhere = ID_OUTPUT_TO_WINDOW;
	CString dir = AfxGetApp()->GetProfileString(REGKEY,"Directory");
	if (!dir.IsEmpty())
		_chdir(dir);
}

CMainFrame::~CMainFrame()
{
}

/////////////////
// Override to use "MfxTraceWindow" as 
// class name so applications can find us.
//
BOOL CMainFrame::PreCreateWindow(CREATESTRUCT& cs)
{
	static LPCSTR className = NULL;

	if (!CFrameWnd::PreCreateWindow(cs))
		return FALSE;

	if (className==NULL) {
		// One-time class registration
		// The only purpose is to make the class name something meaningful
		// instead of "Afx:0x4d:27:32:hup1hup:hike!"
		//
		WNDCLASS wndcls;
		::GetClassInfo(AfxGetInstanceHandle(), cs.lpszClass, &wndcls);
		wndcls.lpszClassName = TRACEWND_CLASSNAME;
		wndcls.hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
		VERIFY(AfxRegisterClass(&wndcls));
		className=TRACEWND_CLASSNAME;
	}
	cs.lpszClass = className;
	if (AfxGetApp()->GetProfileInt(REGKEY, "KeepOnTop", 0))
		cs.dwExStyle |= WS_EX_TOPMOST;

	return TRUE;
}

//////////////////
// Main frame created.
//
int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CFrameWnd::OnCreate(lpCreateStruct) == -1)
		return -1;

	// Create buffer window
	if (!m_wndBuffer.Create(this, AFX_IDW_PANE_FIRST)) {
		TRACE("Failed to create buffer window\n");
		return -1;      // fail to create
	}

	// Restore font from profile
	CFontUI fui;
	if (!fui.GetProfileFont(REGKEY, "Font", m_font)) {
		// Use 8pt Courier (monospace) default
		m_font.CreatePointFont(80,"Courier");
	}
	m_wndBuffer.SetFont(&m_font);

	return 0;
}

//////////////////
// If I get focus, give to buffer
//
void CMainFrame::OnSetFocus(CWnd* pOldWnd)
{
	m_wndBuffer.SetFocus();
}

//////////////////
// Got command message: pass to trace buffer
// Without this, m_wndBuffer doesn't get a chance to handle 
// menu commands with ON_COMMAND, etc.
//
BOOL CMainFrame::OnCmdMsg(UINT nID, int nCode, void* pExtra,
	AFX_CMDHANDLERINFO* pHandlerInfo)
{
	if (m_wndBuffer.OnCmdMsg(nID, nCode, pExtra, pHandlerInfo))
		return TRUE;	// handled by buffer window

	return CFrameWnd::OnCmdMsg(nID, nCode, pExtra, pHandlerInfo);
}

//////////////////
// Someone sent me a WM_COPYDATA message: display it
//
LRESULT CMainFrame::OnTraceMsg(WPARAM wParam, LPARAM lParam)
{
	COPYDATASTRUCT* pcds = (COPYDATASTRUCT*)lParam;
	if (m_nOutputWhere==ID_OUTPUT_OFF || pcds->dwData!=ID_COPYDATA_TRACEMSG)
		return 0;

	LPCTSTR lpText = (LPCTSTR)pcds->lpData;
	DWORD len = pcds->cbData;

	if (m_nOutputWhere==ID_OUTPUT_TO_WINDOW) {
		// Convert \n to \n\r for Windows brain-damaged edit control
		// It's 1995, and I'm still writing code like this!
		//
		LPCTSTR src = lpText;
		LPCTSTR endsrc = lpText + len;

		TCHAR buf[1024];
		TCHAR* dst = buf;
		TCHAR* endbuf = buf + sizeof(buf) - 1;

		while (src < endsrc && dst < endbuf) {
			if (*src == '\n')
				*dst++ = '\r';
			*dst++ = *src++;
		}
		*dst = 0;

		// Append string to contents of trace buffer
		m_wndBuffer.SetSel(-1, -1);		// end of edit text
		m_wndBuffer.ReplaceSel(buf);		// append string..
		m_wndBuffer.SendMessage(EM_SCROLLCARET);	// ..and show caret

	} else if (m_nOutputWhere==ID_OUTPUT_TO_FILE) {
		m_file.Write(lpText, len);
	}
	return TRUE;
}

//////////////////
// Save contents of trace buffer to a file
//
void CMainFrame::OnFileSaveAs() 
{
	CFileDialog dlg(FALSE);
	if (dlg.DoModal()!=IDOK) 
		return;

	CFile f;
	if (OpenFile(f, dlg.GetPathName())) {
		char buf[256];
		int count = m_wndBuffer.GetLineCount();
		for (int line=0; line<count; line++) {
			int len = m_wndBuffer.GetLine(line, buf, sizeof(buf)-1);
			buf[len++]='\n';
			f.Write(buf, len);
		}
		f.Close();
	}
}

//////////////////
// Helper to open a file
//
BOOL CMainFrame::OpenFile(CFile& f, LPCSTR lpszPathName)
{
	BOOL bOpen = FALSE;
	TRY {
		bOpen = f.Open(lpszPathName,
			CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite);

	} CATCH_ALL(e) {
	} END_CATCH_ALL

	if (!bOpen) {
		MessageBox("Failed to open file.","TRACEWIN", MB_OK|MB_ICONEXCLAMATION);
	}

	return bOpen;
}

void CMainFrame::CloseTraceFile()
{
	if (m_file.m_hFile != CFile::hFileNull)
		m_file.Close();
}

void CMainFrame::OnUpdateFileSaveAs(CCmdUI* pCmdUI) 
{
	pCmdUI->Enable(!m_wndBuffer.IsEmpty());
}

void CMainFrame::OnOutputOff() 
{
	CloseTraceFile();
	m_nOutputWhere = ID_OUTPUT_OFF;
}

void CMainFrame::OnUpdateOutputOff(CCmdUI* pCmdUI) 
{
	pCmdUI->SetRadio(m_nOutputWhere==ID_OUTPUT_OFF);
}

void CMainFrame::OnOutputToFile() 
{
	CFileDialog dlg(TRUE);
	dlg.m_ofn.lpstrTitle = "Output to File";
	if (dlg.DoModal()!=IDOK) 
		return;

	CloseTraceFile();
	if (OpenFile(m_file, dlg.GetPathName()))
		m_nOutputWhere = ID_OUTPUT_TO_FILE;
}

void CMainFrame::OnUpdateOutputToFile(CCmdUI* pCmdUI) 
{
	pCmdUI->SetRadio(m_nOutputWhere==ID_OUTPUT_TO_FILE);
}

void CMainFrame::OnOutputToWindow() 
{
	CloseTraceFile();
	m_nOutputWhere = ID_OUTPUT_TO_WINDOW;
}

void CMainFrame::OnUpdateOutputToWindow(CCmdUI* pCmdUI) 
{
	pCmdUI->SetRadio(m_nOutputWhere==ID_OUTPUT_TO_WINDOW);
}

void CMainFrame::OnKeepOnTop() 
{
	DWORD dwStyle = ::GetWindowLong(m_hWnd, GWL_EXSTYLE);
	const CWnd* pWnd = dwStyle & WS_EX_TOPMOST ? &wndNoTopMost : &wndTopMost;
	SetWindowPos(pWnd,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE);
}

void CMainFrame::OnUpdateKeepOnTop(CCmdUI* pCmdUI) 
{
	DWORD dwStyle = ::GetWindowLong(m_hWnd, GWL_EXSTYLE);
	pCmdUI->SetCheck(dwStyle & WS_EX_TOPMOST ? TRUE : FALSE);
}

//////////////////
// Frame window closed: save position, size in profile.
//
void CMainFrame::OnClose() 
{
	// Save current settings in registry
	CWindowPlacement wp;
	wp.Save(REGKEY, "MainWindow", this);

	CWinApp *pApp = AfxGetApp();
	pApp->WriteProfileInt(REGKEY, "TraceWhere", m_nOutputWhere);
	pApp->WriteProfileInt(REGKEY, "KeepOnTop",  
		::GetWindowLong(m_hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST ? TRUE : FALSE);

	char buf[_MAX_PATH];
	_getcwd(buf, sizeof(buf));
	pApp->WriteProfileString(REGKEY, "Directory", buf);

	CFontUI().WriteProfileFont(REGKEY, "Font", m_font);

	CFrameWnd::OnClose();
}

////////////////////////////////////////////////////////////////
// Handle font change (bigger/smaller/dialog)
//
void CMainFrame::OnFontChange()
{
	CFontUI fui;
	if (fui.OnChangeFont(m_font,0,this,
		CF_SCREENFONTS|CF_FORCEFONTEXIST|CF_FIXEDPITCHONLY)) {
		m_wndBuffer.SetFont(&m_font);
		m_wndBuffer.Invalidate(TRUE);
		m_wndBuffer.UpdateWindow();
	}
}

