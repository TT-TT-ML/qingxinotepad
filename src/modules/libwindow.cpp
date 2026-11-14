/*
  Library browser window.

  Left side of the job: keep it usable without blocking the UI, so every network
  step (source probing, list fetching, downloading) runs on its own thread and
  reports back through WM_APP messages.
*/

#include "libwindow.h"
#include "libcatalog.h"
#include "libsource.h"
#include "netlib.h"
#include "theme.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

const wchar_t *kClassName = L"LegacyLibWnd";
const wchar_t *kListClass = L"LegacyLibList";
HWND g_libWin = nullptr;

const int kIdRadCpp = 2001;
const int kIdRadPy = 2002;
const int kIdProbe = 2003;
const int kIdSearch = 2004;
const int kIdList = 2005;
const int kIdDownload = 2006;
const int kIdOpenDir = 2007;
const UINT kEmSetCueBanner = 0x1501;

const UINT kMsgList = WM_APP + 100;      // wParam = lang, result in state
const UINT kMsgStatus = WM_APP + 101;    // lParam = new wchar_t[]
const UINT kMsgDone = WM_APP + 102;      // wParam = ok, lParam = new wchar_t[]
const UINT kMsgProgress = WM_APP + 103;  // wParam = percent, lParam = bytes
const UINT kMsgSelChanged = WM_APP + 110;
const UINT kMsgWantDownload = WM_APP + 111;
const UINT kMsgInfo = WM_APP + 104;      // lParam = InfoResult*
const UINT kMsgInfoDone = WM_APP + 105;

struct InfoResult
{
    int langIdx;
    size_t itemIdx;
    LibEntry entry;
    std::shared_ptr<std::atomic<bool>> alive;
};

const int kPad = 12;
const int kRow1Y = 10;
const int kRow1H = 30;
const int kRow2Y = 46;
const int kListY = 84;
const int kFootH = 46;
const int kRowH = 48;

struct LibState
{
    HWND hwnd = nullptr;
    HWND hList = nullptr;
    HWND hSearch = nullptr;
    HWND hRadCpp = nullptr;
    HWND hRadPy = nullptr;
    HWND hBtnProbe = nullptr;
    HWND hBtnDown = nullptr;
    HWND hBtnOpen = nullptr;
    HFONT uiFont = nullptr;
    HFONT boldFont = nullptr;
    HFONT smallFont = nullptr;
    HBRUSH bgBrush = nullptr;

    LibLang lang = LibLang::Cpp;
    std::vector<LibEntry> items[2];
    bool loaded[2] = {false, false};
    std::vector<long> latencies[2];
    std::vector<size_t> view;
    int sel = -1;
    int top = 0;
    int downloading = -1;

    std::wstring status;
    std::wstring sourceText;
};

std::shared_ptr<std::atomic<bool>> g_alive;

void LogLib(const std::wstring &line)
{
    std::wstring path = LibCacheDir() + L"\\lib_debug.log";
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t ts[64] = {};
    swprintf_s(ts, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    std::wstring s = std::wstring(ts) + line + L"\r\n";
    std::string a;
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    a.resize(static_cast<size_t>(len));
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), a.data(), len, nullptr, nullptr);
    DWORD got = 0;
    WriteFile(h, a.data(), static_cast<DWORD>(a.size()), &got, nullptr);
    CloseHandle(h);
}

LibState *GetState(HWND h)
{
    return reinterpret_cast<LibState *>(GetWindowLongPtrW(h, GWLP_USERDATA));
}

const std::vector<LibEntry> &ItemsOf(const LibState *st)
{
    return st->items[static_cast<int>(st->lang)];
}

std::wstring BaseOf(LibLang lang)
{
    const std::vector<LibSource> &srcs = AllSources(lang);
    int idx = LibSelectedSource(lang);
    if (idx < 0 || static_cast<size_t>(idx) >= srcs.size())
        return std::wstring();
    return srcs[static_cast<size_t>(idx)].base;
}

