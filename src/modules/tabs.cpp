/*
  Implementation of the multi-document tab strip (buffer-swap model).
*/

#include "tabs.h"
#include "editor.h"
#include "file.h"
#include "commands.h"
#include "ui.h"
#include "theme.h"
#include "lang/lang.h"
#include "core/globals.h"
#include <windows.h>
#include <windowsx.h>
#include <shlwapi.h>
#include <vector>
#include <string>

namespace
{

struct DocTab
{
    std::wstring path;
    std::wstring text;
    bool modified;
    Encoding enc;
    LineEnding le;
};

const wchar_t *kTabClass = L"LegacyTabBar";
const int kBarH = 32;

std::vector<DocTab> g_tabs;
int g_active = -1;
HWND g_tabBar = nullptr;
HFONT g_tabFont = nullptr;
int g_hover = -1;

void TabLabel(const DocTab &t, std::wstring &out)
{
    std::wstring name = t.path.empty() ? GetLangStrings().untitled
                                       : PathFindFileNameW(t.path.c_str());
    if (name.size() > 5)
        out = name.substr(0, 5) + L"...";
    else
        out = name;
}

int TabWidth(HDC dc, const DocTab &t)
{
    std::wstring label;
    TabLabel(t, label);
    SIZE sz = {};
    GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &sz);
    return 12 + 10 + 6 + sz.cx + 26;
}

void PaintBar(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    const bool dark = IsDarkMode();
    COLORREF barBg = dark ? RGB(32, 32, 32) : RGB(246, 246, 246);
    HBRUSH bb = CreateSolidBrush(barBg);
    FillRect(dc, &rc, bb);
    DeleteObject(bb);

    HBRUSH lineBrush = CreateSolidBrush(dark ? RGB(70, 70, 70) : RGB(208, 208, 208));
    RECT line = {0, rc.bottom - 1, rc.right, rc.bottom};
    FillRect(dc, &line, lineBrush);
    DeleteObject(lineBrush);

    HFONT useFont = g_tabFont ? g_tabFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HFONT old = reinterpret_cast<HFONT>(SelectObject(dc, useFont));
    SetBkMode(dc, TRANSPARENT);

    int x = 6;
    for (size_t i = 0; i < g_tabs.size(); ++i)
    {
        const DocTab &t = g_tabs[i];
        int w = TabWidth(dc, t);
        RECT tr = {x, 4, x + w, rc.bottom - 4};
        if (tr.right > rc.right)
            break;

        const bool activeTab = (static_cast<int>(i) == g_active);
        COLORREF fill;
        COLORREF textCol;
        if (activeTab)
        {
            fill = RGB(0, 120, 215);
            textCol = RGB(255, 255, 255);
        }
        else
        {
            fill = dark ? RGB(48, 48, 48) : RGB(232, 232, 232);
            textCol = dark ? RGB(210, 210, 210) : RGB(40, 40, 40);
        }
        if (!activeTab && static_cast<int>(i) == g_hover)
            fill = dark ? RGB(62, 62, 62) : RGB(222, 222, 222);

        HBRUSH fb = CreateSolidBrush(fill);
        HGDIOBJ oldBrush = SelectObject(dc, fb);
        HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, tr.left, tr.top, tr.right, tr.bottom, 8, 8);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(fb);

        // Saved-state dot: green = saved on disk, orange = unsaved/untitled.
        const bool unsaved = t.modified || t.path.empty();
        COLORREF dot = unsaved ? RGB(235, 140, 0) : RGB(46, 170, 78);
        HBRUSH db = CreateSolidBrush(dot);
        HGDIOBJ ob = SelectObject(dc, db);
        int cx = tr.left + 12;
        int cy = (tr.top + tr.bottom) / 2;
        Ellipse(dc, cx - 4, cy - 4, cx + 4, cy + 4);
        SelectObject(dc, ob);
        DeleteObject(db);

        std::wstring label;
        TabLabel(t, label);
        SetTextColor(dc, textCol);
        RECT lr = {cx + 8, tr.top, tr.right - 22, tr.bottom};
        DrawTextW(dc, label.c_str(), -1, &lr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);

        SetTextColor(dc, activeTab ? RGB(235, 235, 235) : (dark ? RGB(150, 150, 150) : RGB(110, 110, 110)));
        RECT cr = {tr.right - 20, tr.top, tr.right - 4, tr.bottom};
        DrawTextW(dc, L"\u00D7", -1, &cr, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);

        x += w + 4;
    }

    SelectObject(dc, old);
    EndPaint(hwnd, &ps);
}

