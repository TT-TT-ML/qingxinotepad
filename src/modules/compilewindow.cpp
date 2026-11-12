/*
  Self-drawn compile result window.

  Two independent blocks: warnings (yellow) and errors (red). Each block is a
  read-only multiline EDIT control, so its text can be selected with the mouse
  or keyboard (Ctrl+C also works) and each block owns a 复制 button.
*/

#include "compilewindow.h"
#include "theme.h"
#include <windows.h>
#include <string>
#include <vector>

namespace
{

const wchar_t *kClassName = L"LegacyCompileResultWnd";
HWND g_compileWin = nullptr;

const int kIdCopyWarn = 1001;
const int kIdCopyErr = 1002;

const int kTopBand = 34;   // info strip above the two blocks
const int kHeadH = 30;     // block caption strip
const int kGap = 6;
const int kPad = 8;

struct CompileWinState
{
    HWND warnEdit = nullptr;
    HWND errEdit = nullptr;
    HWND warnBtn = nullptr;
    HWND errBtn = nullptr;
    HFONT monoFont = nullptr;
    HFONT uiFont = nullptr;

    int warnCount = 0;
    int errCount = 0;
    std::wstring infoLine;
    std::wstring warnText;
    std::wstring errText;

    RECT warnHead = {0, 0, 0, 0};
    RECT errHead = {0, 0, 0, 0};
};

CompileWinState *GetState(HWND h)
{
    return reinterpret_cast<CompileWinState *>(GetWindowLongPtrW(h, GWLP_USERDATA));
}

bool ContainsNoCase(const std::wstring &hay, const wchar_t *needle)
{
    if (!needle || !*needle)
        return false;
    std::wstring low = hay;
    for (wchar_t &c : low)
    {
        if (c >= L'A' && c <= L'Z')
            c = static_cast<wchar_t>(c - L'A' + L'a');
    }
    return low.find(needle) != std::wstring::npos;
}

// 0 = normal, 1 = warning, 2 = error. Prefix based, so the trailing summary
// line ("2 warnings, 2 errors") is not miscounted.
int ClassifyLine(const std::wstring &s)
{
    const bool err = ContainsNoCase(s, L"error c") || ContainsNoCase(s, L"error:") ||
                     ContainsNoCase(s, L"error lnk") || ContainsNoCase(s, L"fatal error") ||
                     s.find(L"错误") != std::wstring::npos;
    const bool warn = ContainsNoCase(s, L"warning c") || ContainsNoCase(s, L"warning:") ||
                      s.find(L"警告") != std::wstring::npos;
    if (err)
        return 2;
    if (warn)
        return 1;
    return 0;
}

void ClassifyOutput(CompileWinState *st, const std::wstring &text)
{
    st->warnCount = 0;
    st->errCount = 0;
    st->warnText.clear();
    st->errText.clear();
    st->infoLine.clear();

    size_t start = 0;
    while (start <= text.size())
    {
        size_t nl = text.find(L'\n', start);
        std::wstring line = text.substr(start, (nl == std::wstring::npos) ? std::wstring::npos : nl - start);
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();

        const int kind = ClassifyLine(line);
        if (kind == 2)
        {
            ++st->errCount;
            if (!st->errText.empty())
                st->errText += L"\r\n";
            st->errText += line;
        }
        else if (kind == 1)
        {
            ++st->warnCount;
            if (!st->warnText.empty())
                st->warnText += L"\r\n";
            st->warnText += line;
        }
        else if (!line.empty() && st->infoLine.empty())
        {
            // Keep only the first non-diagnostic line (the build target);
            // summaries and noise stay out of the strip.
            st->infoLine = line;
        }

        if (nl == std::wstring::npos)
            break;
        start = nl + 1;
    }
}

void CopyToClipboard(const std::wstring &text)
{
    if (!OpenClipboard(nullptr))
        return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem)
    {
        void *p = GlobalLock(mem);
        if (p)
        {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(mem);
            SetClipboardData(CF_UNICODETEXT, mem);
        }
    }
    CloseClipboard();
}