std::wstring FormatSize(unsigned long long bytes)
{
    wchar_t buf[64] = {};
    if (bytes >= 1024ull * 1024ull)
        swprintf_s(buf, L"%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    else if (bytes >= 1024ull)
        swprintf_s(buf, L"%.0f KB", static_cast<double>(bytes) / 1024.0);
    else
        swprintf_s(buf, L"%llu B", bytes);
    return std::wstring(buf);
}

std::wstring LowerOf(const std::wstring &s)
{
    std::wstring r = s;
    for (wchar_t &c : r)
        if (c >= L'A' && c <= L'Z')
            c = static_cast<wchar_t>(c - L'A' + L'a');
    return r;
}

void SetStatus(LibState *st, const std::wstring &text)
{
    if (!st)
        return;
    st->status = text;
    if (st->hwnd)
        InvalidateRect(st->hwnd, nullptr, FALSE);
}

void PostStatus(HWND target, const std::wstring &text)
{
    auto *p = new std::wstring(text);
    if (!PostMessageW(target, kMsgStatus, 0, reinterpret_cast<LPARAM>(p)))
        delete p;
}

void LayoutChildren(HWND hwnd, LibState *st)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int right = rc.right - kPad;

    if (st->hRadCpp)
        MoveWindow(st->hRadCpp, kPad, kRow1Y + 6, 62, 22, TRUE);
    if (st->hRadPy)
        MoveWindow(st->hRadPy, kPad + 66, kRow1Y + 6, 74, 22, TRUE);
    if (st->hBtnProbe)
        MoveWindow(st->hBtnProbe, right - 92, kRow1Y + 1, 92, 28, TRUE);

    if (st->hSearch)
    {
        int w = right - kPad - 44 - 230;
        MoveWindow(st->hSearch, kPad + 44, kRow2Y + 1, w, 28, TRUE);
    }

    const int footY = rc.bottom - kFootH;
    if (st->hList)
        MoveWindow(st->hList, kPad, kListY, right - kPad, footY - kListY - 8, TRUE);

    int bx = right - 96;
    if (st->hBtnOpen)
        MoveWindow(st->hBtnOpen, bx, footY + 10, 96, 28, TRUE);
    bx -= 8 + 96;
    if (st->hBtnDown)
        MoveWindow(st->hBtnDown, bx, footY + 10, 96, 28, TRUE);

    (void)rc;
}

void RebuildView(LibState *st)
{
    st->view.clear();
    const std::vector<LibEntry> &items = ItemsOf(st);

    std::wstring query;
    if (st->hSearch)
    {
        wchar_t buf[256] = {};
        GetWindowTextW(st->hSearch, buf, 256);
        query = LowerOf(buf);
    }

    std::vector<std::pair<int, size_t>> ranked;
    ranked.reserve(items.size());
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!query.empty())
        {
            const std::wstring name = LowerOf(items[i].name);
            if (name.find(query) == std::wstring::npos)
                continue;
            // Exact and prefix hits first: searching "requests" should not bury
            // the package actually called requests under longer look-alikes.
            int score = 2;
            if (name == query)
                score = 0;
            else if (name.compare(0, query.size(), query) == 0)
                score = 1;
            ranked.emplace_back(score, i);
        }
        else
        {
            ranked.emplace_back(0, i);
        }
    }
    if (!query.empty())
        std::stable_sort(ranked.begin(), ranked.end(),
                         [](const std::pair<int, size_t> &a, const std::pair<int, size_t> &b) {
                             return a.first < b.first;
                         });
    for (const auto &r : ranked)
        st->view.push_back(r.second);
    if (st->sel >= static_cast<int>(st->view.size()))
        st->sel = static_cast<int>(st->view.size()) - 1;
    if (st->sel < 0 && !st->view.empty())
        st->sel = 0;
    st->top = 0;
    if (st->hList)
    {
        int page = 1;
        RECT lr;
        GetClientRect(st->hList, &lr);
        if (lr.bottom > 0)
            page = (lr.bottom + kRowH - 1) / kRowH;
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0;
        si.nMax = static_cast<int>(st->view.size()) - 1;
        si.nPage = static_cast<UINT>(page > 0 ? page : 1);
        si.nPos = st->top;
        SetScrollInfo(st->hList, SB_VERT, &si, TRUE);
        InvalidateRect(st->hList, nullptr, TRUE);
    }
    if (st->hwnd)
        InvalidateRect(st->hwnd, nullptr, FALSE);
}

// ------------------------------------------------------------- list control