int HitTest(HWND hwnd, int mx, bool &onClose)
{
    onClose = false;
    HDC dc = GetDC(hwnd);
    HFONT useFont = g_tabFont ? g_tabFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HFONT old = reinterpret_cast<HFONT>(SelectObject(dc, useFont));
    int x = 6;
    int hit = -1;
    for (size_t i = 0; i < g_tabs.size(); ++i)
    {
        int w = TabWidth(dc, g_tabs[i]);
        if (mx >= x && mx < x + w)
        {
            hit = static_cast<int>(i);
            onClose = (mx >= x + w - 22);
            break;
        }
        x += w + 4;
    }
    SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
    return hit;
}

LRESULT CALLBACK TabBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintBar(hwnd);
        return 0;
    case WM_MOUSEMOVE:
    {
        int mx = GET_X_LPARAM(lParam);
        bool dummy = false;
        int hit = HitTest(hwnd, mx, dummy);
        if (hit != g_hover)
        {
            g_hover = hit;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        int mx = GET_X_LPARAM(lParam);
        bool onClose = false;
        int hit = HitTest(hwnd, mx, onClose);
        if (hit >= 0)
        {
            if (onClose)
                TabsClose(hit);
            else
                TabsSwitchTo(hit);
        }
        return 0;
    }
    case WM_MBUTTONDOWN:
    {
        bool onClose = false;
        int hit = HitTest(hwnd, GET_X_LPARAM(lParam), onClose);
        if (hit >= 0)
            TabsClose(hit);
        return 0;
    }
    case WM_THEMECHANGED:
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_DESTROY:
        g_tabBar = nullptr;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ApplyActiveToEditor()
{
    if (g_active < 0 || g_active >= static_cast<int>(g_tabs.size()))
        return;
    DocTab &t = g_tabs[g_active];
    SetEditorText(t.text);
    EditorSetLanguageFromPath(t.path);
    g_state.filePath = t.path;
    g_state.encoding = t.enc;
    g_state.lineEnding = t.le;
    g_state.modified = t.modified;
    UpdateTitle();
    UpdateStatus();
    UpdateEncodingMenuChecks();
    if (g_tabBar)
        InvalidateRect(g_tabBar, nullptr, TRUE);
}

} // namespace

void TabsInit()
{
    g_tabs.clear();
    DocTab t;
    t.modified = false;
    t.enc = Encoding::UTF8;
    t.le = LineEnding::CRLF;
    g_tabs.push_back(t);
    g_active = 0;
}