void SetFont(HWND child, HFONT font)
{
    if (child && font)
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void LayoutChildren(HWND hwnd, CompileWinState *st)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int left = kPad;
    const int right = rc.right - kPad;
    const int width = right - left;
    if (width <= 0 || rc.bottom <= 0)
        return;

    const int topY = kTopBand;
    const int totalH = rc.bottom - topY - kPad;
    int bodyAvail = totalH - 2 * kHeadH - kGap;
    if (bodyAvail < 40)
        bodyAvail = 40;

    int warnBody = bodyAvail / 2;
    int errBody = bodyAvail - warnBody;
    if (st->warnCount == 0)
    {
        warnBody = 44;
        errBody = bodyAvail - warnBody;
    }
    else if (st->errCount == 0)
    {
        errBody = 44;
        warnBody = bodyAvail - errBody;
    }
    if (warnBody < 44)
        warnBody = 44;
    if (errBody < 44)
        errBody = 44;

    int y = topY;
    st->warnHead = {left, y, right, y + kHeadH};
    y += kHeadH;
    if (st->warnEdit)
        MoveWindow(st->warnEdit, left, y, width, warnBody, TRUE);
    y += warnBody + kGap;

    st->errHead = {left, y, right, y + kHeadH};
    y += kHeadH;
    if (st->errEdit)
        MoveWindow(st->errEdit, left, y, width, errBody, TRUE);

    const int btnW = 74;
    if (st->warnBtn)
        MoveWindow(st->warnBtn, right - btnW - 4, st->warnHead.top + 3, btnW, kHeadH - 6, TRUE);
    if (st->errBtn)
        MoveWindow(st->errBtn, right - btnW - 4, st->errHead.top + 3, btnW, kHeadH - 6, TRUE);
}

void RefreshTexts(CompileWinState *st)
{
    if (st->warnEdit)
    {
        std::wstring t = st->warnCount > 0 ? st->warnText : L"（无警告）";
        SetWindowTextW(st->warnEdit, t.c_str());
        SendMessageW(st->warnEdit, EM_SETSEL, 0, 0);
    }
    if (st->errEdit)
    {
        std::wstring t = st->errCount > 0 ? st->errText : L"（无错误）";
        SetWindowTextW(st->errEdit, t.c_str());
        SendMessageW(st->errEdit, EM_SETSEL, 0, 0);
    }
    if (st->warnBtn)
        EnableWindow(st->warnBtn, st->warnCount > 0);
    if (st->errBtn)
        EnableWindow(st->errBtn, st->errCount > 0);
}

