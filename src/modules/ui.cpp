
#include "ui.h"
#include "core/globals.h"
#include "lang/lang.h"
#include "editor.h"
#include "file.h"
#include "tabs.h"
#include <commctrl.h>
#include <shlwapi.h>

void UpdateTitle()
{
    const auto &lang = GetLangStrings();
    std::wstring filename = g_state.filePath.empty() ? lang.untitled : PathFindFileNameW(g_state.filePath.c_str());
    std::wstring title = (g_state.modified ? L"*" : L"") + filename + L" - " + lang.appName;
    SetWindowTextW(g_hwndMain, title.c_str());
}

void UpdateStatus()
{
    if (!g_state.showStatusBar)
    {
        ShowWindow(g_hwndStatus, SW_HIDE);
        return;
    }
    ShowWindow(g_hwndStatus, SW_SHOW);
    auto [line, col] = GetCursorPos();
    const auto &lang = GetLangStrings();
    wchar_t buf[256];
    swprintf(buf, sizeof(buf) / sizeof(wchar_t), L"%s%d%s%d ", lang.statusLn.c_str(), line, lang.statusCol.c_str(), col);
    g_statusTexts[0] = buf;
    g_statusTexts[1] = GetEncodingName(g_state.encoding);
    g_statusTexts[2] = GetLineEndingName(g_state.lineEnding);
    wsprintfW(buf, L" %d%% ", g_state.zoomLevel);
    g_statusTexts[3] = buf;
    g_statusTexts[4] = std::wstring(L" ") + EditorLanguageName() + L" ";
    for (int i = 0; i < 5; i++)
        SendMessageW(g_hwndStatus, SB_SETTEXTW, i | SBT_NOBORDERS, reinterpret_cast<LPARAM>(g_statusTexts[i].c_str()));
    InvalidateRect(g_hwndStatus, nullptr, TRUE);
}

void SetupStatusBarParts()
{
    RECT rc;
    GetClientRect(g_hwndMain, &rc);
    HDC hdc = GetDC(g_hwndStatus);
    HFONT hFont = reinterpret_cast<HFONT>(SendMessageW(g_hwndStatus, WM_GETFONT, 0, 0));
    HFONT old = reinterpret_cast<HFONT>(SelectObject(hdc, hFont ? hFont : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT))));
    auto textW = [&](const wchar_t *s)
    {
        SIZE sz{};
        GetTextExtentPoint32W(hdc, s, static_cast<int>(wcslen(s)), &sz);
        return sz.cx + 24;
    };
    int wZoom = textW(L" 500% ");
    int wLE = textW(L" Windows (CRLF) ");
    int wEnc = textW(L" UTF-8 with BOM ");
    std::wstring langText = std::wstring(L" ") + EditorLanguageName() + L" ";
    int wLang = textW(langText.c_str());
    SelectObject(hdc, old);
    ReleaseDC(g_hwndStatus, hdc);
    int w = rc.right;
    int parts[5] = {w - (wEnc + wLE + wZoom + wLang),
                    w - (wLE + wZoom + wLang),
                    w - (wZoom + wLang),
                    w - wLang,
                    -1};
    SendMessageW(g_hwndStatus, SB_SETPARTS, 5, reinterpret_cast<LPARAM>(parts));
}

void ResizeControls()
{
    RECT rc;
    GetClientRect(g_hwndMain, &rc);
    int statusH = 0;
    if (g_state.showStatusBar)
    {
        ShowWindow(g_hwndStatus, SW_SHOW);
        SendMessageW(g_hwndStatus, WM_SIZE, 0, 0);
        RECT rs;
        GetWindowRect(g_hwndStatus, &rs);
        statusH = rs.bottom - rs.top;
    }
    else
        ShowWindow(g_hwndStatus, SW_HIDE);
    int tabH = TabsBarHeight();
    TabsMoveBar(0, 0, rc.right, tabH);
    MoveWindow(g_hwndEditor, 0, tabH, rc.right, rc.bottom - statusH - tabH, TRUE);
    SetupStatusBarParts();
}
