
#pragma once

#include <windows.h>
#include <commdlg.h>
#include <gdiplus.h>
#include "types.h"

extern HWND g_hwndMain;
extern HWND g_hwndEditor;
extern HWND g_hwndStatus;
extern HWND g_hwndFindDlg;
extern HACCEL g_hAccel;
extern AppState g_state;
extern WNDPROC g_origEditorProc;
extern WNDPROC g_origStatusProc;
extern ULONG_PTR g_gdiplusToken;
extern Gdiplus::Image *g_bgImage;
extern HBITMAP g_bgBitmap;
extern int g_bgBitmapW;
extern int g_bgBitmapH;
extern HBRUSH g_hbrStatusDark;
extern HBRUSH g_hbrMenuDark;
extern PAGESETUPDLGW g_pageSetup;
extern std::wstring g_statusTexts[5];
extern HICON g_hCustomIcon;
extern bool g_ignoreCheck; // "force compile" active: suppress the code-check popup