HWND TabsCreateBar(HWND parent)
{
    static bool registered = false;
    if (!registered)
    {
        registered = true;
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = TabBarProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kTabClass;
        RegisterClassExW(&wc);
    }
    if (!g_tabFont)
    {
        LOGFONTW lf = {};
        lf.lfHeight = -14;
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
        const wchar_t *face = L"Microsoft YaHei UI";
        for (int i = 0; face[i] != L'\0' && i < LF_FACESIZE - 1; ++i)
            lf.lfFaceName[i] = face[i];
        lf.lfFaceName[LF_FACESIZE - 1] = L'\0';
        g_tabFont = CreateFontIndirectW(&lf);
    }
    g_tabBar = CreateWindowExW(0, kTabClass, nullptr, WS_CHILD | WS_VISIBLE,
                               0, 0, 0, kBarH, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    return g_tabBar;
}

void TabsMoveBar(int x, int y, int w, int h)
{
    if (g_tabBar)
        MoveWindow(g_tabBar, x, y, w, h, TRUE);
}

int TabsBarHeight()
{
    return g_tabBar ? kBarH : 0;
}

int TabsActiveIndex()
{
    return g_active;
}

int TabsCount()
{
    return static_cast<int>(g_tabs.size());
}

void TabsSyncActive()
{
    if (g_active < 0 || g_active >= static_cast<int>(g_tabs.size()))
        return;
    DocTab &t = g_tabs[g_active];
    t.text = GetEditorText();
    t.modified = g_state.modified;
    t.enc = g_state.encoding;
    t.le = g_state.lineEnding;
}

void TabsSwitchTo(int index)
{
    if (index < 0 || index >= static_cast<int>(g_tabs.size()) || index == g_active)
        return;
    TabsSyncActive();
    g_active = index;
    ApplyActiveToEditor();
}

void TabsAddDocument(const std::wstring &path, const std::wstring &text,
                     Encoding enc, LineEnding le, bool modified, bool activate)
{
    int existing = -1;
    if (!path.empty() && TabsFindPath(path, existing))
    {
        if (activate)
            TabsSwitchTo(existing);
        return;
    }
    if (g_active >= 0)
        TabsSyncActive();

    // Opening a real file into a pristine "Untitled" tab reuses it instead
    // of leaving an empty tab behind.
    if (activate && g_active >= 0 && !path.empty())
    {
        DocTab &cur = g_tabs[g_active];
        if (cur.path.empty() && cur.text.empty() && !cur.modified)
        {
            cur.path = path;
            cur.text = text;
            cur.modified = modified;
            cur.enc = enc;
            cur.le = le;
            ApplyActiveToEditor();
            return;
        }
    }

    DocTab t;
    t.path = path;
    t.text = text;
    t.modified = modified;
    t.enc = enc;
    t.le = le;
    g_tabs.push_back(t);

    if (activate)
    {
        g_active = static_cast<int>(g_tabs.size()) - 1;
        ApplyActiveToEditor();
    }
    else if (g_tabBar)
    {
        InvalidateRect(g_tabBar, nullptr, TRUE);
    }
}

bool TabsFindPath(const std::wstring &path, int &index)
{
    for (size_t i = 0; i < g_tabs.size(); ++i)
    {
        if (!g_tabs[i].path.empty() &&
            CompareStringOrdinal(g_tabs[i].path.c_str(), -1, path.c_str(), -1, TRUE) == CSTR_EQUAL)
        {
            index = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

void TabsClose(int index)
{
    if (index < 0 || index >= static_cast<int>(g_tabs.size()))
        return;
    if (index == g_active)
        TabsSyncActive();

    int oldActive = g_active;

    DocTab &t = g_tabs[index];
    if (t.modified && !(t.path.empty() && t.text.empty()))
    {
        std::wstring name = t.path.empty() ? GetLangStrings().untitled : PathFindFileNameW(t.path.c_str());
        std::wstring msg = L"文件 " + name + L" 尚未保存，是否保存？";
        int r = MessageBoxW(g_hwndMain, msg.c_str(), GetLangStrings().appName.c_str(), MB_YESNOCANCEL | MB_ICONWARNING);
        if (r == IDCANCEL)
            return;
        if (r == IDYES)
        {
            TabsSwitchTo(index);
            FileSave();
            if (g_state.modified)
                return; // save was cancelled
        }
    }

    g_tabs.erase(g_tabs.begin() + index);
    if (g_tabs.empty())
    {
        DocTab nt;
        nt.modified = false;
        nt.enc = Encoding::UTF8;
        nt.le = LineEnding::CRLF;
        g_tabs.push_back(nt);
        g_active = 0;
    }
    else if (oldActive > index)
        g_active = oldActive - 1;
    else if (oldActive == index)
        g_active = (index < static_cast<int>(g_tabs.size())) ? index : static_cast<int>(g_tabs.size()) - 1;

    ApplyActiveToEditor();
}

void TabsMarkActiveModified()
{
    if (g_active >= 0 && g_active < static_cast<int>(g_tabs.size()))
    {
        g_tabs[g_active].modified = true;
        if (g_tabBar)
            InvalidateRect(g_tabBar, nullptr, FALSE);
    }
}

void TabsNotifySaved()
{
    if (g_active < 0 || g_active >= static_cast<int>(g_tabs.size()))
        return;
    DocTab &t = g_tabs[g_active];
    t.path = g_state.filePath;
    t.modified = false;
    t.enc = g_state.encoding;
    t.le = g_state.lineEnding;
    if (g_tabBar)
        InvalidateRect(g_tabBar, nullptr, FALSE);
}

void TabsRefresh()
{
    if (g_tabBar)
        InvalidateRect(g_tabBar, nullptr, TRUE);
}

bool TabsConfirmExit()
{
    TabsSyncActive();
    int dirty = 0;
    for (const DocTab &t : g_tabs)
    {
        if (t.modified && !(t.path.empty() && t.text.empty()))
            ++dirty;
    }
    if (dirty == 0)
        return true;

    wchar_t msg[256];
    wsprintfW(msg, L"有 %d 个文件尚未保存。\n是否保存所有更改后退出？", dirty);
    int r = MessageBoxW(g_hwndMain, msg, GetLangStrings().appName.c_str(), MB_YESNOCANCEL | MB_ICONWARNING);
    if (r == IDCANCEL)
        return false;
    if (r == IDNO)
        return true;
    if (r == IDYES)
    {
        for (size_t i = 0; i < g_tabs.size(); ++i)
        {
            if (!g_tabs[i].modified || (g_tabs[i].path.empty() && g_tabs[i].text.empty()))
                continue;
            TabsSwitchTo(static_cast<int>(i));
            FileSave();
            if (g_state.modified)
                return false;
        }
    }
    return true;
}