void ListPaint(HWND hList, LibState *st, HDC dc)
{
    RECT rc;
    GetClientRect(hList, &rc);
    const bool dark = IsDarkMode();
    const COLORREF bg = dark ? RGB(30, 30, 30) : RGB(255, 255, 255);

    HBRUSH brush = CreateSolidBrush(bg);
    FillRect(dc, &rc, brush);
    DeleteObject(brush);

    HFONT old = reinterpret_cast<HFONT>(SelectObject(dc, st->uiFont ? st->uiFont : GetStockObject(DEFAULT_GUI_FONT)));
    SetBkMode(dc, TRANSPARENT);

    const int rows = (rc.bottom + kRowH - 1) / kRowH + 1;
    for (int r = 0; r < rows; ++r)
    {
        const int idx = st->top + r;
        if (idx >= static_cast<int>(st->view.size()))
            break;
        const LibEntry &e = ItemsOf(st)[st->view[static_cast<size_t>(idx)]];
        const int y = r * kRowH;
        RECT row = {0, y, rc.right, y + kRowH};

        if (idx == st->sel)
        {
            HBRUSH selBrush = CreateSolidBrush(dark ? RGB(0, 90, 158) : RGB(0, 120, 215));
            FillRect(dc, &row, selBrush);
            DeleteObject(selBrush);
        }
        else
        {
            RECT line = {0, y + kRowH - 1, rc.right, y + kRowH};
            HBRUSH lb = CreateSolidBrush(dark ? RGB(48, 48, 48) : RGB(232, 232, 232));
            FillRect(dc, &line, lb);
            DeleteObject(lb);
        }

        const COLORREF fg = (idx == st->sel) ? RGB(255, 255, 255) : (dark ? RGB(226, 226, 226) : RGB(28, 28, 28));
        const COLORREF sub = (idx == st->sel) ? RGB(220, 235, 250) : (dark ? RGB(160, 160, 160) : RGB(110, 110, 110));

        HFONT bf = st->boldFont ? st->boldFont : st->uiFont;
        HFONT prev = reinterpret_cast<HFONT>(SelectObject(dc, bf));
        SetTextColor(dc, fg);
        RECT nr = {12, y + 5, rc.right - 120, y + 25};
        DrawTextW(dc, e.name.c_str(), -1, &nr, DT_SINGLELINE | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);

        SelectObject(dc, st->smallFont ? st->smallFont : st->uiFont);
        SetTextColor(dc, sub);
        std::wstring right = e.version.empty() ? L"" : e.version;
        RECT vr = {rc.right - 130, y + 7, rc.right - 12, y + 25};
        DrawTextW(dc, right.c_str(), -1, &vr, DT_SINGLELINE | DT_RIGHT | DT_NOPREFIX | DT_END_ELLIPSIS);

        RECT sr = {12, y + 25, rc.right - 120, y + 44};
        const std::wstring sub2 = e.summary.empty() ? (st->lang == LibLang::Cpp ? L"vcpkg 端口" : L"PyPI 包") : e.summary;
        DrawTextW(dc, sub2.c_str(), -1, &sr, DT_SINGLELINE | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);

        SelectObject(dc, prev);
    }

    if (st->view.empty())
    {
        SetTextColor(dc, dark ? RGB(160, 160, 160) : RGB(120, 120, 120));
        RECT er = {0, 20, rc.right, 60};
        DrawTextW(dc, st->items[static_cast<int>(st->lang)].empty() ? L"正在获取库列表…" : L"没有匹配的库",
                  -1, &er, DT_SINGLELINE | DT_CENTER | DT_NOPREFIX);
    }
    SelectObject(dc, old);
}

void ListScroll(LibState *st, int cmd)
{
    RECT lr;
    GetClientRect(st->hList, &lr);
    const int page = (lr.bottom > 0) ? (lr.bottom / kRowH) : 1;
    const int count = static_cast<int>(st->view.size());
    switch (cmd)
    {
    case SB_LINEUP:
        st->top -= 1;
        break;
    case SB_LINEDOWN:
        st->top += 1;
        break;
    case SB_PAGEUP:
        st->top -= page;
        break;
    case SB_PAGEDOWN:
        st->top += page;
        break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
    {
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask = SIF_TRACKPOS;
        GetScrollInfo(st->hList, SB_VERT, &si);
        st->top = si.nTrackPos;
        break;
    }
    default:
        break;
    }
    if (st->top > count - page)
        st->top = count - page;
    if (st->top < 0)
        st->top = 0;
    SetScrollPos(st->hList, SB_VERT, st->top, TRUE);
    InvalidateRect(st->hList, nullptr, TRUE);
}