void PaintChrome(HWND hwnd, HDC dc, CompileWinState *st)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    const bool dark = IsDarkMode();
    COLORREF bg = dark ? RGB(32, 32, 32) : RGB(246, 246, 246);

    HBRUSH b = CreateSolidBrush(bg);
    FillRect(dc, &rc, b);
    DeleteObject(b);

    HFONT useFont = st->uiFont ? st->uiFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HFONT old = reinterpret_cast<HFONT>(SelectObject(dc, useFont));
    SetBkMode(dc, TRANSPARENT);

    // Info strip.
    SetTextColor(dc, dark ? RGB(210, 210, 210) : RGB(90, 90, 90));
    RECT info = {kPad, 6, rc.right - kPad, kTopBand - 4};
    DrawTextW(dc, st->infoLine.c_str(), -1, &info,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);

    struct Block
    {
        const RECT *head;
        int count;
        COLORREF accent;
        COLORREF band;
        const wchar_t *title;
    };
    const COLORREF warnAccent = dark ? RGB(214, 190, 70) : RGB(214, 180, 0);
    const COLORREF warnBand = dark ? RGB(88, 78, 26) : RGB(255, 248, 204);
    const COLORREF errAccent = dark ? RGB(220, 70, 70) : RGB(200, 0, 0);
    const COLORREF errBand = dark ? RGB(96, 32, 32) : RGB(255, 224, 224);

    const Block blocks[2] = {
        {&st->warnHead, st->warnCount, warnAccent, warnBand, L"警告"},
        {&st->errHead, st->errCount, errAccent, errBand, L"错误"},
    };

    for (const Block &blk : blocks)
    {
        const RECT &h = *blk.head;
        if (h.right <= h.left)
            continue;

        HBRUSH bb = CreateSolidBrush(blk.band);
        FillRect(dc, &h, bb);
        DeleteObject(bb);

        HBRUSH ab = CreateSolidBrush(blk.accent);
        RECT bar = {h.left, h.top, h.left + 4, h.bottom};
        FillRect(dc, &bar, ab);
        DeleteObject(ab);

        std::wstring label = blk.title;
        label += L"（";
        label += std::to_wstring(blk.count);
        label += L"）";
        SetTextColor(dc, dark ? RGB(235, 235, 235) : RGB(30, 30, 30));
        RECT lr = {h.left + 12, h.top, h.right - 90, h.bottom};
        DrawTextW(dc, label.c_str(), -1, &lr,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    SelectObject(dc, old);
}

HBRUSH SectionBrush(HWND hwnd, CompileWinState *st)
{
    static HBRUSH s_warnBrush = nullptr;
    static HBRUSH s_errBrush = nullptr;
    static bool s_dark = false;
    const bool dark = IsDarkMode();
    if (s_dark != dark || !s_warnBrush || !s_errBrush)
    {
        if (s_warnBrush)
        {
            DeleteObject(s_warnBrush);
            s_warnBrush = nullptr;
        }
        if (s_errBrush)
        {
            DeleteObject(s_errBrush);
            s_errBrush = nullptr;
        }
        s_warnBrush = CreateSolidBrush(dark ? RGB(88, 78, 26) : RGB(255, 248, 204));
        s_errBrush = CreateSolidBrush(dark ? RGB(96, 32, 32) : RGB(255, 224, 224));
        s_dark = dark;
    }
    if (hwnd == st->warnEdit)
        return s_warnBrush;
    if (hwnd == st->errEdit)
        return s_errBrush;
    return nullptr;
}

void BuildTitle(CompileWinState *st, bool success, std::wstring &title)
{
    title = success ? L"编译完成" : L"编译失败";
    if (st->errCount == 0 && st->warnCount == 0)
        return;
    title += L"  ·";
    if (st->errCount > 0)
        title += L"  " + std::to_wstring(st->errCount) + L" 个错误";
    if (st->warnCount > 0)
        title += L"  " + std::to_wstring(st->warnCount) + L" 个警告";
}

LRESULT CALLBACK CompileWinProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        CompileWinState *st = new CompileWinState();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

        LOGFONTW lf = {};
        lf.lfHeight = -16;
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
        lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
        const wchar_t *face = L"Consolas";
        for (int i = 0; face[i] != L'\0' && i < LF_FACESIZE - 1; ++i)
            lf.lfFaceName[i] = face[i];
        lf.lfFaceName[LF_FACESIZE - 1] = L'\0';
        st->monoFont = CreateFontIndirectW(&lf);

        LOGFONTW uf = {};
        uf.lfHeight = -15;
        uf.lfWeight = FW_NORMAL;
        uf.lfCharSet = DEFAULT_CHARSET;
        uf.lfQuality = CLEARTYPE_QUALITY;
        const wchar_t *uiFace = L"Microsoft YaHei UI";
        for (int i = 0; uiFace[i] != L'\0' && i < LF_FACESIZE - 1; ++i)
            uf.lfFaceName[i] = uiFace[i];
        uf.lfFaceName[LF_FACESIZE - 1] = L'\0';
        st->uiFont = CreateFontIndirectW(&uf);

        st->warnEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                       WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
                                           ES_AUTOVSCROLL | ES_NOHIDESEL,
                                       0, 0, 10, 10, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        st->errEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                      WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
                                          ES_AUTOVSCROLL | ES_NOHIDESEL,
                                      0, 0, 10, 10, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        st->warnBtn = CreateWindowExW(0, L"BUTTON", L"复制", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                     0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdCopyWarn)),
                                     GetModuleHandleW(nullptr), nullptr);
        st->errBtn = CreateWindowExW(0, L"BUTTON", L"复制", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                     0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdCopyErr)),
                                     GetModuleHandleW(nullptr), nullptr);

        SetFont(st->warnEdit, st->monoFont);
        SetFont(st->errEdit, st->monoFont);
        SetFont(st->warnBtn, st->uiFont);
        SetFont(st->errBtn, st->uiFont);
        return 0;
    }
    case WM_SIZE:
    {
        CompileWinState *st = GetState(hwnd);
        if (st)
            LayoutChildren(hwnd, st);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        CompileWinState *st = GetState(hwnd);
        if (st)
            PaintChrome(hwnd, dc, st);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    {
        CompileWinState *st = GetState(hwnd);
        if (!st)
            break;
        HWND child = reinterpret_cast<HWND>(lParam);
        HBRUSH brush = SectionBrush(child, st);
        if (!brush)
            break;
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, IsDarkMode() ? RGB(88, 78, 26) : RGB(255, 248, 204));
        if (child == st->errEdit)
            SetBkColor(dc, IsDarkMode() ? RGB(96, 32, 32) : RGB(255, 224, 224));
        SetTextColor(dc, IsDarkMode() ? RGB(232, 232, 232) : RGB(20, 20, 20));
        return reinterpret_cast<LRESULT>(brush);
    }
    case WM_COMMAND:
    {
        CompileWinState *st = GetState(hwnd);
        if (!st)
            break;
        const int id = LOWORD(wParam);
        if (HIWORD(wParam) == BN_CLICKED && id == kIdCopyWarn)
        {
            CopyToClipboard(st->warnText);
            return 0;
        }
        if (HIWORD(wParam) == BN_CLICKED && id == kIdCopyErr)
        {
            CopyToClipboard(st->errText);
            return 0;
        }
        break;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_DESTROY:
    {
        CompileWinState *st = GetState(hwnd);
        if (st)
        {
            if (st->monoFont)
                DeleteObject(st->monoFont);
            if (st->uiFont)
                DeleteObject(st->uiFont);
            delete st;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        g_compileWin = nullptr;
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void EnsureClass()
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = CompileWinProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
}

} // namespace

void CloseCompileResult()
{
    if (g_compileWin && IsWindow(g_compileWin))
        DestroyWindow(g_compileWin);
    g_compileWin = nullptr;
}

void ShowCompileResult(HWND parent, const std::wstring &output, bool success)
{
    EnsureClass();

    if (!(g_compileWin && IsWindow(g_compileWin)))
    {
        RECT prc = {0, 0, 0, 0};
        if (parent)
            GetWindowRect(parent, &prc);
        int w = 900;
        int h = 640;
        int x = prc.left + ((prc.right - prc.left) - w) / 2;
        int y = prc.top + ((prc.bottom - prc.top) - h) / 2;
        if (x < 0)
            x = 40;
        if (y < 0)
            y = 40;

        g_compileWin = CreateWindowExW(WS_EX_DLGMODALFRAME, kClassName, L"",
                                       WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                       x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!g_compileWin)
            return;
    }

    CompileWinState *st = GetState(g_compileWin);
    if (!st)
        return;

    ClassifyOutput(st, output);
    RefreshTexts(st);
    LayoutChildren(g_compileWin, st);

    std::wstring title;
    BuildTitle(st, success, title);
    SetWindowTextW(g_compileWin, title.c_str());

    InvalidateRect(g_compileWin, nullptr, TRUE);
    ShowWindow(g_compileWin, SW_SHOW);
    SetForegroundWindow(g_compileWin);
}
