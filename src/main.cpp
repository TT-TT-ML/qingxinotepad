#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <gdiplus.h>
#include "resource.h"
#include "core/types.h"
#include "core/globals.h"
#include "modules/theme.h"
#include "modules/editor.h"
#include "modules/codeeditor.h"
#include "modules/file.h"
#include "modules/ui.h"
#include "modules/background.h"
#include "modules/dialog.h"
#include "modules/commands.h"
#include "modules/settings.h"
#include "modules/menu.h"
#include "modules/compiler.h"
#include "modules/consolewindow.h"
#include "modules/tabs.h"
#include "modules/menu_skin.h"
#include "modules/libwindow.h"
#include "modules/syntax.h"
#include "lang/lang.h"

// 切换界面语言：统一走这一处，避免各 case 里漏掉某个刷新环节
static void ApplyUiLanguage(LangID id)
{
    if (g_hwndFindDlg)
    {
        DestroyWindow(g_hwndFindDlg);
        g_hwndFindDlg = nullptr;
    }
    SetLanguage(id);
    UpdateMenuStrings();
    UpdateRecentFilesMenu();   // 最近文件子菜单是运行时插入的 POPUP，标题也要跟着换
    UpdateLanguageMenu();
    UpdateTitle();
    UpdateStatus();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hwndMain = hwnd;
        DragAcceptFiles(hwnd, TRUE);
        g_hwndEditor = CreateCodeEditor(hwnd, reinterpret_cast<HMENU>(IDC_EDITOR), GetModuleHandleW(nullptr));
        g_hwndStatus = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr,
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_STATUSBAR), GetModuleHandleW(nullptr), nullptr);
        g_origStatusProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_hwndStatus, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(StatusSubclassProc)));
        TabsInit();
        TabsCreateBar(hwnd);
        ApplyFont();
        ApplyWordWrap();
        CheckMenuItem(GetMenu(g_hwndMain), IDM_FORMAT_WORDWRAP,
                      g_state.wordWrap ? MF_CHECKED : MF_UNCHECKED);
        SetupStatusBarParts();
        UpdateMenuStrings();
        UpdateLanguageMenu();
        CheckMenuItem(GetMenu(g_hwndMain), IDM_VIEW_ALWAYSONTOP, g_state.alwaysOnTop ? MF_CHECKED : MF_UNCHECKED);
        if (g_state.alwaysOnTop)
            SetWindowPos(g_hwndMain, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        UpdateTitle();
        UpdateStatus();
        ApplyTheme();
        SetFocus(g_hwndEditor);
        return 0;
    }
    case WM_UAHDRAWMENU:
    {
        if (IsDarkMode())
        {
            UAHMENU* pUDM = reinterpret_cast<UAHMENU*>(lParam);
            MENUBARINFO mbi = {};
            mbi.cbSize = sizeof(mbi);
            if (GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi))
            {
                RECT rcWindow;
                GetWindowRect(hwnd, &rcWindow);
                RECT rcMenuBar = mbi.rcBar;
                OffsetRect(&rcMenuBar, -rcWindow.left, -rcWindow.top);
                FillRect(pUDM->hdc, &rcMenuBar, g_hbrMenuDark ? g_hbrMenuDark : reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            }
            return TRUE;
        }
        break;
    }
    case WM_UAHDRAWMENUITEM:
    {
        if (IsDarkMode())
        {
            UAHDRAWMENUITEM* pUDMI = reinterpret_cast<UAHDRAWMENUITEM*>(lParam);
            wchar_t szText[256] = {};
            MENUITEMINFOW mii = {};
            mii.cbSize = sizeof(mii);
            mii.fMask = MIIM_STRING;
            mii.dwTypeData = szText;
            mii.cch = 255;
            GetMenuItemInfoW(pUDMI->um.hMenu, pUDMI->umi.iPosition, TRUE, &mii);
            COLORREF bgColor = RGB(45, 45, 45);
            COLORREF textColor = RGB(255, 255, 255);
            if ((pUDMI->dis.itemState & ODS_HOTLIGHT) || (pUDMI->dis.itemState & ODS_SELECTED))
                bgColor = RGB(65, 65, 65);
            HBRUSH hbr = CreateSolidBrush(bgColor);
            FillRect(pUDMI->um.hdc, &pUDMI->dis.rcItem, hbr);
            DeleteObject(hbr);
            NONCLIENTMETRICSW ncm = {};
            ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            HFONT hFont = CreateFontIndirectW(&ncm.lfMenuFont);
            HFONT hOldFont = reinterpret_cast<HFONT>(SelectObject(pUDMI->um.hdc, hFont));
            SetBkMode(pUDMI->um.hdc, TRANSPARENT);
            SetTextColor(pUDMI->um.hdc, textColor);
            RECT rcText = pUDMI->dis.rcItem;
            DrawTextW(pUDMI->um.hdc, szText, -1, &rcText, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            SelectObject(pUDMI->um.hdc, hOldFont);
            DeleteObject(hFont);
            return TRUE;
        }
        break;
    }
    case WM_NCPAINT:
    case WM_NCACTIVATE:
    {
        LRESULT result = DefWindowProcW(hwnd, msg, wParam, lParam);
        if (IsDarkMode())
        {
            HDC hdc = GetWindowDC(hwnd);
            MENUBARINFO mbi = {};
            mbi.cbSize = sizeof(mbi);
            if (GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi))
            {
                RECT rcWindow;
                GetWindowRect(hwnd, &rcWindow);
                RECT rcMenuBar = mbi.rcBar;
                OffsetRect(&rcMenuBar, -rcWindow.left, -rcWindow.top);
                rcMenuBar.bottom += 2;
                FillRect(hdc, &rcMenuBar, g_hbrMenuDark ? g_hbrMenuDark : reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                HMENU hMenu = GetMenu(hwnd);
                int itemCount = GetMenuItemCount(hMenu);
                NONCLIENTMETRICSW ncm = {};
                ncm.cbSize = sizeof(ncm);
                SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
                HFONT hFont = CreateFontIndirectW(&ncm.lfMenuFont);
                HFONT hOldFont = reinterpret_cast<HFONT>(SelectObject(hdc, hFont));
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(255, 255, 255));
                for (int i = 0; i < itemCount; i++)
                {
                    RECT rcItem;
                    if (GetMenuBarInfo(hwnd, OBJID_MENU, i + 1, &mbi))
                    {
                        rcItem = mbi.rcBar;
                        OffsetRect(&rcItem, -rcWindow.left, -rcWindow.top);
                        wchar_t szText[256] = {};
                        MENUITEMINFOW mii = {};
                        mii.cbSize = sizeof(mii);
                        mii.fMask = MIIM_STRING;
                        mii.dwTypeData = szText;
                        mii.cch = 255;
                        GetMenuItemInfoW(hMenu, i, TRUE, &mii);
                        DrawTextW(hdc, szText, -1, &rcItem, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                    }
                }
                SelectObject(hdc, hOldFont);
                DeleteObject(hFont);
            }
            ReleaseDC(hwnd, hdc);
        }
        return result;
    }
    case WM_SETTINGCHANGE:
    {
        if (lParam && wcscmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0)
            ApplyTheme();
        return 0;
    }
    case WM_DROPFILES:
    {
        HDROP hDrop = reinterpret_cast<HDROP>(wParam);
        wchar_t path[MAX_PATH];
        if (DragQueryFileW(hDrop, 0, path, MAX_PATH))
            LoadFile(path); // opens as a new tab
        DragFinish(hDrop);
        return 0;
    }
    case WM_SIZE:
        ResizeControls();
        UpdateStatus();
        return 0;
    case WM_SETFOCUS:
        SetFocus(g_hwndEditor);
        return 0;
    case WM_CTLCOLOREDIT:
        if (g_state.background.enabled && g_bgImage && reinterpret_cast<HWND>(lParam) == g_hwndEditor)
        {
            SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
        }
        break;
    case WM_CTLCOLORSTATIC:
        if (reinterpret_cast<HWND>(lParam) == g_hwndStatus && IsDarkMode())
        {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, RGB(45, 45, 45));
            return reinterpret_cast<LRESULT>(g_hbrStatusDark ? g_hbrStatusDark : GetStockObject(BLACK_BRUSH));
        }
        break;
    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT pDIS = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (pDIS->CtlType == ODT_MENU)
        {
            if (MenuSkinDraw(hwnd, pDIS))
                return TRUE;
            break;
        }
        if (pDIS->hwndItem == g_hwndStatus && IsDarkMode())
        {
            HBRUSH hbr = g_hbrStatusDark ? g_hbrStatusDark : CreateSolidBrush(RGB(45, 45, 45));
            FillRect(pDIS->hDC, &pDIS->rcItem, hbr);
            if (!g_hbrStatusDark)
                DeleteObject(hbr);
            SetBkMode(pDIS->hDC, TRANSPARENT);
            SetTextColor(pDIS->hDC, RGB(255, 255, 255));
            int part = static_cast<int>(pDIS->itemID);
            if (part >= 0 && part < 5)
            {
                RECT rc = pDIS->rcItem;
                rc.left += 4;
                DrawTextW(pDIS->hDC, g_statusTexts[part].c_str(), -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
            }
            return TRUE;
        }
        break;
    }
    case WM_MEASUREITEM:
    {
        MEASUREITEMSTRUCT *mis = reinterpret_cast<MEASUREITEMSTRUCT *>(lParam);
        if (MenuSkinMeasure(mis))
            return TRUE;
        break;
    }
    case WM_COMMAND:
    {
        WORD cmd = LOWORD(wParam);
        if (cmd == IDC_EDITOR && HIWORD(wParam) == EN_CHANGE)
        {
            if (!g_state.modified)
            {
                g_state.modified = true;
                TabsMarkActiveModified();
                UpdateTitle();
            }
            UpdateStatus();
            return 0;
        }
        if (cmd == IDC_EDITOR && HIWORD(wParam) == EN_SELCHANGE)
        {
            UpdateStatus();
            return 0;
        }
        if (cmd >= IDM_FILE_RECENT_BASE && cmd < IDM_FILE_RECENT_BASE + MAX_RECENT_FILES)
        {
            int idx = cmd - IDM_FILE_RECENT_BASE;
            if (idx < static_cast<int>(g_state.recentFiles.size()))
            {
                if (ConfirmDiscard())
                    LoadFile(g_state.recentFiles[idx]);
            }
            return 0;
        }
        switch (cmd)
        {
        case IDM_FILE_NEW:
            FileNew();
            break;
        case IDM_FILE_OPEN:
            FileOpen();
            break;
        case IDM_FILE_SAVE:
            FileSave();
            break;
        case IDM_FILE_SAVEAS:
            FileSaveAs();
            break;
        case IDM_FILE_PRINT:
            FilePrint();
            break;
        case IDM_FILE_PAGESETUP:
            FilePageSetup();
            break;
        case IDM_FILE_EXIT:
            if (ConfirmDiscard())
                DestroyWindow(hwnd);
            break;
        case IDM_EDIT_UNDO:
            EditUndo();
            break;
        case IDM_EDIT_REDO:
            EditRedo();
            break;
        case IDM_EDIT_CUT:
            EditCut();
            break;
        case IDM_EDIT_COPY:
            EditCopy();
            break;
        case IDM_EDIT_PASTE:
            EditPaste();
            break;
        case IDM_EDIT_DELETE:
            EditDelete();
            break;
        case IDM_EDIT_FIND:
            EditFind();
            break;
        case IDM_EDIT_FINDNEXT:
            EditFindNext();
            break;
        case IDM_EDIT_FINDPREV:
            EditFindPrev();
            break;
        case IDM_EDIT_REPLACE:
            EditReplace();
            break;
        case IDM_EDIT_GOTO:
            EditGoto();
            break;
        case IDM_EDIT_SELECTALL:
            EditSelectAll();
            break;
        case IDM_EDIT_TIMEDATE:
            EditTimeDate();
            break;
        case IDM_FORMAT_WORDWRAP:
            FormatWordWrap();
            break;
        case IDM_FORMAT_FONT:
            FormatFont();
            break;
        case IDM_FORMAT_ENC_BASE + 0:
            SetDocumentEncoding(Encoding::UTF8);
            break;
        case IDM_FORMAT_ENC_BASE + 1:
            SetDocumentEncoding(Encoding::UTF8BOM);
            break;
        case IDM_FORMAT_ENC_BASE + 2:
            SetDocumentEncoding(Encoding::UTF16LE);
            break;
        case IDM_FORMAT_ENC_BASE + 3:
            SetDocumentEncoding(Encoding::UTF16BE);
            break;
        case IDM_FORMAT_ENC_BASE + 4:
            SetDocumentEncoding(Encoding::ANSI);
            break;
        case IDM_FORMAT_EOL_BASE + 0:
            SetDocumentLineEnding(LineEnding::CRLF);
            break;
        case IDM_FORMAT_EOL_BASE + 1:
            SetDocumentLineEnding(LineEnding::LF);
            break;
        case IDM_FORMAT_EOL_BASE + 2:
            SetDocumentLineEnding(LineEnding::CR);
            break;
        case IDM_VIEW_ZOOMIN:
            ViewZoomIn();
            break;
        case IDM_VIEW_ZOOMOUT:
            ViewZoomOut();
            break;
        case IDM_VIEW_ZOOMDEFAULT:
            ViewZoomDefault();
            break;
        case IDM_VIEW_STATUSBAR:
            ViewStatusBar();
            break;
        case IDM_VIEW_DARKMODE:
            ToggleDarkMode();
            break;
        case IDM_VIEW_TRANSPARENCY:
            ViewTransparency();
            break;
        case IDM_VIEW_ALWAYSONTOP:
            ViewAlwaysOnTop();
            break;
        case IDM_VIEW_BG_SELECT:
            ViewSelectBackground();
            break;
        case IDM_VIEW_BG_CLEAR:
            ViewClearBackground();
            break;
        case IDM_VIEW_BG_OPACITY:
            ViewBackgroundOpacity();
            break;
        case IDM_VIEW_BG_POS_TOPLEFT:
            SetBackgroundPosition(BgPosition::TopLeft);
            break;
        case IDM_VIEW_BG_POS_TOPCENTER:
            SetBackgroundPosition(BgPosition::TopCenter);
            break;
        case IDM_VIEW_BG_POS_TOPRIGHT:
            SetBackgroundPosition(BgPosition::TopRight);
            break;
        case IDM_VIEW_BG_POS_CENTERLEFT:
            SetBackgroundPosition(BgPosition::CenterLeft);
            break;
        case IDM_VIEW_BG_POS_CENTER:
            SetBackgroundPosition(BgPosition::Center);
            break;
        case IDM_VIEW_BG_POS_CENTERRIGHT:
            SetBackgroundPosition(BgPosition::CenterRight);
            break;
        case IDM_VIEW_BG_POS_BOTTOMLEFT:
            SetBackgroundPosition(BgPosition::BottomLeft);
            break;
        case IDM_VIEW_BG_POS_BOTTOMCENTER:
            SetBackgroundPosition(BgPosition::BottomCenter);
            break;
        case IDM_VIEW_BG_POS_BOTTOMRIGHT:
            SetBackgroundPosition(BgPosition::BottomRight);
            break;
        case IDM_VIEW_BG_POS_TILE:
            SetBackgroundPosition(BgPosition::Tile);
            break;
        case IDM_VIEW_BG_POS_STRETCH:
            SetBackgroundPosition(BgPosition::Stretch);
            break;
        case IDM_VIEW_BG_POS_FIT:
            SetBackgroundPosition(BgPosition::Fit);
            break;
        case IDM_VIEW_BG_POS_FILL:
            SetBackgroundPosition(BgPosition::Fill);
            break;
        case IDM_VIEW_LANG_EN:
            ApplyUiLanguage(LangID::EN);
            break;
        case IDM_VIEW_LANG_JA:
            ApplyUiLanguage(LangID::JA);
            break;
        case IDM_VIEW_LANG_ZH_CN:
            ApplyUiLanguage(LangID::ZH_CN);
            break;
        case IDM_VIEW_LANG_RU:
            ApplyUiLanguage(LangID::RU);
            break;
        case IDM_VIEW_LANG_ES:
            ApplyUiLanguage(LangID::ES);
            break;
        case IDM_VIEW_LANG_ZH_TW:
            ApplyUiLanguage(LangID::ZH_TW);
            break;
        case IDM_VIEW_LANG_KO:
            ApplyUiLanguage(LangID::KO);
            break;
        case IDM_VIEW_LANG_FR:
            ApplyUiLanguage(LangID::FR);
            break;
        case IDM_VIEW_LANG_DE:
            ApplyUiLanguage(LangID::DE);
            break;
        case IDM_VIEW_LANG_PT:
            ApplyUiLanguage(LangID::PT);
            break;
        case IDM_VIEW_LANG_AR:
            ApplyUiLanguage(LangID::AR);
            break;
        case IDM_VIEW_LANG_HI:
            ApplyUiLanguage(LangID::HI);
            break;
        case IDM_VIEW_LANG_IT:
            ApplyUiLanguage(LangID::IT);
            break;
        case IDM_VIEW_LANG_NL:
            ApplyUiLanguage(LangID::NL);
            break;
        case IDM_VIEW_LANG_TR:
            ApplyUiLanguage(LangID::TR);
            break;
        case IDM_VIEW_ICON_CHANGE:
            ViewChangeIcon();
            break;
        case IDM_VIEW_ICON_RESET:
            ViewResetIcon();
            break;
        case IDM_VIEW_CONSOLE:
            // 打开自绘命令行窗口，工作目录跟随当前显示的文件
            ConsoleShow(g_hwndMain);
            break;
        case IDM_CONSOLE_CLEAR:
            ConsoleClear();
            break;
        case IDM_CONSOLE_SETTINGS:
            ConsoleShowSettings(g_hwndMain);
            break;
        case IDM_CONSOLE_CLOSE:
            ConsoleClose();
            break;
        case IDM_HELP_ABOUT:
            HelpAbout();
            break;
        case IDM_BUILD_COMPILE:
            BuildCompile();
            break;
        case IDM_BUILD_RUN:
            // 编译并运行：编译成功后自动把产物送进命令行窗口执行
            BuildCompileRun();
            break;
        case IDM_BUILD_FORCE:
            BuildCompileForce();
            break;
        case IDM_BUILD_CODECHECK:
            BuildToggleCodeCheck();
            break;
        case IDM_BUILD_SETTINGS:
            BuildShowSettings();
            break;
        case IDM_BUILD_LIBS:
            ShowLibraryWindow(g_hwndMain);
            break;
        default:
            if (cmd >= IDM_BUILD_LANG_BASE && cmd < IDM_BUILD_LANG_BASE + LanguageCount())
            {
                const LanguageDef *L = LanguageByIndex(cmd - IDM_BUILD_LANG_BASE);
                if (L && L->id)
                    BuildCompileLang(std::wstring(L->id));
            }
            break;
        }
        return 0;
    }
    case WM_APP_COMPILE_DONE:
        // Background compile finished (posted from the compile worker thread).
        CompileNotifyDone(hwnd, wParam);
        return 0;
    case WM_NOTIFY:
    {
        NMHDR* pnmh = reinterpret_cast<NMHDR*>(lParam);
        if (pnmh->hwndFrom == g_hwndStatus && pnmh->code == NM_CUSTOMDRAW)
        {
            if (IsDarkMode())
            {
                LPNMCUSTOMDRAW lpnmcd = reinterpret_cast<LPNMCUSTOMDRAW>(lParam);
                if (lpnmcd->dwDrawStage == CDDS_PREPAINT)
                    return CDRF_NOTIFYITEMDRAW;
                if (lpnmcd->dwDrawStage == CDDS_ITEMPREPAINT)
                {
                    HBRUSH hbr = g_hbrStatusDark ? g_hbrStatusDark : CreateSolidBrush(RGB(45, 45, 45));
                    FillRect(lpnmcd->hdc, &lpnmcd->rc, hbr);
                    if (!g_hbrStatusDark && hbr)
                        DeleteObject(hbr);
                    SetBkMode(lpnmcd->hdc, TRANSPARENT);
                    SetBkColor(lpnmcd->hdc, RGB(45, 45, 45));
                    SetTextColor(lpnmcd->hdc, RGB(255, 255, 255));
                    wchar_t buf[256] = {};
                    int part = static_cast<int>(lpnmcd->dwItemSpec);
                    SendMessageW(g_hwndStatus, SB_GETTEXTW, part, reinterpret_cast<LPARAM>(buf));
                    RECT rc = lpnmcd->rc;
                    rc.left += 6;
                    DrawTextW(lpnmcd->hdc, buf, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
                    return CDRF_SKIPDEFAULT;
                }
            }
        }
        if (pnmh->hwndFrom == g_hwndEditor && pnmh->code == EN_SELCHANGE)
        {
            UpdateStatus();
        }
        return 0;
    }
    case WM_CLOSE:
        if (g_state.closing)
            return 0;
        g_state.closing = true;
        if (TabsConfirmExit())
            DestroyWindow(hwnd);
        else
            g_state.closing = false;
        return 0;
    case WM_DESTROY:
    {
        WINDOWPLACEMENT wp = {};
        wp.length = sizeof(WINDOWPLACEMENT);
        if (GetWindowPlacement(hwnd, &wp))
        {
            RECT rect = wp.rcNormalPosition;
            g_state.windowX = rect.left;
            g_state.windowY = rect.top;
            g_state.windowWidth = rect.right - rect.left;
            g_state.windowHeight = rect.bottom - rect.top;
        }
        SaveWindowSettings();
    }
    if (g_state.hFont)
    {
        DeleteObject(g_state.hFont);
        g_state.hFont = nullptr;
    }
    if (g_bgImage)
    {
        delete g_bgImage;
        g_bgImage = nullptr;
    }
    if (g_bgBitmap)
    {
        DeleteObject(g_bgBitmap);
        g_bgBitmap = nullptr;
    }
    if (g_hCustomIcon)
    {
        DestroyIcon(g_hCustomIcon);
        g_hCustomIcon = nullptr;
    }
    PostQuitMessage(0);
    return 0;
    case WM_MOUSEWHEEL:
        if (GetKeyState(VK_CONTROL) & 0x8000)
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (delta > 0)
                ViewZoomIn();
            else
                ViewZoomOut();
            return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow)
{
    InitLanguage();
    typedef BOOL(WINAPI* fnSetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32)
    {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
        auto setProcDPI = reinterpret_cast<fnSetProcessDpiAwarenessContext>(GetProcAddress(hUser32, "SetProcessDpiAwarenessContext"));
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
        if (setProcDPI)
            setProcDPI(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
    LoadFontSettings();
    LoadWindowSettings();
    LoadCompilerCommands();
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_NOTEPAD));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszMenuName = MAKEINTRESOURCEW(IDR_MAINMENU);
    wc.lpszClassName = L"NotepadClass";
    wc.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_NOTEPAD));
    RegisterClassExW(&wc);
    RegisterCodeEditorClasses(hInstance);
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr);
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);
    const auto& lang = GetLangStrings();
    std::wstring initialTitle = lang.untitled + L" - " + lang.appName;
    g_hwndMain = CreateWindowExW(0, L"NotepadClass", initialTitle.c_str(),
        WS_OVERLAPPEDWINDOW | WS_MAXIMIZEBOX | WS_CLIPCHILDREN, g_state.windowX, g_state.windowY, g_state.windowWidth, g_state.windowHeight,
        nullptr, nullptr, hInstance, nullptr);
    g_hAccel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(IDR_ACCEL));
    ShowWindow(g_hwndMain, nCmdShow);
    UpdateWindow(g_hwndMain);
    // 自绘命令行窗口（查看 → 命令行）：注册窗口类并读取保存的行列网格
    ConsoleInstall(g_hwndMain);
    if (lpCmdLine && lpCmdLine[0])
    {
        // 支持 --console-rows N / --console-cols N 覆盖命令行窗口的默认网格，
        // 其余位置参数按文件名处理（可能带引号）。
        int argc = 0;
        LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::wstring path;
        if (argv)
        {
            for (int i = 1; i < argc; ++i)
            {
                std::wstring a = argv[i];
                if (a == L"--console-rows" && i + 1 < argc)
                    ConsoleSetDefaultSize(_wtoi(argv[++i]), ConsoleDefaultCols());
                else if (a == L"--console-cols" && i + 1 < argc)
                    ConsoleSetDefaultSize(ConsoleDefaultRows(), _wtoi(argv[++i]));
                else if (a == L"--console")
                    ConsoleShow(g_hwndMain);
                else if (!a.empty() && a[0] != L'-')
                    path = a;
            }
            LocalFree(argv);
        }
        if (!path.empty())
        {
            if (path.front() == L'"' && path.back() == L'"' && path.size() > 1)
                path = path.substr(1, path.size() - 2);
            LoadFile(path);
        }
    }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        if (g_hwndFindDlg && IsDialogMessageW(g_hwndFindDlg, &msg))
            continue;
        if (!TranslateAcceleratorW(g_hwndMain, g_hAccel, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    return static_cast<int>(msg.wParam);
}