LRESULT CALLBACK ListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LibState *st = GetState(hwnd);
    switch (msg)
    {
    case WM_CREATE:
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (st)
            ListPaint(hwnd, st, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        if (st)
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            const int page = (rc.bottom > 0) ? (rc.bottom / kRowH) : 1;
            SCROLLINFO si = {};
            si.cbSize = sizeof(si);
            si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
            si.nMin = 0;
            si.nMax = static_cast<int>(st->view.size()) - 1;
            si.nPage = static_cast<UINT>(page > 0 ? page : 1);
            si.nPos = st->top;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        }
        return 0;
    case WM_VSCROLL:
        if (st)
            ListScroll(st, LOWORD(wParam));
        return 0;
    case WM_MOUSEWHEEL:
        if (st)
        {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            ListScroll(st, delta > 0 ? SB_LINEUP : SB_LINEDOWN);
        }
        return 0;
    case WM_LBUTTONDOWN:
    {
        if (!st)
            break;
        const int y = static_cast<short>(HIWORD(lParam));
        const int idx = st->top + y / kRowH;
        if (y >= 0 && idx >= 0 && idx < static_cast<int>(st->view.size()))
        {
            st->sel = idx;
            SetFocus(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            PostMessageW(GetParent(hwnd), kMsgSelChanged, 0, 0);
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK:
        PostMessageW(GetParent(hwnd), kMsgWantDownload, 0, 0);
        return 0;
    case WM_KEYDOWN:
        if (!st)
            break;
        if (wParam == VK_DOWN || wParam == VK_UP)
        {
            const int count = static_cast<int>(st->view.size());
            if (count > 0)
            {
                st->sel += (wParam == VK_DOWN) ? 1 : -1;
                if (st->sel < 0)
                    st->sel = 0;
                if (st->sel >= count)
                    st->sel = count - 1;
                RECT lr;
                GetClientRect(hwnd, &lr);
                const int page = (lr.bottom > 0) ? (lr.bottom / kRowH) : 1;
                if (st->sel < st->top)
                    st->top = st->sel;
                if (st->sel >= st->top + page)
                    st->top = st->sel - page + 1;
                SetScrollPos(hwnd, SB_VERT, st->top, TRUE);
                InvalidateRect(hwnd, nullptr, FALSE);
                PostMessageW(GetParent(hwnd), kMsgSelChanged, 0, 0);
            }
        }
        else if (wParam == VK_RETURN)
        {
            PostMessageW(GetParent(hwnd), kMsgWantDownload, 0, 0);
        }
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// --------------------------------------------------------------- chrome

void PaintChrome(HWND hwnd, HDC dc, LibState *st)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    const bool dark = IsDarkMode();
    HBRUSH bg = CreateSolidBrush(dark ? RGB(32, 32, 32) : RGB(246, 246, 246));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    HFONT old = reinterpret_cast<HFONT>(SelectObject(dc, st->uiFont ? st->uiFont : GetStockObject(DEFAULT_GUI_FONT)));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, dark ? RGB(170, 170, 170) : RGB(96, 96, 96));

    // Source line to the right of the language radios.
    RECT sr = {kPad + 150, kRow1Y, rc.right - kPad - 100, kRow1Y + kRow1H};
    DrawTextW(dc, st->sourceText.c_str(), -1, &sr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);

    // Label to the left of the search box (the edit's cue banner is not reliable).
    RECT lr = {kPad, kRow2Y + 1, kPad + 44, kRow2Y + 29};
    SetTextColor(dc, dark ? RGB(180, 180, 180) : RGB(90, 90, 90));
    DrawTextW(dc, L"搜索", -1, &lr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);

    // Match counter under the search box, on the right.
    {
        wchar_t count[96] = {};
        swprintf_s(count, L"共 %zu 个 / 匹配 %zu 个", st->items[static_cast<int>(st->lang)].size(),
                   st->view.size());
        RECT cr = {rc.right - kPad - 220, kRow2Y + 1, rc.right - kPad, kRow2Y + 29};
        SetTextColor(dc, dark ? RGB(170, 170, 170) : RGB(110, 110, 110));
        DrawTextW(dc, count, -1, &cr, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
    }

    // Footer status.
    RECT fr = {kPad, rc.bottom - kFootH + 14, rc.right - kPad - 210, rc.bottom - kFootH + 38};
    SetTextColor(dc, dark ? RGB(200, 200, 200) : RGB(60, 60, 60));
    DrawTextW(dc, st->status.c_str(), -1, &fr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);

    // Separator above the footer.
    RECT sep = {kPad, rc.bottom - kFootH + 2, rc.right - kPad, rc.bottom - kFootH + 3};
    HBRUSH lineBrush = CreateSolidBrush(dark ? RGB(60, 60, 60) : RGB(220, 220, 220));
    FillRect(dc, &sep, lineBrush);
    DeleteObject(lineBrush);

    SelectObject(dc, old);
}

// ------------------------------------------------------------------ jobs

void StartListJob(HWND target, LibLang lang, bool forceRefresh, bool reprobe);

void StartListJob(HWND target, LibLang lang, bool forceRefresh, bool reprobe)
{
    auto alive = g_alive;
    std::thread([target, lang, forceRefresh, reprobe, alive]() {
        std::wstring err;
        LogLib(L"job start, reprobe=" + std::to_wstring(reprobe ? 1 : 0));
        if (reprobe || LibSelectedSource(lang) < 0)
        {
            PostStatus(target, L"正在测速并选择最快的源…");
            std::vector<long> lat;
            const int best = LibChooseFastest(lang, lat);
            std::wstring latStr;
            for (size_t i = 0; i < lat.size(); ++i)
                latStr += std::to_wstring(lat[i]) + L" ";
            LogLib(L"probe: " + latStr + L"best=" + std::to_wstring(best));
            if (best < 0)
            {
                PostStatus(target, L"所有预设源都无法连接，请检查网络");
                return;
            }
            const std::vector<LibSource> &srcs = AllSources(lang);
            std::wstring msg = L"已选源：" + std::wstring(srcs[static_cast<size_t>(best)].name);
            msg += L"（";
            msg += std::to_wstring(lat[static_cast<size_t>(best)]);
            msg += L" ms）";
            PostStatus(target, msg);
        }

        const std::wstring base = BaseOf(lang);
        if (base.empty())
        {
            PostStatus(target, L"没有可用的下载源");
            return;
        }

        // Try the selected source first, then the remaining reachable ones in
        // latency order: a mirror can answer the ping and still serve a broken
        // index page, so one failure must not end the whole job.
        std::vector<int> order;
        order.push_back(LibSelectedSource(lang));
        if (reprobe)
        {
            std::vector<long> lat;
            (void)LibChooseFastest(lang, lat); // already cached above
            std::vector<int> rest;
            for (size_t i = 0; i < lat.size(); ++i)
            {
                if (static_cast<int>(i) != order[0] && lat[i] >= 0)
                    rest.push_back(static_cast<int>(i));
            }
            std::sort(rest.begin(), rest.end(), [&lat](int a, int b) { return lat[static_cast<size_t>(a)] < lat[static_cast<size_t>(b)]; });
            order.insert(order.end(), rest.begin(), rest.end());
        }
        else
        {
            const std::vector<LibSource> &srcs = AllSources(lang);
            for (size_t i = 0; i < srcs.size(); ++i)
            {
                if (static_cast<int>(i) != order[0])
                    order.push_back(static_cast<int>(i));
            }
        }

        PostStatus(target, L"正在获取库列表…");
        std::vector<LibEntry> list;
        std::wstring lastErr = L"没有可用的下载源";
        for (int idx : order)
        {
            if (idx < 0)
                continue;
            if (!alive->load())
                return;
            const std::vector<LibSource> &srcs = AllSources(lang);
            const std::wstring tryBase = std::wstring(srcs[static_cast<size_t>(idx)].base);
            LogLib(L"list try src=" + std::to_wstring(idx) + L" base=" + tryBase);
            if (LibLoadList(lang, tryBase, list, err, forceRefresh))
            {
                LibSetSelectedSource(lang, idx);
                LogLib(L"list ok: " + std::to_wstring(list.size()) + L" src=" + std::to_wstring(idx));
                if (alive->load())
                    PostMessageW(target, kMsgList, static_cast<WPARAM>(lang), 0);
                PostStatus(target, L"已获取 " + std::to_wstring(list.size()) + L" 个库（源：" +
                                       std::wstring(srcs[static_cast<size_t>(idx)].name) + L"）");
                return;
            }
            LogLib(L"list fail src=" + std::to_wstring(idx) + L": " + err);
            lastErr = err;
        }
        PostStatus(target, L"获取库列表失败：" + lastErr);
    }).detach();
}

struct ProgressCtx
{
    HWND hwnd = nullptr;
    std::shared_ptr<std::atomic<bool>> alive;
    int lastPercent = -1;
};

void OnProgress(void *raw, unsigned long long got, unsigned long long total)
{
    ProgressCtx *ctx = static_cast<ProgressCtx *>(raw);
    if (!ctx || !ctx->hwnd)
        return;
    if (total == 0)
    {
        PostMessageW(ctx->hwnd, kMsgProgress, 0, static_cast<LPARAM>(got));
        return;
    }
    int percent = static_cast<int>((got * 100ull) / total);
    if (percent == ctx->lastPercent || percent % 2 != 0)
        return;
    ctx->lastPercent = percent;
    PostMessageW(ctx->hwnd, kMsgProgress, static_cast<WPARAM>(percent), static_cast<LPARAM>(got));
}

void StartDownload(HWND target, LibLang lang, LibEntry entry);

void StartDownload(HWND target, LibLang lang, LibEntry entry)
{
    auto alive = g_alive;
    std::thread([target, lang, entry, alive]() {
        const std::wstring base = BaseOf(lang);
        PostStatus(target, L"正在解析 " + entry.name + L" 的下载地址…");

        std::wstring url, fileName, err;
        LogLib(L"resolve start: " + entry.name);
        if (!LibResolveDownload(lang, base, entry, url, fileName, err))
        {
            LogLib(L"resolve fail: " + err);
            auto *p = new std::wstring(std::wstring(L"解析失败：") + err);
            PostMessageW(target, kMsgDone, 0, reinterpret_cast<LPARAM>(p));
            return;
        }

        const std::wstring dir = LibDownloadDir(lang);
        const std::wstring path = dir + L"\\" + fileName;
        PostStatus(target, L"正在下载 " + fileName + L"…");

        // GitHub release archives are unreachable from this network, so a
        // failed direct attempt is retried through the gh proxies before the
        // download is declared dead.
        std::vector<std::wstring> tries;
        tries.push_back(url);
        const bool fromGithub = url.find(L"://github.com/") != std::wstring::npos ||
                                url.find(L"://raw.githubusercontent.com/") != std::wstring::npos ||
                                url.find(L"://objects.githubusercontent.com/") != std::wstring::npos ||
                                url.find(L"://codeload.github.com/") != std::wstring::npos;
        if (fromGithub)
        {
            tries.push_back(L"https://gh-proxy.com/" + url);
            tries.push_back(L"https://ghfast.top/" + url);
        }

        ProgressCtx ctx;
        ctx.hwnd = target;
        ctx.alive = alive;
        std::wstring derr;
        bool ok = false;
        std::wstring usedUrl = url;
        for (const std::wstring &tryUrl : tries)
        {
            if (!alive->load())
                break;
            ok = NetDownloadFile(tryUrl, path, derr, OnProgress, &ctx);
            if (ok)
            {
                usedUrl = tryUrl;
                break;
            }
            LogLib(L"download fail: " + tryUrl + L" -> " + derr);
        }
        std::wstring msg;
        if (ok)
        {
            WIN32_FILE_ATTRIBUTE_DATA fad = {};
            GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad);
            const unsigned long long size =
                static_cast<unsigned long long>(fad.nFileSizeHigh) << 32 | fad.nFileSizeLow;
            msg = L"下载完成：" + fileName + L"（" + FormatSize(size) + L"）";
            LogLib(L"download ok: " + usedUrl);
        }
        else
        {
            msg = L"下载失败：" + derr;
        }
        auto *p = new std::wstring(msg);
        if (!PostMessageW(target, kMsgDone, ok ? 1 : 0, reinterpret_cast<LPARAM>(p)))
            delete p;
    }).detach();
}

// ------------------------------------------------------------- window proc

void RefreshSourceText(LibState *st)
{
    const std::vector<LibSource> &srcs = AllSources(st->lang);
    const int idx = LibSelectedSource(st->lang);
    st->sourceText = L"源：";
    if (idx >= 0 && static_cast<size_t>(idx) < srcs.size())
    {
        st->sourceText += srcs[static_cast<size_t>(idx)].name;
        if (st->lang == LibLang::Cpp)
            st->sourceText += L"（vcpkg 端口）";
        else
            st->sourceText += L"（PyPI 镜像）";
    }
    else
    {
        st->sourceText += L"自动测速中…";
    }
}

void DownloadSelection(LibState *st)
{
    if (!st || st->downloading >= 0)
        return;
    if (st->sel < 0 || st->sel >= static_cast<int>(st->view.size()))
    {
        SetStatus(st, L"请先在列表里选中一个库");
        return;
    }
    const LibEntry entry = ItemsOf(st)[st->view[static_cast<size_t>(st->sel)]];
    st->downloading = st->sel;
    SetStatus(st, L"开始处理 " + entry.name + L"…");
    StartDownload(st->hwnd, st->lang, entry);
}

LRESULT CALLBACK LibProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LibState *st = GetState(hwnd);
    switch (msg)
    {
    case WM_CREATE:
    {
        st = new LibState();
        st->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

        LOGFONTW lf = {};
        lf.lfHeight = -15;
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
        const wchar_t *face = L"Microsoft YaHei UI";
        for (int i = 0; face[i] != L'\0' && i < LF_FACESIZE - 1; ++i)
            lf.lfFaceName[i] = face[i];
        lf.lfFaceName[LF_FACESIZE - 1] = L'\0';
        st->uiFont = CreateFontIndirectW(&lf);

        lf.lfWeight = FW_SEMIBOLD;
        st->boldFont = CreateFontIndirectW(&lf);

        lf.lfHeight = -13;
        lf.lfWeight = FW_NORMAL;
        st->smallFont = CreateFontIndirectW(&lf);

        st->bgBrush = CreateSolidBrush(IsDarkMode() ? RGB(32, 32, 32) : RGB(246, 246, 246));

        st->hRadCpp = CreateWindowExW(0, L"BUTTON", L"C++", WS_CHILD | WS_VISIBLE | WS_GROUP | BS_AUTORADIOBUTTON,
                                     0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRadCpp)),
                                     GetModuleHandleW(nullptr), nullptr);
        st->hRadPy = CreateWindowExW(0, L"BUTTON", L"Python", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                     0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRadPy)),
                                     GetModuleHandleW(nullptr), nullptr);
        st->hBtnProbe = CreateWindowExW(0, L"BUTTON", L"重新测速", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                        0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdProbe)),
                                        GetModuleHandleW(nullptr), nullptr);
        st->hSearch = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                      0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSearch)),
                                      GetModuleHandleW(nullptr), nullptr);
        SendMessageW(st->hSearch, kEmSetCueBanner, TRUE, reinterpret_cast<LPARAM>(L"搜索库名，例如 json / requests"));
        st->hList = CreateWindowExW(WS_EX_CLIENTEDGE, kListClass, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP,
                                    0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdList)),
                                    GetModuleHandleW(nullptr), nullptr);
        SetWindowLongPtrW(st->hList, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

        st->hBtnDown = CreateWindowExW(0, L"BUTTON", L"下载", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDownload)),
                                       GetModuleHandleW(nullptr), nullptr);
        st->hBtnOpen = CreateWindowExW(0, L"BUTTON", L"打开目录", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdOpenDir)),
                                       GetModuleHandleW(nullptr), nullptr);

        SendMessageW(st->hRadCpp, BM_SETCHECK, BST_CHECKED, 0);

        for (HWND child : {st->hRadCpp, st->hRadPy, st->hBtnProbe, st->hSearch, st->hBtnDown, st->hBtnOpen})
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(st->uiFont), TRUE);

        LayoutChildren(hwnd, st);
        return 0;
    }
    case WM_SIZE:
        if (st)
            LayoutChildren(hwnd, st);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (st)
            PaintChrome(hwnd, dc, st);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        if (st)
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkColor(dc, IsDarkMode() ? RGB(32, 32, 32) : RGB(246, 246, 246));
            SetTextColor(dc, IsDarkMode() ? RGB(225, 225, 225) : RGB(30, 30, 30));
            return reinterpret_cast<LRESULT>(st->bgBrush);
        }
        break;
    case WM_CTLCOLOREDIT:
        if (st)
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkColor(dc, IsDarkMode() ? RGB(45, 45, 45) : RGB(255, 255, 255));
            SetTextColor(dc, IsDarkMode() ? RGB(230, 230, 230) : RGB(20, 20, 20));
            static HBRUSH editBrush = nullptr;
            static bool editDark = false;
            if (!editBrush || editDark != IsDarkMode())
            {
                if (editBrush)
                    DeleteObject(editBrush);
                editBrush = CreateSolidBrush(IsDarkMode() ? RGB(45, 45, 45) : RGB(255, 255, 255));
                editDark = IsDarkMode();
            }
            return reinterpret_cast<LRESULT>(editBrush);
        }
        break;
    case WM_COMMAND:
    {
        if (!st)
            break;
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);

        if (id == kIdRadCpp && code == BN_CLICKED)
        {
            st->lang = LibLang::Cpp;
            st->sel = -1;
            // The search text is language specific: dropping it avoids ending up
            // with a filter that matches nothing in the new list.
            if (st->hSearch)
                SetWindowTextW(st->hSearch, L"");
            RefreshSourceText(st);
            RebuildView(st);
            if (!st->loaded[0])
                StartListJob(hwnd, LibLang::Cpp, false, LibSelectedSource(LibLang::Cpp) < 0);
            else
                SetStatus(st, L"已载入 " + std::to_wstring(st->items[0].size()) + L" 个 vcpkg 端口");
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (id == kIdRadPy && code == BN_CLICKED)
        {
            st->lang = LibLang::Python;
            st->sel = -1;
            if (st->hSearch)
                SetWindowTextW(st->hSearch, L"");
            RefreshSourceText(st);
            RebuildView(st);
            if (!st->loaded[1])
                StartListJob(hwnd, LibLang::Python, false, LibSelectedSource(LibLang::Python) < 0);
            else
                SetStatus(st, L"已载入 " + std::to_wstring(st->items[1].size()) + L" 个 PyPI 包");
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (id == kIdProbe && code == BN_CLICKED)
        {
            SetStatus(st, L"正在重新测速…");
            StartListJob(hwnd, st->lang, true, true);
            return 0;
        }
        if (id == kIdSearch && code == EN_CHANGE)
        {
            RebuildView(st);
            return 0;
        }
        if (id == kIdDownload && code == BN_CLICKED)
        {
            DownloadSelection(st);
            return 0;
        }
        if (id == kIdOpenDir && code == BN_CLICKED)
        {
            std::wstring dir = LibDownloadDir(st->lang);
            ShellExecuteW(hwnd, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        break;
    }
    case kMsgStatus:
    {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring *>(lParam));
        if (st && text)
        {
            st->status = *text;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case kMsgList:
    {
        if (!st)
            return 0;
        const int langIdx = static_cast<int>(wParam);
        // The worker filled the cache; reload it from disk-backed API is costly,
        // so ask the loader once more only when the memory copy is empty.
        if (st->items[langIdx].empty())
        {
            std::vector<LibEntry> list;
            std::wstring err;
            if (LibLoadList(static_cast<LibLang>(langIdx), BaseOf(static_cast<LibLang>(langIdx)), list, err, false))
                st->items[langIdx] = std::move(list);
        }
        st->loaded[langIdx] = true;
        RefreshSourceText(st);
        if (static_cast<int>(st->lang) == langIdx)
            RebuildView(st);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case kMsgProgress:
    {
        if (!st)
            return 0;
        const int percent = static_cast<int>(wParam);
        const unsigned long long got = static_cast<unsigned long long>(lParam);
        std::wstring s;
        if (percent > 0)
            s = L"下载中… " + std::to_wstring(percent) + L"%";
        else
            s = L"下载中… " + FormatSize(got);
        SetStatus(st, s);
        return 0;
    }
    case kMsgDone:
    {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring *>(lParam));
        if (st)
        {
            st->downloading = -1;
            if (text)
                st->status = *text;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case kMsgSelChanged:
    {
        if (!st)
            return 0;
        if (st->sel < 0 || st->sel >= static_cast<int>(st->view.size()))
            return 0;
        const size_t itemIdx = st->view[static_cast<size_t>(st->sel)];
        const LibEntry e = ItemsOf(st)[itemIdx];
        st->status = L"已选中：" + e.name + (e.version.empty() ? L"" : L"  v" + e.version);
        InvalidateRect(hwnd, nullptr, FALSE);

        if (st->lang == LibLang::Python && e.version.empty())
        {
            auto alive = g_alive;
            std::wstring base = BaseOf(st->lang);
            const int langIdx = static_cast<int>(st->lang);
            std::thread([hwnd, langIdx, itemIdx, e, base, alive]() {
                LibEntry info = e;
                std::wstring err;
                if (!LibFetchInfo(LibLang::Python, base, info, err))
                    return;
                auto *res = new InfoResult{langIdx, itemIdx, info, alive};
                if (!PostMessageW(hwnd, kMsgInfo, 0, reinterpret_cast<LPARAM>(res)))
                    delete res;
            }).detach();
        }
        return 0;
    }
    case kMsgInfo:
    {
        std::unique_ptr<InfoResult> res(reinterpret_cast<InfoResult *>(lParam));
        if (!st || !res)
            return 0;
        std::vector<LibEntry> &items = st->items[res->langIdx];
        if (res->itemIdx < items.size())
        {
            LibEntry &slot = items[res->itemIdx];
            if (slot.name == res->entry.name)
            {
                slot.version = res->entry.version;
                slot.summary = res->entry.summary;
                if (st->hList)
                    InvalidateRect(st->hList, nullptr, FALSE);
                if (static_cast<int>(st->lang) == res->langIdx)
                    st->status = L"已选中：" + slot.name + L"  v" + slot.version;
            }
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        (void)kMsgInfoDone;
        return 0;
    }
    case kMsgWantDownload:
        DownloadSelection(st);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_DESTROY:
        if (g_alive)
            g_alive->store(false);
        if (st)
        {
            if (st->uiFont)
                DeleteObject(st->uiFont);
            if (st->boldFont)
                DeleteObject(st->boldFont);
            if (st->smallFont)
                DeleteObject(st->smallFont);
            if (st->bgBrush)
                DeleteObject(st->bgBrush);
            delete st;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        g_libWin = nullptr;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void EnsureClasses()
{
    static bool done = false;
    if (done)
        return;
    done = true;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = ListProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kListClass;
    RegisterClassExW(&wc);

    WNDCLASSEXW wc2 = {};
    wc2.cbSize = sizeof(wc2);
    wc2.style = CS_DBLCLKS;
    wc2.lpfnWndProc = LibProc;
    wc2.hInstance = GetModuleHandleW(nullptr);
    wc2.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc2.hbrBackground = nullptr;
    wc2.lpszClassName = kClassName;
    RegisterClassExW(&wc2);
}

} // namespace

void ShowLibraryWindow(HWND parent)
{
    EnsureClasses();

    if (g_libWin && IsWindow(g_libWin))
    {
        ShowWindow(g_libWin, SW_SHOW);
        SetForegroundWindow(g_libWin);
        return;
    }

    g_alive = std::make_shared<std::atomic<bool>>(true);

    RECT prc = {0, 0, 0, 0};
    if (parent)
        GetWindowRect(parent, &prc);
    const int w = 980;
    const int h = 660;
    int x = prc.left + ((prc.right - prc.left) - w) / 2;
    int y = prc.top + ((prc.bottom - prc.top) - h) / 2;
    if (x < 0)
        x = 30;
    if (y < 0)
        y = 30;

    g_libWin = CreateWindowExW(WS_EX_DLGMODALFRAME, kClassName, L"开发库下载",
                               WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, w, h, parent, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
    if (!g_libWin)
        return;

    LibState *st = GetState(g_libWin);
    if (st)
    {
        RefreshSourceText(st);
        st->status = L"正在测速并选择最快的源…";
        StartListJob(g_libWin, LibLang::Cpp, false, true);
    }

    ShowWindow(g_libWin, SW_SHOW);
    SetForegroundWindow(g_libWin);
}
