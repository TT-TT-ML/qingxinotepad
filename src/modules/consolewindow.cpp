#include "consolewindow.h"
#include "core/globals.h"
#include "core/types.h"
#include "core/storage.h"
#include "theme.h"
#include "lang/lang.h"

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cwctype>

#define CONSOLE_CLASS L"LegacyCodeConsole"
#define CONSOLE_SETTINGS_KEY L"Software\\LegacyNotepad"

// 传回 UI 线程的结果（输出片段 / 命令结束）
#define WM_APP_CONSOLE_APPEND (WM_APP + 20)
#define WM_APP_CONSOLE_DONE (WM_APP + 21)

namespace
{
    int g_defaultRows = 5;
    int g_defaultCols = 50;

    // 一个字符单元格：字符 + 颜色索引
    //   0        = 主题默认色
    //   1..16    = ANSI 基本色（1 黑 … 8 白灰，9..16 高亮）
    //   17..272  = 256 色方案的调色板序号（color - 17）
    struct Cell
    {
        wchar_t ch;
        unsigned short color;
    };

    struct Line
    {
        std::vector<Cell> cells;
    };

    // 逻辑行折成视觉行后的一段（[first, last) 是单元格下标）
    struct RowRef
    {
        int line;
        int first;
        int last;
    };

    struct ConsoleState
    {
        HWND hwnd = nullptr;
        std::wstring cwd;
        std::vector<Line> lines; // 最后一行是正在写入的行
        int curPos = 0;          // 当前行写入位置（支持 \r 覆盖、\b 退格）
        std::wstring pending;    // 未收完的 ESC 序列
        unsigned short curColor = 0;
        bool color = true;       // 是否启用 ANSI 彩色输出

        std::wstring input;
        int caret = 0;

        int rows = 5;
        int cols = 50;
        int fontPx = 15;

        std::vector<RowRef> layout;
        bool layoutDirty = true;
        int firstVisible = 0; // 输出区首行（按视觉行计）
        bool followTail = true;

        std::vector<std::wstring> history;
        int historyIndex = -1;

        bool running = false;
        HANDLE job = nullptr;        // 作业对象：Ctrl+C / stop 用来杀进程树
        HANDLE stdinWrite = nullptr; // 运行中进程的标准输入写端（可交互）

        HFONT font = nullptr;
        int cellW = 8;
        int lineH = 18;
        int padX = 8;
        int padY = 6;
        int inputH = 22;
        bool caretOn = true;

        // 输出区选中范围（字符级）
        int selAnchorLine = -1;
        int selAnchorCol = -1;
        int selLine = -1;
        int selCol = -1;
        bool dragging = false;

        // 滚动条拖动
        bool barDragging = false;
        int barGrabOffset = 0;

        // 输出查找
        bool findMode = false;
        std::wstring findQuery;
        std::vector<int> findHits; // 命中的逻辑行号
        int findIndex = -1;
        bool findDirty = false;
    };

    ConsoleState g_con;

    int ClampInt(int v, int lo, int hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // 控制台文案：中文界面用中文，其余语言用英文（新增文本不铺 15 份语言表）
    std::wstring Tr(const std::wstring &zh, const std::wstring &en)
    {
        const LangID id = GetCurrentLanguage();
        if (id == LangID::ZH_CN || id == LangID::ZH_TW)
            return zh;
        return en;
    }

    void SplitAppend(const std::wstring &text);

    void RedrawConsole()
    {
        if (g_con.hwnd)
            InvalidateRect(g_con.hwnd, nullptr, FALSE);
    }

    std::wstring NormalizeDir(const std::wstring &dir)
    {
        if (dir.empty())
            return std::wstring();
        std::wstring out = dir;
        while (out.size() > 3 && (out.back() == L'\\' || out.back() == L'/'))
            out.pop_back();
        return out;
    }

    std::wstring JoinPath(const std::wstring &dir, const std::wstring &name)
    {
        if (dir.empty())
            return name;
        const wchar_t last = dir.back();
        if (last == L'\\' || last == L'/')
            return dir + name;
        return dir + L"\\" + name;
    }

    // 取文件所在目录。不能对 std::wstring::data() 用 PathRemoveFileSpecW：
    // 那会在原缓冲区上截断，实测得到的是半截路径。
    std::wstring DirOfFile(const std::wstring &file)
    {
        const size_t slash = file.find_last_of(L"\\/");
        const std::wstring dir = (slash == std::wstring::npos) ? std::wstring() : file.substr(0, slash);
        return NormalizeDir(dir);
    }

    std::wstring LowerText(const std::wstring &text)
    {
        std::wstring out = text;
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = static_cast<wchar_t>(towlower(out[i]));
        return out;
    }

    // ---- 设置读写 -------------------------------------------------------

    void LoadConsoleSettings()
    {
        DWORD v = 0;
        if (StorageGetDword(CONSOLE_SETTINGS_KEY, L"ConsoleRows", &v) && v >= 3 && v <= 60)
            g_defaultRows = static_cast<int>(v);
        if (StorageGetDword(CONSOLE_SETTINGS_KEY, L"ConsoleCols", &v) && v >= 20 && v <= 300)
            g_defaultCols = static_cast<int>(v);
        if (StorageGetDword(CONSOLE_SETTINGS_KEY, L"ConsoleFontSize", &v) && v >= 10 && v <= 32)
            g_con.fontPx = static_cast<int>(v);
        if (StorageGetDword(CONSOLE_SETTINGS_KEY, L"ConsoleColor", &v))
            g_con.color = (v != 0);

        std::wstring hist;
        if (StorageGetString(CONSOLE_SETTINGS_KEY, L"ConsoleHistory", &hist) && !hist.empty())
        {
            g_con.history.clear();
            size_t start = 0;
            while (start <= hist.size() && g_con.history.size() < 200)
            {
                size_t nl = hist.find(L'\n', start);
                if (nl == std::wstring::npos)
                    nl = hist.size();
                std::wstring item = hist.substr(start, nl - start);
                if (!item.empty())
                    g_con.history.push_back(item);
                start = nl + 1;
            }
        }
    }

    void SaveConsoleSize(int rows, int cols)
    {
        StorageSetDword(CONSOLE_SETTINGS_KEY, L"ConsoleRows", static_cast<DWORD>(rows));
        StorageSetDword(CONSOLE_SETTINGS_KEY, L"ConsoleCols", static_cast<DWORD>(cols));
    }

    void SaveConsoleFont(int px)
    {
        StorageSetDword(CONSOLE_SETTINGS_KEY, L"ConsoleFontSize", static_cast<DWORD>(px));
    }

    void SaveConsoleColor(bool on)
    {
        StorageSetDword(CONSOLE_SETTINGS_KEY, L"ConsoleColor", on ? 1u : 0u);
    }

    void SaveConsoleHistory()
    {
        std::wstring joined;
        for (size_t i = 0; i < g_con.history.size(); ++i)
        {
            if (i)
                joined += L'\n';
            joined += g_con.history[i];
        }
        StorageSetString(CONSOLE_SETTINGS_KEY, L"ConsoleHistory", joined);
    }

    // ---- 文本写入 -------------------------------------------------------

    // 宽字符占 2 列（中日韩、全角），用于折行计算
    int DisplayWidth(wchar_t c)
    {
        if (c >= 0x1100 &&
            (c <= 0x115F ||
             (c >= 0x2E80 && c <= 0x303E) ||
             (c >= 0x3041 && c <= 0x33FF) ||
             (c >= 0x3400 && c <= 0x4DBF) ||
             (c >= 0x4E00 && c <= 0x9FFF) ||
             (c >= 0xA000 && c <= 0xA4CF) ||
             (c >= 0xAC00 && c <= 0xD7A3) ||
             (c >= 0xF900 && c <= 0xFAFF) ||
             (c >= 0xFE30 && c <= 0xFE4F) ||
             (c >= 0xFF00 && c <= 0xFF60) ||
             (c >= 0xFFE0 && c <= 0xFFE6)))
            return 2;
        return 1;
    }

    std::wstring LineText(const Line &ln)
    {
        std::wstring out;
        out.reserve(ln.cells.size());
        for (size_t i = 0; i < ln.cells.size(); ++i)
            out += ln.cells[i].ch == 0 ? L' ' : ln.cells[i].ch;
        return out;
    }

    // 复制用：去掉行尾补齐的空格
    std::wstring TrimRightText(const std::wstring &text)
    {
        std::wstring out = text;
        while (!out.empty() && (out.back() == L' ' || out.back() == L'\t'))
            out.pop_back();
        return out;
    }

    void ClearSelection()
        {
            g_con.selAnchorLine = -1;
            g_con.selAnchorCol = -1;
            g_con.selLine = -1;
            g_con.selCol = -1;
        }

    void TrimLines()
    {
        const size_t limit = 2000;
        if (g_con.lines.size() <= limit)
            return;
        const size_t drop = g_con.lines.size() - limit;
        g_con.lines.erase(g_con.lines.begin(), g_con.lines.begin() + static_cast<std::ptrdiff_t>(drop));
        if (g_con.selAnchorLine >= 0)
        {
            g_con.selAnchorLine -= static_cast<int>(drop);
            g_con.selLine -= static_cast<int>(drop);
            if (g_con.selLine < 0 || g_con.selAnchorLine < 0)
                ClearSelection();
        }
        g_con.findDirty = true;
    }

    void ClearOutput()
    {
        g_con.lines.clear();
        g_con.lines.push_back(Line());
        g_con.curPos = 0;
        g_con.firstVisible = 0;
        g_con.followTail = true;
        ClearSelection();
        g_con.layoutDirty = true;
        g_con.findDirty = true;
        RedrawConsole();
    }

    void WriteCell(wchar_t ch, unsigned short color)
    {
        Line &ln = g_con.lines.back();
        if (g_con.curPos < 0)
            g_con.curPos = 0;
        const size_t need = static_cast<size_t>(g_con.curPos) + 1;
        if (ln.cells.size() < need)
        {
            const size_t old = ln.cells.size();
            ln.cells.resize(need);
            for (size_t i = old; i < need; ++i)
                ln.cells[i] = Cell{L' ', 0};
        }
        ln.cells[static_cast<size_t>(g_con.curPos)] = Cell{ch, color};
        ++g_con.curPos;
    }

    void BuildLayout()
    {
        g_con.layout.clear();
        const int cols = g_con.cols > 0 ? g_con.cols : 1;
        for (size_t li = 0; li < g_con.lines.size(); ++li)
        {
            const Line &ln = g_con.lines[li];
            const int count = static_cast<int>(ln.cells.size());
            if (count == 0)
            {
                g_con.layout.push_back(RowRef{static_cast<int>(li), 0, 0});
                continue;
            }
            int start = 0;
            while (start < count)
            {
                int width = 0;
                int end = start;
                while (end < count)
                {
                    const int w = DisplayWidth(ln.cells[static_cast<size_t>(end)].ch);
                    if (width + w > cols && end > start)
                        break;
                    width += w;
                    ++end;
                }
                if (end == start)
                    ++end;
                g_con.layout.push_back(RowRef{static_cast<int>(li), start, end});
                start = end;
            }
        }
        g_con.layoutDirty = false;
    }

    // ---- 输出查找 -------------------------------------------------------

    void RebuildFindHits()
    {
        g_con.findHits.clear();
        g_con.findDirty = false;
        if (g_con.findQuery.empty())
        {
            g_con.findIndex = -1;
            return;
        }
        const std::wstring q = LowerText(g_con.findQuery);
        for (size_t i = 0; i < g_con.lines.size(); ++i)
        {
            if (LowerText(LineText(g_con.lines[i])).find(q) != std::wstring::npos)
                g_con.findHits.push_back(static_cast<int>(i));
        }
        if (g_con.findHits.empty())
            g_con.findIndex = -1;
        else if (g_con.findIndex >= static_cast<int>(g_con.findHits.size()))
            g_con.findIndex = 0;
    }

    void ScrollToLine(int line)
    {
        if (g_con.layoutDirty)
            BuildLayout();
        const int total = static_cast<int>(g_con.layout.size());
        const int maxFirst = total > g_con.rows ? total - g_con.rows : 0;
        for (size_t i = 0; i < g_con.layout.size(); ++i)
        {
            if (g_con.layout[i].line != line)
                continue;
            g_con.firstVisible = static_cast<int>(i) - g_con.rows / 2;
            g_con.firstVisible = ClampInt(g_con.firstVisible, 0, maxFirst);
            g_con.followTail = g_con.firstVisible >= maxFirst;
            break;
        }
        g_con.layoutDirty = true;
        RedrawConsole();
    }

    void FindStep(int dir)
    {
        if (g_con.findQuery.empty())
            return;
        RebuildFindHits();
        if (g_con.findHits.empty())
        {
            g_con.findIndex = -1;
            SplitAppend(Tr(L"（没有找到匹配的行）\r\n", L"(no matching line found)\r\n"));
            return;
        }
        const int count = static_cast<int>(g_con.findHits.size());
        if (g_con.findIndex < 0)
            g_con.findIndex = dir > 0 ? 0 : count - 1;
        else
            g_con.findIndex = (g_con.findIndex + dir + count) % count;
        ScrollToLine(g_con.findHits[static_cast<size_t>(g_con.findIndex)]);
    }

    void ApplyEscape(const std::wstring &seq)
    {
        if (seq.size() < 3)
            return;
        const wchar_t finalCh = seq.back();
        const std::wstring body = seq.substr(2, seq.size() - 3);
        if (finalCh == L'm')
        {
            // 关闭彩色输出时忽略所有 SGR 取值，后续字符一律用主题默认色
            if (!g_con.color)
            {
                g_con.curColor = 0;
                return;
            }
            std::vector<int> codes;
            size_t start = 0;
            while (start <= body.size())
            {
                const size_t semi = body.find(L';', start);
                const size_t end = (semi == std::wstring::npos) ? body.size() : semi;
                const std::wstring piece = body.substr(start, end - start);
                codes.push_back(piece.empty() ? 0 : _wtoi(piece.c_str()));
                if (semi == std::wstring::npos)
                    break;
                start = semi + 1;
            }

            // 6×6×6 色立方 + 灰阶的近似下标，供真彩色（38;2;r;g;b）使用
            auto cubeIndex = [](int v) {
                if (v < 48)
                    return 0;
                if (v < 114)
                    return 1;
                if (v < 180)
                    return 2;
                if (v < 235)
                    return 3;
                if (v < 250)
                    return 4;
                return 5;
            };

            for (size_t i = 0; i < codes.size(); ++i)
            {
                const int code = codes[i];
                if (code == 0 || code == 39)
                {
                    g_con.curColor = 0;
                }
                else if (code >= 30 && code <= 37)
                {
                    g_con.curColor = static_cast<unsigned short>(code - 29);
                }
                else if (code >= 90 && code <= 97)
                {
                    g_con.curColor = static_cast<unsigned short>(code - 81);
                }
                else if (code == 38 && i + 2 < codes.size() && codes[i + 1] == 5)
                {
                    const int n = ClampInt(codes[i + 2], 0, 255);
                    g_con.curColor = static_cast<unsigned short>(17 + n);
                    i += 2;
                }
                else if (code == 38 && i + 4 < codes.size() && codes[i + 1] == 2)
                {
                    const int r = cubeIndex(ClampInt(codes[i + 2], 0, 255));
                    const int g = cubeIndex(ClampInt(codes[i + 3], 0, 255));
                    const int b = cubeIndex(ClampInt(codes[i + 4], 0, 255));
                    g_con.curColor = static_cast<unsigned short>(17 + 16 + r * 36 + g * 6 + b);
                    i += 4;
                }
                else if ((code == 48 || code == 58) && i + 2 < codes.size() && codes[i + 1] == 5)
                {
                    i += 2; // 背景色 / 下划线色：本窗口只画前景，跳过取值
                }
                else if (code == 48 && i + 4 < codes.size() && codes[i + 1] == 2)
                {
                    i += 4;
                }
                // 其余（粗体 / 下划线 / 斜体）忽略，避免颜色错乱
            }
        }
        else if (finalCh == L'K')
        {
            Line &ln = g_con.lines.back();
            if (body == L"1")
            {
                const int end = ClampInt(g_con.curPos, 0, static_cast<int>(ln.cells.size()));
                for (int i = 0; i < end; ++i)
                    ln.cells[static_cast<size_t>(i)] = Cell{L' ', 0};
            }
            else if (body == L"2")
            {
                for (size_t i = 0; i < ln.cells.size(); ++i)
                    ln.cells[i] = Cell{L' ', 0};
            }
            else
            {
                if (g_con.curPos >= 0 && g_con.curPos < static_cast<int>(ln.cells.size()))
                    ln.cells.resize(static_cast<size_t>(g_con.curPos));
            }
            g_con.layoutDirty = true;
        }
        else if (finalCh == L'J')
        {
            if (body == L"2" || body == L"3")
                ClearOutput();
        }
        else if (finalCh == L'G' || finalCh == L'H')
        {
            if (body.empty() || _wtoi(body.c_str()) <= 1)
                g_con.curPos = 0;
        }
        else if (finalCh == L'A')
        {
            // 光标上移：退回上一行末尾继续写（进度条类输出常见）
            if (g_con.lines.size() >= 2)
            {
                g_con.lines.pop_back();
                g_con.curPos = static_cast<int>(g_con.lines.back().cells.size());
                g_con.layoutDirty = true;
            }
        }
    }

    void FeedText(const std::wstring &text)
    {
        bool changed = false;
        for (size_t i = 0; i < text.size(); ++i)
        {
            const wchar_t c = text[i];

            if (!g_con.pending.empty())
            {
                if (g_con.pending.size() == 1)
                {
                    if (c == L'[')
                    {
                        g_con.pending += c;
                        continue;
                    }
                    g_con.pending.clear(); // 不支持的 ESC 序列，丢弃
                    continue;
                }
                g_con.pending += c;
                if (c >= 0x40 && c <= 0x7E)
                {
                    ApplyEscape(g_con.pending);
                    g_con.pending.clear();
                }
                else if (g_con.pending.size() > 40)
                {
                    g_con.pending.clear();
                }
                continue;
            }

            switch (c)
            {
            case L'\x1b':
                g_con.pending = L"\x1b";
                break;
            case L'\r':
                g_con.curPos = 0;
                break;
            case L'\n':
                g_con.lines.push_back(Line());
                g_con.curPos = 0;
                TrimLines();
                break;
            case L'\b':
                if (g_con.curPos > 0)
                    --g_con.curPos;
                break;
            case L'\a':
                break;
            case L'\t':
            {
                const int spaces = 8 - (g_con.curPos % 8);
                for (int k = 0; k < spaces; ++k)
                    WriteCell(L' ', g_con.curColor);
                break;
            }
            default:
                if (c == 0)
                    break;
                WriteCell(c, g_con.curColor);
                break;
            }
            changed = true;
        }

        if (!changed)
            return;

        g_con.layoutDirty = true;
        g_con.findDirty = true;
        if (g_con.followTail)
        {
            BuildLayout();
            const int total = static_cast<int>(g_con.layout.size());
            g_con.firstVisible = total > g_con.rows ? total - g_con.rows : 0;
        }
        RedrawConsole();
    }

    void SplitAppend(const std::wstring &text)
    {
        FeedText(text);
    }

    void PostsAppend(const std::wstring &text)
    {
        if (text.empty() || !g_con.hwnd)
            return;
        auto *payload = new std::wstring(text);
        if (!PostMessageW(g_con.hwnd, WM_APP_CONSOLE_APPEND, reinterpret_cast<WPARAM>(payload), 0))
            delete payload;
    }

    // ---- 字体与窗口尺寸 -------------------------------------------------

    void MeasureFont(HWND hwnd)
    {
        HDC dc = GetDC(hwnd);
        HGDIOBJ old = SelectObject(dc, g_con.font);
        SIZE sz = {};
        GetTextExtentPoint32W(dc, L"M", 1, &sz);
        g_con.cellW = sz.cx > 0 ? static_cast<int>(sz.cx) : 8;
        TEXTMETRICW tm = {};
        GetTextMetricsW(dc, &tm);
        g_con.lineH = tm.tmHeight + 2 > 0 ? static_cast<int>(tm.tmHeight) + 2 : 18;
        SelectObject(dc, old);
        ReleaseDC(hwnd, dc);
        g_con.inputH = g_con.lineH + 6 > 22 ? g_con.lineH + 6 : 22;
    }

    void CreateConsoleFont(HWND hwnd)
    {
        if (g_con.font)
        {
            DeleteObject(g_con.font);
            g_con.font = nullptr;
        }
        g_con.font = CreateFontW(-g_con.fontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        MeasureFont(hwnd);
    }

    // 右侧留给滚动条
    const int kBarGap = 12;

    void EnsureSize()
    {
        if (!g_con.hwnd)
            return;
        const int clientW = g_con.cols * g_con.cellW + g_con.padX * 2 + kBarGap;
        const int clientH = g_con.rows * g_con.lineH + g_con.inputH + g_con.padY * 2;
        RECT rc = {0, 0, clientW, clientH};
        AdjustWindowRectEx(&rc, WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX, FALSE, WS_EX_TOOLWINDOW);
        SetWindowPos(g_con.hwnd, HWND_TOP, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void SetFontSize(int px)
    {
        px = ClampInt(px, 10, 32);
        if (px == g_con.fontPx)
            return;
        g_con.fontPx = px;
        CreateConsoleFont(g_con.hwnd);
        SaveConsoleFont(px);
        g_con.layoutDirty = true;
        EnsureSize();
        RedrawConsole();
    }

    // 行/列：写默认值 + 注册表，并立即按新网格调整窗口
    void ApplyGrid(int rows, int cols)
    {
        g_con.rows = ClampInt(rows, 3, 60);
        g_con.cols = ClampInt(cols, 20, 300);
        g_defaultRows = g_con.rows;
        g_defaultCols = g_con.cols;
        SaveConsoleSize(g_con.rows, g_con.cols);
        g_con.layoutDirty = true;
        g_con.followTail = true;
        if (g_con.hwnd)
        {
            EnsureSize();
            RedrawConsole();
        }
    }

    void SetColorEnabled(bool on)
    {
        g_con.color = on;
        if (!on)
            g_con.curColor = 0;
        SaveConsoleColor(on);
        RedrawConsole();
    }

    // 命令行窗口的设置对话框：行数 / 列数 / 字号 / 是否彩色
    INT_PTR CALLBACK ConsoleSettingsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_INITDIALOG:
        {
            wchar_t buf[32];
            wsprintfW(buf, L"%d", g_con.rows);
            SetWindowTextW(GetDlgItem(hDlg, 1001), buf);
            wsprintfW(buf, L"%d", g_con.cols);
            SetWindowTextW(GetDlgItem(hDlg, 1002), buf);
            wsprintfW(buf, L"%d", g_con.fontPx);
            SetWindowTextW(GetDlgItem(hDlg, 1003), buf);
            CheckDlgButton(hDlg, 1004, g_con.color ? BST_CHECKED : BST_UNCHECKED);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK)
            {
                BOOL ok1 = FALSE, ok2 = FALSE, ok3 = FALSE;
                const int rows = static_cast<int>(GetDlgItemInt(hDlg, 1001, &ok1, FALSE));
                const int cols = static_cast<int>(GetDlgItemInt(hDlg, 1002, &ok2, FALSE));
                const int px = static_cast<int>(GetDlgItemInt(hDlg, 1003, &ok3, FALSE));
                if (ok1 && ok2 && ok3 && rows >= 3 && rows <= 60 && cols >= 20 && cols <= 300 &&
                    px >= 10 && px <= 32)
                {
                    SetFontSize(px);
                    ApplyGrid(rows, cols);
                    SetColorEnabled(IsDlgButtonChecked(hDlg, 1004) == BST_CHECKED);
                    DestroyWindow(hDlg);
                }
                else
                {
                    MessageBoxW(hDlg, Tr(L"范围：行 3-60、列 20-300、字号 10-32。",
                                         L"Range: rows 3-60, columns 20-300, font size 10-32.").c_str(),
                                Tr(L"数值超出范围", L"Value out of range").c_str(), MB_OK | MB_ICONWARNING);
                }
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL)
            {
                DestroyWindow(hDlg);
                return TRUE;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hDlg);
            return TRUE;
        }
        return DefDlgProcW(hDlg, msg, wParam, lParam);
    }

    // 建一个和「转到」对话框同款的 #32770 面板，控件全部手建
    void ShowSettingsDialog(HWND owner)
    {
        HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"#32770",
                                    Tr(L"命令行设置", L"Console settings").c_str(),
                                    WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                    100, 100, 320, 230,
                                    owner, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!hDlg)
            return;

        HFONT hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        struct Item { const wchar_t *cls; const std::wstring &text; DWORD style; int x, y, w, h; int id; };
        const std::wstring lblRows = Tr(L"行数", L"Rows");
        const std::wstring lblCols = Tr(L"列数", L"Columns");
        const std::wstring lblFont = Tr(L"字号（像素）", L"Font size (px)");
        const std::wstring lblColor = Tr(L"启用 ANSI 彩色输出", L"ANSI colored output");
        const std::wstring hint = Tr(L"行 3-60、列 20-300、字号 10-32", L"Rows 3-60, columns 20-300, font 10-32");

        Item items[] = {
            {L"STATIC", lblRows, WS_CHILD | WS_VISIBLE, 18, 20, 92, 16, 0},
            {L"EDIT", lblRows, WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL, 118, 17, 70, 22, 1001},
            {L"STATIC", lblCols, WS_CHILD | WS_VISIBLE, 18, 52, 92, 16, 0},
            {L"EDIT", lblCols, WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL, 118, 49, 70, 22, 1002},
            {L"STATIC", lblFont, WS_CHILD | WS_VISIBLE, 18, 84, 92, 16, 0},
            {L"EDIT", lblFont, WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL, 118, 81, 70, 22, 1003},
            {L"BUTTON", lblColor, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 18, 116, 260, 22, 1004},
            {L"STATIC", hint, WS_CHILD | WS_VISIBLE, 18, 142, 280, 16, 0},
        };
        for (const Item &it : items)
        {
            const DWORD ex = (it.cls[0] == L'E') ? WS_EX_CLIENTEDGE : 0;
            HWND h = CreateWindowExW(ex, it.cls, it.text.c_str(), it.style,
                                     it.x, it.y, it.w, it.h, hDlg,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(it.id)), nullptr, nullptr);
            if (h)
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
        }
        CreateWindowExW(0, L"BUTTON", Tr(L"确定", L"OK").c_str(),
                        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 62, 176, 88, 26, hDlg,
                        reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", Tr(L"取消", L"Cancel").c_str(),
                        WS_CHILD | WS_VISIBLE, 168, 176, 88, 26, hDlg,
                        reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);
        for (HWND h = GetWindow(hDlg, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

        ConsoleSettingsDlgProc(hDlg, WM_INITDIALOG, 0, 0);
        SetWindowLongPtrW(hDlg, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ConsoleSettingsDlgProc));
        SetFocus(GetDlgItem(hDlg, 1001));
        SendMessageW(GetDlgItem(hDlg, 1001), EM_SETSEL, 0, -1);
    }

    // ---- 绘制 -----------------------------------------------------------

    // 基本 16 色（ANSI 顺序：0 黑 / 1 红 / 2 绿 / 3 黄 / 4 蓝 / 5 品红 / 6 青 / 7 白）
    COLORREF BasicColor(int idx, bool dark, COLORREF def)
    {
        switch (idx)
        {
        case 1: return dark ? RGB(120, 124, 132) : RGB(60, 60, 60);    // 黑
        case 2: return dark ? RGB(236, 118, 118) : RGB(186, 34, 34);   // 红
        case 3: return dark ? RGB(122, 200, 122) : RGB(24, 128, 56);   // 绿
        case 4: return dark ? RGB(230, 200, 110) : RGB(150, 110, 0);   // 黄
        case 5: return dark ? RGB(120, 170, 240) : RGB(30, 80, 190);   // 蓝
        case 6: return dark ? RGB(206, 130, 220) : RGB(150, 40, 160);  // 品红
        case 7: return dark ? RGB(110, 200, 210) : RGB(0, 130, 140);   // 青
        case 8: return dark ? RGB(210, 214, 220) : RGB(60, 60, 60);    // 白
        case 9: return dark ? RGB(150, 154, 162) : RGB(110, 110, 110); // 亮黑
        case 10: return dark ? RGB(255, 130, 130) : RGB(210, 20, 20);
        case 11: return dark ? RGB(150, 230, 150) : RGB(0, 150, 60);
        case 12: return dark ? RGB(250, 225, 130) : RGB(170, 130, 0);
        case 13: return dark ? RGB(150, 195, 255) : RGB(40, 100, 220);
        case 14: return dark ? RGB(230, 160, 240) : RGB(180, 40, 190);
        case 15: return dark ? RGB(140, 230, 240) : RGB(0, 150, 160);
        case 16: return dark ? RGB(245, 248, 255) : RGB(30, 30, 30);
        default: return def;
        }
    }

    // 256 色方案：16 基本色 + 6×6×6 立方 + 24 级灰阶
    COLORREF PaletteColor(int n, bool dark)
    {
        if (n < 0)
            return dark ? RGB(220, 224, 230) : RGB(30, 30, 30);
        if (n < 16)
            return BasicColor(n + 1, dark, 0);
        if (n < 232)
        {
            static const int levels[6] = {0, 95, 135, 175, 215, 255};
            const int idx = n - 16;
            const int r = levels[idx / 36];
            const int g = levels[(idx / 6) % 6];
            const int b = levels[idx % 6];
            return RGB(r, g, b);
        }
        const int v = 8 + (n - 232) * 10;
        return RGB(v, v, v);
    }

    COLORREF AnsiColor(unsigned short idx, bool dark, COLORREF def)
    {
        if (!g_con.color)
            return def;
        if (idx == 0)
            return def;
        if (idx <= 16)
            return BasicColor(static_cast<int>(idx), dark, def);
        return PaletteColor(static_cast<int>(idx) - 17, dark);
    }

    // 滚动条几何：轨道与滑块
    void GetScrollBarRects(RECT &track, RECT &thumb, int clientW)
    {
        const int x = clientW - 9;
        const int top = g_con.padY;
        const int height = g_con.rows * g_con.lineH;
        track = RECT{x, top, x + 5, top + height};

        if (g_con.layoutDirty)
            BuildLayout();
        const int total = static_cast<int>(g_con.layout.size());
        const int maxFirst = total > g_con.rows ? total - g_con.rows : 0;

        int thumbH = height;
        int thumbY = top;
        if (total > g_con.rows && total > 0)
        {
            thumbH = height * g_con.rows / total;
            if (thumbH < 24)
                thumbH = 24;
            if (thumbH > height)
                thumbH = height;
            const int span = height - thumbH;
            thumbY = top + (maxFirst > 0 ? (span * g_con.firstVisible) / maxFirst : 0);
        }
        thumb = RECT{x, thumbY, x + 5, thumbY + thumbH};
    }

    void PaintConsole(HWND hwnd)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);

        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
        SelectObject(mem, bmp);

        const bool dark = IsDarkMode();
        const COLORREF bg = dark ? RGB(24, 26, 30) : RGB(250, 250, 250);
        const COLORREF fg = dark ? RGB(220, 224, 230) : RGB(30, 30, 30);
        const COLORREF promptColor = dark ? RGB(120, 190, 255) : RGB(0, 90, 190);
        const COLORREF selBg = dark ? RGB(52, 74, 104) : RGB(206, 224, 246);
        const COLORREF findBg = dark ? RGB(72, 66, 40) : RGB(255, 240, 190);
        const COLORREF findCurBg = dark ? RGB(120, 104, 44) : RGB(255, 214, 96);
        const COLORREF busyColor = dark ? RGB(240, 190, 110) : RGB(176, 110, 0);
        const COLORREF hitColor = dark ? RGB(130, 136, 145) : RGB(150, 155, 162);

        HBRUSH bgBrush = CreateSolidBrush(bg);
        FillRect(mem, &rcClient, bgBrush);
        DeleteObject(bgBrush);

        SelectObject(mem, g_con.font);
        SetBkMode(mem, TRANSPARENT);

        if (g_con.layoutDirty)
            BuildLayout();
        if (g_con.findDirty && !g_con.findQuery.empty())
            RebuildFindHits();

        const int visible = g_con.rows;
        const int total = static_cast<int>(g_con.layout.size());
        int maxFirst = total - visible;
        if (maxFirst < 0)
            maxFirst = 0;
        if (g_con.firstVisible > maxFirst)
            g_con.firstVisible = maxFirst;
        if (g_con.firstVisible < 0)
            g_con.firstVisible = 0;

        // 选中范围（有序）
        int selL1 = -1, selC1 = 0, selL2 = -1, selC2 = 0;
        if (g_con.selAnchorLine >= 0)
        {
            const int al = g_con.selAnchorLine, ac = g_con.selAnchorCol;
            const int bl = g_con.selLine, bc = g_con.selCol;
            if (al <= bl)
            {
                selL1 = al; selC1 = ac; selL2 = bl; selC2 = bc;
            }
            else
            {
                selL1 = bl; selC1 = bc; selL2 = al; selC2 = ac;
            }
        }

        const int curHitLine =
            (g_con.findIndex >= 0 && g_con.findIndex < static_cast<int>(g_con.findHits.size()))
                ? g_con.findHits[static_cast<size_t>(g_con.findIndex)]
                : -1;

        for (int i = 0; i < visible; ++i)
        {
            const int rowIdx = g_con.firstVisible + i;
            if (rowIdx < 0 || rowIdx >= total)
                continue;
            const RowRef &r = g_con.layout[static_cast<size_t>(rowIdx)];
            const Line &ln = g_con.lines[static_cast<size_t>(r.line)];
            const int y = g_con.padY + i * g_con.lineH;

            if (r.line == curHitLine)
            {
                RECT sr = {0, y - 1, rcClient.right - kBarGap, y + g_con.lineH - 1};
                HBRUSH sb = CreateSolidBrush(findCurBg);
                FillRect(mem, &sr, sb);
                DeleteObject(sb);
            }
            else if (!g_con.findHits.empty() &&
                     std::binary_search(g_con.findHits.begin(), g_con.findHits.end(), r.line))
            {
                RECT sr = {0, y - 1, rcClient.right - kBarGap, y + g_con.lineH - 1};
                HBRUSH sb = CreateSolidBrush(findBg);
                FillRect(mem, &sr, sb);
                DeleteObject(sb);
            }

            if (selL1 >= 0 && r.line >= selL1 && r.line <= selL2)
            {
                int from = r.first;
                int to = r.last;
                if (r.line == selL1 && selC1 > from)
                    from = selC1;
                if (r.line == selL2 && selC2 < to)
                    to = selC2;
                if (from < to)
                {
                    const int x1 = g_con.padX + (from - r.first) * g_con.cellW;
                    const int x2 = g_con.padX + (to - r.first) * g_con.cellW;
                    RECT sr = {x1, y - 1, x2, y + g_con.lineH - 1};
                    HBRUSH sb = CreateSolidBrush(selBg);
                    FillRect(mem, &sr, sb);
                    DeleteObject(sb);
                }
            }

            int x = g_con.padX;
            int k = r.first;
            while (k < r.last && k < static_cast<int>(ln.cells.size()))
            {
                const unsigned short color = g_con.color ? ln.cells[static_cast<size_t>(k)].color : 0;
                std::wstring run;
                while (k < r.last && k < static_cast<int>(ln.cells.size()) &&
                       (g_con.color ? ln.cells[static_cast<size_t>(k)].color : 0) == color)
                {
                    run += ln.cells[static_cast<size_t>(k)].ch == 0 ? L' ' : ln.cells[static_cast<size_t>(k)].ch;
                    ++k;
                }
                if (run.empty())
                    break;
                SetTextColor(mem, AnsiColor(color, dark, fg));
                TextOutW(mem, x, y, run.c_str(), static_cast<int>(run.size()));
                SIZE sz = {};
                GetTextExtentPoint32W(mem, run.c_str(), static_cast<int>(run.size()), &sz);
                x += sz.cx;
            }
        }

        // 滚动条
        RECT track, thumb;
        GetScrollBarRects(track, thumb, rcClient.right);
        if (total > visible)
        {
            HBRUSH tb = CreateSolidBrush(dark ? RGB(40, 43, 48) : RGB(228, 230, 234));
            FillRect(mem, &track, tb);
            DeleteObject(tb);
            HBRUSH thb = CreateSolidBrush(dark ? RGB(96, 102, 112) : RGB(160, 166, 174));
            FillRect(mem, &thumb, thb);
            DeleteObject(thb);
        }

        // 输入行：提示符 + 输入 + 光标
        const int inputTop = g_con.padY + visible * g_con.lineH + 2;
        HBRUSH lineBrush = CreateSolidBrush(dark ? RGB(38, 41, 46) : RGB(236, 238, 242));
        RECT inputRect = {2, inputTop - 2, rcClient.right - 2, rcClient.bottom - 2};
        FillRect(mem, &inputRect, lineBrush);
        DeleteObject(lineBrush);

        if (g_con.findMode)
        {
            const std::wstring label = Tr(L"查找：", L"Find: ");
            const std::wstring body = label + g_con.findQuery;
            SetTextColor(mem, promptColor);
            TextOutW(mem, g_con.padX, inputTop, label.c_str(), static_cast<int>(label.size()));
            SetTextColor(mem, fg);
            TextOutW(mem, g_con.padX + static_cast<int>(label.size()) * g_con.cellW, inputTop,
                     g_con.findQuery.c_str(), static_cast<int>(g_con.findQuery.size()));

            SetTextColor(mem, hitColor);
            RECT br = {rcClient.right - 320 - g_con.padX, inputTop, rcClient.right - g_con.padX,
                       inputTop + g_con.lineH};
            DrawTextW(mem, Tr(L"Enter / F3 下一个 · Esc 退出", L"Enter / F3 next · Esc closes").c_str(),
                      -1, &br, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);

            if (g_con.caretOn)
            {
                const int cx = g_con.padX + (static_cast<int>(body.size())) * g_con.cellW;
                RECT cr = {cx, inputTop, cx + 1, inputTop + g_con.lineH};
                HBRUSH caretBrush = CreateSolidBrush(fg);
                FillRect(mem, &cr, caretBrush);
                DeleteObject(caretBrush);
            }
        }
        else
        {
            std::wstring prompt = g_con.cwd + L">";
            std::wstring body = prompt + g_con.input;
            const int caretPos = static_cast<int>(prompt.size()) + g_con.caret;
            int startIdx = 0;
            if (static_cast<int>(body.size()) > g_con.cols && caretPos > g_con.cols - 1)
                startIdx = caretPos - (g_con.cols - 1);
            std::wstring shown = body.substr(static_cast<size_t>(startIdx));
            if (static_cast<int>(shown.size()) > g_con.cols)
                shown.resize(static_cast<size_t>(g_con.cols));

            const int promptLen = static_cast<int>(prompt.size()) - startIdx;
            if (promptLen > 0)
            {
                SetTextColor(mem, promptColor);
                std::wstring p = shown.substr(0, static_cast<size_t>(promptLen));
                TextOutW(mem, g_con.padX, inputTop, p.c_str(), static_cast<int>(p.size()));
            }
            SetTextColor(mem, fg);
            if (static_cast<int>(shown.size()) > promptLen)
            {
                std::wstring b = shown.substr(static_cast<size_t>(promptLen > 0 ? promptLen : 0));
                TextOutW(mem, g_con.padX + (promptLen > 0 ? promptLen * g_con.cellW : 0), inputTop,
                         b.c_str(), static_cast<int>(b.size()));
            }

            if (g_con.running)
            {
                SetTextColor(mem, busyColor);
                RECT br = {rcClient.right - 380 - g_con.padX, inputTop, rcClient.right - g_con.padX,
                           inputTop + g_con.lineH};
                DrawTextW(mem, Tr(L"运行中… Enter 送入输入 · Ctrl+C 中断",
                                  L"Running… Enter sends input · Ctrl+C stops").c_str(),
                          -1, &br, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
            }
            else if (g_con.input.empty() && g_con.history.empty())
            {
                SetTextColor(mem, hitColor);
                RECT br = {rcClient.right - 400 - g_con.padX, inputTop, rcClient.right - g_con.padX,
                           inputTop + g_con.lineH};
                DrawTextW(mem, Tr(L"Tab 补全路径 · Ctrl+F 查找 · Ctrl+V 粘贴",
                                  L"Tab completes paths · Ctrl+F finds · Ctrl+V pastes").c_str(),
                          -1, &br, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
            }

            if (g_con.caretOn)
            {
                const int cx = g_con.padX + (caretPos - startIdx) * g_con.cellW;
                RECT cr = {cx, inputTop, cx + 1, inputTop + g_con.lineH};
                HBRUSH caretBrush = CreateSolidBrush(fg);
                FillRect(mem, &cr, caretBrush);
                DeleteObject(caretBrush);
            }
        }

        BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, mem, 0, 0, SRCCOPY);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
    }

    // ---- 输出区选中 / 剪贴板 --------------------------------------------

    void HitTest(int x, int y, int &line, int &col)
    {
        if (g_con.layoutDirty)
            BuildLayout();
        line = -1;
        col = 0;
        const int total = static_cast<int>(g_con.layout.size());
        if (total <= 0)
            return;
        if (y >= g_con.padY + g_con.rows * g_con.lineH)
            return; // 点在输入行上，不落在输出区
        int row = g_con.firstVisible + (y - g_con.padY) / (g_con.lineH ? g_con.lineH : 18);
        if (row < 0)
            row = 0;
        if (row >= total)
            row = total - 1;
        const RowRef &r = g_con.layout[static_cast<size_t>(row)];
        line = r.line;
        int cx = (x - g_con.padX) / (g_con.cellW ? g_con.cellW : 8);
        if (cx < 0)
            cx = 0;
        col = r.first + cx;
        const int cells = static_cast<int>(g_con.lines[static_cast<size_t>(line)].cells.size());
        if (col > cells)
            col = cells;
    }

    std::wstring SelectionText()
    {
        if (g_con.selAnchorLine < 0)
            return std::wstring();

        const int al = g_con.selAnchorLine, ac = g_con.selAnchorCol;
        const int bl = g_con.selLine, bc = g_con.selCol;
        const int l1 = (std::min)(al, bl);
        const int l2 = (std::max)(al, bl);
        const int c1 = al <= bl ? ac : bc;
        const int c2 = al <= bl ? bc : ac;

        std::wstring out;
        for (int i = l1; i <= l2 && i < static_cast<int>(g_con.lines.size()); ++i)
        {
            if (i < 0)
                continue;
            const std::wstring text = LineText(g_con.lines[static_cast<size_t>(i)]);
            const int len = static_cast<int>(text.size());
            int from = (i == l1) ? ClampInt(c1, 0, len) : 0;
            int to = (i == l2) ? ClampInt(c2, 0, len) : len;
            if (to < from)
                to = from;
            out += TrimRightText(text.substr(static_cast<size_t>(from),
                                             static_cast<size_t>(to - from)));
            out += L"\r\n";
        }
        return out;
    }

    void SetClipboardText(const std::wstring &text)
    {
        if (text.empty() || !OpenClipboard(g_con.hwnd))
            return;
        EmptyClipboard();
        const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (mem)
        {
            void *dst = GlobalLock(mem);
            if (dst)
            {
                memcpy(dst, text.c_str(), bytes);
                GlobalUnlock(mem);
                if (SetClipboardData(CF_UNICODETEXT, mem))
                    mem = nullptr; // 交给剪贴板管理
            }
            if (mem)
                GlobalFree(mem);
        }
        CloseClipboard();
    }

    std::wstring ClipboardText()
    {
        std::wstring out;
        if (!OpenClipboard(g_con.hwnd))
            return out;
        if (IsClipboardFormatAvailable(CF_UNICODETEXT))
        {
            HANDLE h = GetClipboardData(CF_UNICODETEXT);
            if (h)
            {
                const wchar_t *p = static_cast<const wchar_t *>(GlobalLock(h));
                if (p)
                {
                    out = p;
                    GlobalUnlock(h);
                }
            }
        }
        CloseClipboard();
        return out;
    }

    void CopyOutput(bool all)
    {
        std::wstring text;
        if (all)
        {
            for (size_t i = 0; i < g_con.lines.size(); ++i)
            {
                text += TrimRightText(LineText(g_con.lines[i]));
                text += L"\r\n";
            }
        }
        else
        {
            text = SelectionText();
        }
        if (text.empty())
        {
            SplitAppend(Tr(L"（没有选中的内容，右键可「复制全部输出」）\r\n",
                          L"(nothing selected — use “Copy all output” in the context menu)\r\n"));
            return;
        }
        SetClipboardText(text);
        SplitAppend(Tr(L"（已复制 " + std::to_wstring(text.size()) + L" 个字符到剪贴板）\r\n",
                      L"(copied " + std::to_wstring(text.size()) + L" chars to the clipboard)\r\n"));
    }

    // ---- 命令执行 -------------------------------------------------------

    struct RunJob
    {
        HWND hwnd;
        std::wstring cmdline;
        std::wstring dir;
        HANDLE job;
        HANDLE outRead;
        HANDLE outWrite;
        HANDLE inRead;
        HANDLE inWrite;
    };

    std::wstring DecodeBytes(const char *data, int len)
    {
        if (len <= 0)
            return std::wstring();
        int need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, len, nullptr, 0);
        if (need > 0)
        {
            std::wstring out(static_cast<size_t>(need), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, data, len, &out[0], need);
            return out;
        }
        need = MultiByteToWideChar(CP_OEMCP, 0, data, len, nullptr, 0);
        if (need <= 0)
            return std::wstring();
        std::wstring out(static_cast<size_t>(need), L'\0');
        MultiByteToWideChar(CP_OEMCP, 0, data, len, &out[0], need);
        return out;
    }

    DWORD WINAPI RunWorker(LPVOID param)
    {
        RunJob *job = static_cast<RunJob *>(param);

        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
        si.wShowWindow = SW_HIDE;
        si.hStdOutput = job->outWrite;
        si.hStdError = job->outWrite;
        si.hStdInput = job->inRead;

        PROCESS_INFORMATION pi = {};
        std::wstring cmdline = job->cmdline;
        BOOL ok = CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, TRUE,
                                 CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP,
                                 nullptr, job->dir.empty() ? nullptr : job->dir.c_str(), &si, &pi);
        CloseHandle(job->outWrite);
        job->outWrite = nullptr;
        if (job->inRead)
        {
            CloseHandle(job->inRead);
            job->inRead = nullptr;
        }
        if (!ok)
        {
            PostsAppend(Tr(L"（无法启动进程，路径或权限有问题）\r\n",
                          L"(cannot start the process — check the path or permissions)\r\n"));
            CloseHandle(job->outRead);
            PostMessageW(job->hwnd, WM_APP_CONSOLE_DONE, 1, 0);
            delete job;
            return 0;
        }

        if (job->job)
            AssignProcessToJobObject(job->job, pi.hProcess);
        ResumeThread(pi.hThread);

        std::vector<char> pending;
        pending.reserve(8192);

        auto flush = [&pending](bool all) {
            if (pending.empty())
                return;
            size_t cut = 0;
            if (all)
            {
                cut = pending.size();
            }
            else
            {
                size_t pos = pending.size();
                size_t nl = 0;
                bool found = false;
                while (pos > 0)
                {
                    if (pending[pos - 1] == '\n')
                    {
                        nl = pos;
                        found = true;
                        break;
                    }
                    --pos;
                }
                if (!found)
                    return; // 没有换行就先攒着，避免把多字节字符切断
                cut = nl;
            }
            if (cut == 0)
                return;
            PostsAppend(DecodeBytes(pending.data(), static_cast<int>(cut)));
            pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(cut));
        };

        char buf[4096];
        DWORD got = 0;
        while (ReadFile(job->outRead, buf, sizeof(buf) - 1, &got, nullptr) && got > 0)
        {
            pending.insert(pending.end(), buf, buf + got);
            flush(false);
            if (pending.size() > 8192)
                flush(true);
        }
        flush(true);
        CloseHandle(job->outRead);

        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);

        PostMessageW(job->hwnd, WM_APP_CONSOLE_DONE, static_cast<WPARAM>(code), 0);
        delete job;
        return 0;
    }

    void CloseStdinWrite()
    {
        if (g_con.stdinWrite)
        {
            CloseHandle(g_con.stdinWrite);
            g_con.stdinWrite = nullptr;
        }
    }

    void RunCommandAsync(const std::wstring &command, const std::wstring &dir)
    {
        RunJob *job = new RunJob();
        job->hwnd = g_con.hwnd;
        job->cmdline = L"cmd.exe /C " + command;
        job->dir = dir;
        job->job = CreateJobObjectW(nullptr, nullptr);
        job->outRead = nullptr;
        job->outWrite = nullptr;
        job->inRead = nullptr;
        job->inWrite = nullptr;

        if (job->job)
        {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {};
            li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(job->job, JobObjectExtendedLimitInformation, &li, sizeof(li));
        }

        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE outRead = nullptr, outWrite = nullptr, inRead = nullptr, inWrite = nullptr;
        if (!CreatePipe(&outRead, &outWrite, &sa, 0))
        {
            PostsAppend(Tr(L"（无法创建输出管道）\r\n", L"(cannot create the output pipe)\r\n"));
            CloseStdinWrite();
            if (job->job)
                CloseHandle(job->job);
            g_con.running = false;
            delete job;
            return;
        }
        SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

        // 标准输入：保留写端，运行中可以继续往里灌数据（交互式程序）
        if (CreatePipe(&inRead, &inWrite, &sa, 0))
        {
            SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
        }
        else
        {
            inRead = nullptr;
            inWrite = nullptr;
        }

        job->outRead = outRead;
        job->outWrite = outWrite;
        job->inRead = inRead;
        job->inWrite = inWrite;

        CloseStdinWrite();
        g_con.stdinWrite = inWrite;
        g_con.job = job->job;

        HANDLE t = CreateThread(nullptr, 0, RunWorker, job, 0, nullptr);
        if (t)
        {
            CloseHandle(t);
        }
        else
        {
            CloseStdinWrite();
            if (g_con.job)
            {
                CloseHandle(g_con.job);
                g_con.job = nullptr;
            }
            CloseHandle(outRead);
            CloseHandle(outWrite);
            if (inRead)
                CloseHandle(inRead);
            g_con.running = false;
            delete job;
            PostsAppend(Tr(L"（无法创建执行线程）\r\n", L"(cannot create the worker thread)\r\n"));
        }
    }

    // 给运行中的进程送一行标准输入（管道按 ANSI 代码页转字节）
    bool SendStdinLine(const std::wstring &line)
    {
        if (!g_con.stdinWrite)
            return false;
        const std::wstring text = line + L"\r\n";
        int need = WideCharToMultiByte(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()),
                                       nullptr, 0, nullptr, nullptr);
        if (need <= 0)
            return false;
        std::string bytes(static_cast<size_t>(need), '\0');
        WideCharToMultiByte(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()),
                            bytes.data(), need, nullptr, nullptr);
        DWORD written = 0;
        return WriteFile(g_con.stdinWrite, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE;
    }

    void StopRunning()
    {
        if (!g_con.running)
        {
            SplitAppend(Tr(L"（没有正在运行的进程）\r\n", L"(no process is running)\r\n"));
            return;
        }
        if (g_con.job)
            TerminateJobObject(g_con.job, 1);
        SplitAppend(L"^C\r\n");
    }

    // ---- 输入编辑 -------------------------------------------------------

    void SaveOutputToFile()
    {
        wchar_t file[MAX_PATH] = L"console.txt";
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = g_con.hwnd;
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = L"Text (*.txt)\0*.txt\0All files (*.*)\0*.*\0\0";
        ofn.lpstrDefExt = L"txt";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
        if (!GetSaveFileNameW(&ofn))
            return;

        std::wstring text;
        for (size_t i = 0; i < g_con.lines.size(); ++i)
        {
            text += TrimRightText(LineText(g_con.lines[i]));
            text += L"\r\n";
        }

        std::string bytes;
        bytes.push_back('\xEF');
        bytes.push_back('\xBB');
        bytes.push_back('\xBF');
        int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (n > 0)
        {
            const size_t old = bytes.size();
            bytes.resize(old + static_cast<size_t>(n));
            WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                bytes.data() + old, n, nullptr, nullptr);
        }

        HANDLE h = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            SplitAppend(Tr(L"（无法写入文件：", L"(cannot write file: ") + std::wstring(file) + Tr(L"）\r\n", L")\r\n"));
            return;
        }
        DWORD written = 0;
        WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
        CloseHandle(h);
        SplitAppend(Tr(L"已导出输出到：", L"output written to: ") + std::wstring(file) + L"\r\n");
    }

    void CompleteInput()
    {
        size_t start = g_con.input.size();
        while (start > 0)
        {
            const wchar_t c = g_con.input[start - 1];
            if (c == L' ' || c == L'\t' || c == L'"')
                break;
            --start;
        }

        const std::wstring token = g_con.input.substr(start);
        std::wstring dirPart, namePrefix;
        const size_t slash = token.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
        {
            dirPart = token.substr(0, slash + 1);
            namePrefix = token.substr(slash + 1);
        }
        else
        {
            namePrefix = token;
        }

        const std::wstring base = g_con.input.substr(0, start) + dirPart;
        std::wstring searchDir = dirPart.empty() ? g_con.cwd : dirPart;
        if (!dirPart.empty() && !(dirPart.size() > 1 && dirPart[1] == L':') && dirPart[0] != L'\\')
            searchDir = JoinPath(g_con.cwd, dirPart);
        if (searchDir.size() == 2 && searchDir[1] == L':')
            searchDir += L"\\";

        static std::wstring s_base;
        static std::vector<std::wstring> s_cands;
        static int s_index = 0;

        if (s_base == base && !s_cands.empty() && s_index >= 0)
        {
            s_index = (s_index + 1) % static_cast<int>(s_cands.size());
        }
        else
        {
            s_cands.clear();
            s_index = 0;
            std::wstring pattern = JoinPath(searchDir, namePrefix + L"*");
            WIN32_FIND_DATAW fd = {};
            HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
            if (find != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0 ||
                                                    (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
                        continue;
                    const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    std::wstring name = fd.cFileName;
                    if (isDir)
                        name += L"\\";
                    if (_wcsnicmp(name.c_str(), namePrefix.c_str(), namePrefix.size()) == 0)
                        s_cands.push_back(name);
                } while (FindNextFileW(find, &fd));
                FindClose(find);
            }
            std::sort(s_cands.begin(), s_cands.end(),
                      [](const std::wstring &a, const std::wstring &b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
        }
        s_base = base;
        if (s_cands.empty() || s_index >= static_cast<int>(s_cands.size()))
            return;

        g_con.input = base + s_cands[static_cast<size_t>(s_index)];
        g_con.caret = static_cast<int>(g_con.input.size());
        RedrawConsole();
    }

    void PasteToInput()
    {
        std::wstring text = ClipboardText();
        if (text.empty())
            return;
        std::wstring clean;
        for (size_t i = 0; i < text.size(); ++i)
        {
            const wchar_t c = text[i];
            if (c == L'\r' || c == L'\n' || c == L'\t')
            {
                clean += L' ';
                continue;
            }
            if (c < 32)
                continue;
            clean += c;
        }
        if (clean.empty())
            return;
        while (!clean.empty() && clean.back() == L' ')
            clean.pop_back();
        if (clean.empty())
            return;

        // 运行中的进程：直接把这行当标准输入送进去
        if (g_con.running)
        {
            SplitAppend(L"> " + clean + L"\r\n");
            if (!SendStdinLine(clean))
                SplitAppend(Tr(L"（无法写入标准输入）\r\n", L"(cannot write to stdin)\r\n"));
            return;
        }

        if (g_con.input.size() + clean.size() > 2000)
            clean.resize(2000 - g_con.input.size());
        g_con.input.insert(static_cast<size_t>(g_con.caret), clean);
        g_con.caret += static_cast<int>(clean.size());
        RedrawConsole();
    }

    void OpenInExplorer(const std::wstring &target)
    {
        if (target.empty())
            return;
        ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void ExecuteInput()
    {
        std::wstring cmd = g_con.input;
        while (!cmd.empty() && cmd.front() == L' ')
            cmd.erase(cmd.begin());
        while (!cmd.empty() && cmd.back() == L' ')
            cmd.pop_back();

        // 有进程在跑：这一行当作它的标准输入（空行就是敲一下回车）
        if (g_con.running)
        {
            g_con.followTail = true;
            SplitAppend(L"> " + cmd + L"\r\n");
            g_con.input.clear();
            g_con.caret = 0;
            g_con.historyIndex = -1;
            if (!SendStdinLine(cmd))
                SplitAppend(Tr(L"（该进程不接受标准输入，管道已关闭）\r\n",
                              L"(this process does not accept stdin — the pipe is closed)\r\n"));
            RedrawConsole();
            return;
        }

        // 回显命令行
        g_con.followTail = true;
        SplitAppend(g_con.cwd + L">" + cmd + L"\r\n");
        g_con.input.clear();
        g_con.caret = 0;
        if (!cmd.empty() && (g_con.history.empty() || g_con.history.back() != cmd))
        {
            g_con.history.push_back(cmd);
            if (g_con.history.size() > 200)
                g_con.history.erase(g_con.history.begin());
            SaveConsoleHistory();
        }
        g_con.historyIndex = -1;

        if (cmd.empty())
        {
            RedrawConsole();
            return;
        }

        // 内置命令
        if (cmd == L"cls" || cmd == L"clear")
        {
            ClearOutput();
            return;
        }
        if (cmd == L"exit" || cmd == L"quit")
        {
            ConsoleClose();
            return;
        }
        if (cmd == L"stop")
        {
            StopRunning();
            return;
        }
        if (cmd == L"help" || cmd == L"?")
        {
            if (GetCurrentLanguage() == LangID::ZH_CN || GetCurrentLanguage() == LangID::ZH_TW)
            {
                SplitAppend(
                    L"内置命令：\r\n"
                    L"  cls / clear      清屏\r\n"
                    L"  pwd              显示当前目录（等同直接输入 cd）\r\n"
                    L"  cd <目录>        切换工作目录（支持 .. 与相对路径）\r\n"
                    L"  open [路径]      用资源管理器打开当前目录或指定路径\r\n"
                    L"  history [n]      列出最近 n 条命令（默认 20）\r\n"
                    L"  size <行> <列>   调整命令行窗口的字符网格（默认 5 行 × 50 列）\r\n"
                    L"  font <字号>      设置输出字号（10–32，可 Ctrl+滚轮 直接缩放）\r\n"
                    L"  stop             中断正在运行的进程（等同 Ctrl+C）\r\n"
                    L"  save [文件]      把当前输出导出为 UTF-8 文本\r\n"
                    L"  help             显示本说明\r\n"
                    L"  exit             关闭命令行窗口\r\n"
                    L"其余输入交给 cmd.exe /C 执行，工作目录同上；进程运行中输入的内容会作为\r\n"
                    L"标准输入送入该进程（可跑 readline / scanf / input() 之类的交互程序）。\r\n"
                    L"快捷键：Tab 补全路径、Ctrl+V 粘贴、Ctrl+F 查找输出、Ctrl+A 全选、\r\n"
                    L"        Ctrl+滚轮 缩放字号、PgUp/PgDn 翻页、右键菜单复制与导出。\r\n");
            }
            else
            {
                SplitAppend(
                    L"Built-in commands:\r\n"
                    L"  cls / clear      clear the screen\r\n"
                    L"  pwd              print the working directory\r\n"
                    L"  cd <dir>         change the working directory (.. and relative paths ok)\r\n"
                    L"  open [path]      open the working directory (or a path) in Explorer\r\n"
                    L"  history [n]      list the last n commands (default 20)\r\n"
                    L"  size <rows> <cols>  resize the character grid (default 5 x 50)\r\n"
                    L"  font <size>      output font size (10-32, Ctrl+wheel also zooms)\r\n"
                    L"  stop             terminate the running process (same as Ctrl+C)\r\n"
                    L"  save [file]      export the output as UTF-8 text\r\n"
                    L"  help             show this help\r\n"
                    L"  exit             close the console window\r\n"
                    L"Anything else runs through cmd.exe /C in the same directory. While a\r\n"
                    L"process is running, typed lines are sent to it as standard input.\r\n"
                    L"Keys: Tab completes paths, Ctrl+V pastes, Ctrl+F finds in the output,\r\n"
                    L"      Ctrl+A selects all, Ctrl+wheel zooms, PgUp/PgDn page, right-click menu.\r\n");
            }
            return;
        }
        if (cmd == L"pwd")
        {
            SplitAppend(g_con.cwd + L"\r\n");
            return;
        }
        if (cmd == L"open" || cmd == L"explorer")
        {
            OpenInExplorer(g_con.cwd);
            return;
        }
        if (cmd.compare(0, 5, L"open ") == 0)
        {
            std::wstring arg = cmd.substr(5);
            while (!arg.empty() && arg.front() == L' ')
                arg.erase(arg.begin());
            if (arg.size() >= 2 && arg.front() == L'"' && arg.back() == L'"')
                arg = arg.substr(1, arg.size() - 2);
            const bool absolute = (arg.size() > 1 && arg[1] == L':') || (!arg.empty() && arg[0] == L'\\');
            OpenInExplorer(absolute ? arg : JoinPath(g_con.cwd, arg));
            SplitAppend(Tr(L"已在资源管理器中打开：", L"opened in Explorer: ") + arg + L"\r\n");
            return;
        }
        if (cmd == L"history" || cmd.compare(0, 8, L"history ") == 0 || cmd == L"hist")
        {
            int n = 20;
            if (cmd.size() > 8)
                n = _wtoi(cmd.c_str() + 8);
            if (n <= 0 || n > 200)
                n = 20;
            const int total = static_cast<int>(g_con.history.size());
            const int from = total > n ? total - n : 0;
            for (int i = from; i < total; ++i)
                SplitAppend(L"  " + std::to_wstring(i + 1) + L"  " +
                            g_con.history[static_cast<size_t>(i)] + L"\r\n");
            return;
        }
        if (cmd == L"cd" || cmd.compare(0, 3, L"cd ") == 0 || cmd.compare(0, 3, L"cd\t") == 0)
        {
            std::wstring arg = cmd.size() > 2 ? cmd.substr(2) : std::wstring();
            while (!arg.empty() && (arg.front() == L' ' || arg.front() == L'\t'))
                arg.erase(arg.begin());
            while (!arg.empty() && (arg.back() == L' ' || arg.back() == L'\t'))
                arg.pop_back();
            if (arg.empty())
            {
                SplitAppend(g_con.cwd + L"\r\n");
                return;
            }
            if (arg.size() >= 2 && (arg.front() == L'"') && arg.back() == L'"')
                arg = arg.substr(1, arg.size() - 2);

            const bool absolute = (arg.size() > 1 && arg[1] == L':') || arg[0] == L'\\';
            std::wstring target = absolute ? arg : JoinPath(g_con.cwd, arg);
            wchar_t full[MAX_PATH] = {0};
            if (GetFullPathNameW(target.c_str(), MAX_PATH, full, nullptr) > 0 && PathIsDirectoryW(full))
            {
                g_con.cwd = NormalizeDir(full);
                SplitAppend(Tr(L"当前目录：", L"working directory: ") + g_con.cwd + L"\r\n");
            }
            else
            {
                SplitAppend(Tr(L"（找不到目录：", L"(no such directory: ") + arg + Tr(L"）\r\n", L")\r\n"));
            }
            return;
        }
        if (cmd == L"size" || cmd.compare(0, 5, L"size ") == 0)
        {
            int r = 0, c = 0;
            if (swscanf_s(cmd.c_str() + 4, L"%d %d", &r, &c) == 2)
            {
                ConsoleSetDefaultSize(r, c);
                g_con.rows = ClampInt(r, 3, 60);
                g_con.cols = ClampInt(c, 20, 300);
                SaveConsoleSize(g_con.rows, g_con.cols);
                g_con.layoutDirty = true;
                EnsureSize();
                SplitAppend(Tr(L"命令行网格：", L"console grid: ") + std::to_wstring(g_con.rows) +
                            Tr(L" 行 × ", L" rows x ") + std::to_wstring(g_con.cols) +
                            Tr(L" 列\r\n", L" columns\r\n"));
                RedrawConsole();
            }
            else
            {
                SplitAppend(Tr(L"用法：size <行> <列>\r\n", L"usage: size <rows> <cols>\r\n"));
            }
            return;
        }
        if (cmd == L"font" || cmd.compare(0, 5, L"font ") == 0)
        {
            int px = _wtoi(cmd.c_str() + 4);
            if (px >= 10 && px <= 32)
            {
                SetFontSize(px);
                SplitAppend(Tr(L"输出字号：", L"font size: ") + std::to_wstring(px) + L"\r\n");
            }
            else
            {
                SplitAppend(Tr(L"用法：font <10-32>\r\n", L"usage: font <10-32>\r\n"));
            }
            return;
        }
        if (cmd.compare(0, 5, L"save ") == 0 || cmd == L"save")
        {
            if (cmd == L"save")
            {
                SaveOutputToFile();
                return;
            }
            std::wstring file = cmd.substr(5);
            while (!file.empty() && file.front() == L' ')
                file.erase(file.begin());
            if (file.size() >= 2 && file.front() == L'"' && file.back() == L'"')
                file = file.substr(1, file.size() - 2);
            const bool absolute = (file.size() > 1 && file[1] == L':') || (file.size() > 0 && file[0] == L'\\');
            const std::wstring path = absolute ? file : JoinPath(g_con.cwd, file);

            std::wstring text;
            for (size_t i = 0; i < g_con.lines.size(); ++i)
            {
                text += TrimRightText(LineText(g_con.lines[i]));
                text += L"\r\n";
            }
            std::string bytes;
            bytes.push_back('\xEF');
            bytes.push_back('\xBB');
            bytes.push_back('\xBF');
            int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            if (n > 0)
            {
                const size_t old = bytes.size();
                bytes.resize(old + static_cast<size_t>(n));
                WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                    bytes.data() + old, n, nullptr, nullptr);
            }
            HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE)
            {
                SplitAppend(Tr(L"（无法写入：", L"(cannot write: ") + path + Tr(L"）\r\n", L")\r\n"));
                return;
            }
            DWORD written = 0;
            WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
            CloseHandle(h);
            SplitAppend(Tr(L"已导出输出到：", L"output written to: ") + path + L"\r\n");
            return;
        }

        g_con.running = true;
        RunCommandAsync(cmd, g_con.cwd);
        RedrawConsole();
    }

    // ---- 窗口过程 -------------------------------------------------------

    enum
    {
        IDC_COPY = 1,
        IDC_COPY_ALL,
        IDC_PASTE,
        IDC_CLEAR,
        IDC_SAVE,
        IDC_STOP,
        IDC_CLOSE,
        IDC_FIND,
        IDC_OPEN_DIR
    };

    void ShowContextMenu(HWND hwnd, int x, int y)
    {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, IDC_COPY, Tr(L"复制选中", L"Copy selection").c_str());
        AppendMenuW(menu, MF_STRING, IDC_COPY_ALL, Tr(L"复制全部输出", L"Copy all output").c_str());
        AppendMenuW(menu, MF_STRING, IDC_PASTE, Tr(L"粘贴", L"Paste").c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDC_FIND, Tr(L"查找输出…", L"Find in output…").c_str());
        AppendMenuW(menu, MF_STRING, IDC_CLEAR, Tr(L"清屏", L"Clear").c_str());
        AppendMenuW(menu, MF_STRING, IDC_SAVE, Tr(L"导出输出…", L"Export output…").c_str());
        AppendMenuW(menu, MF_STRING, IDC_OPEN_DIR, Tr(L"在资源管理器中打开目录", L"Open directory in Explorer").c_str());
        AppendMenuW(menu, g_con.running ? MF_STRING : (MF_STRING | MF_GRAYED), IDC_STOP,
                    Tr(L"停止当前进程", L"Stop the running process").c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDC_CLOSE, Tr(L"关闭命令行", L"Close console").c_str());

        const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, hwnd, nullptr);
        DestroyMenu(menu);
        switch (cmd)
        {
        case IDC_COPY: CopyOutput(false); break;
        case IDC_COPY_ALL: CopyOutput(true); break;
        case IDC_PASTE: PasteToInput(); break;
        case IDC_FIND:
            g_con.findMode = true;
            g_con.findIndex = -1;
            g_con.findDirty = true;
            RedrawConsole();
            break;
        case IDC_CLEAR: ClearOutput(); break;
        case IDC_SAVE: SaveOutputToFile(); break;
        case IDC_OPEN_DIR: OpenInExplorer(g_con.cwd); break;
        case IDC_STOP: StopRunning(); break;
        case IDC_CLOSE: ConsoleClose(); break;
        default: break;
        }
    }

    LRESULT CALLBACK ConsoleProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
        {
            g_con.hwnd = hwnd;
            if (g_con.lines.empty())
                g_con.lines.push_back(Line());
            CreateConsoleFont(hwnd);
            return 0;
        }
        case WM_GETMINMAXINFO:
        {
            MINMAXINFO *mmi = reinterpret_cast<MINMAXINFO *>(lParam);
            mmi->ptMinTrackSize.x = 20 * g_con.cellW + g_con.padX * 2 + kBarGap + 20;
            mmi->ptMinTrackSize.y = 3 * g_con.lineH + g_con.inputH + g_con.padY * 2 + 44;
            return 0;
        }
        case WM_SIZE:
        {
            const int clientW = static_cast<int>(LOWORD(lParam));
            const int clientH = static_cast<int>(HIWORD(lParam));
            const int newCols = ClampInt((clientW - g_con.padX * 2 - kBarGap) /
                                             (g_con.cellW ? g_con.cellW : 8),
                                         20, 300);
            const int newRows = ClampInt((clientH - g_con.padY * 2 - g_con.inputH) /
                                             (g_con.lineH ? g_con.lineH : 18),
                                         3, 60);
            if (newCols != g_con.cols || newRows != g_con.rows)
            {
                g_con.cols = newCols;
                g_con.rows = newRows;
                g_con.layoutDirty = true;
                if (g_con.followTail)
                {
                    BuildLayout();
                    const int total = static_cast<int>(g_con.layout.size());
                    g_con.firstVisible = total > g_con.rows ? total - g_con.rows : 0;
                }
            }
            return 0;
        }
        case WM_EXITSIZEMOVE:
            SaveConsoleSize(g_con.rows, g_con.cols);
            return 0;
        case WM_SETFOCUS:
            SetTimer(hwnd, 1, 500, nullptr);
            g_con.caretOn = true;
            RedrawConsole();
            return 0;
        case WM_KILLFOCUS:
            KillTimer(hwnd, 1);
            g_con.caretOn = false;
            RedrawConsole();
            return 0;
        case WM_TIMER:
            if (wParam == 1)
            {
                g_con.caretOn = !g_con.caretOn;
                RECT rc;
                GetClientRect(hwnd, &rc);
                rc.top = g_con.padY + g_con.rows * g_con.lineH;
                InvalidateRect(hwnd, &rc, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN:
        {
            SetFocus(hwnd);
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);

            // 滚动条
            RECT track, thumb, rcClient;
            GetClientRect(hwnd, &rcClient);
            GetScrollBarRects(track, thumb, rcClient.right);
            if (x >= track.left - 2 && x <= track.right + 2 && y >= track.top && y <= track.bottom)
            {
                if (g_con.layoutDirty)
                    BuildLayout();
                const int total = static_cast<int>(g_con.layout.size());
                if (total <= g_con.rows)
                    return 0;
                if (y >= thumb.top && y <= thumb.bottom)
                {
                    g_con.barDragging = true;
                    g_con.barGrabOffset = y - thumb.top;
                }
                else
                {
                    g_con.firstVisible += (y < thumb.top) ? -g_con.rows : g_con.rows;
                    const int maxFirst = total - g_con.rows;
                    g_con.firstVisible = ClampInt(g_con.firstVisible, 0, maxFirst);
                    g_con.followTail = g_con.firstVisible >= maxFirst;
                    RedrawConsole();
                }
                SetCapture(hwnd);
                return 0;
            }

            // 输入行：点哪儿光标就到哪儿
            if (y >= g_con.padY + g_con.rows * g_con.lineH)
            {
                if (!g_con.findMode)
                {
                    const int promptLen = static_cast<int>(g_con.cwd.size()) + 1;
                    int idx = (x - g_con.padX) / (g_con.cellW ? g_con.cellW : 8) - promptLen;
                    idx = ClampInt(idx, 0, static_cast<int>(g_con.input.size()));
                    g_con.caret = idx;
                    RedrawConsole();
                }
                return 0;
            }

            int line = -1, col = 0;
            HitTest(x, y, line, col);
            if (line < 0)
                return 0;
            g_con.selAnchorLine = line;
            g_con.selAnchorCol = col;
            g_con.selLine = line;
            g_con.selCol = col;
            g_con.dragging = true;
            SetCapture(hwnd);
            RedrawConsole();
            return 0;
        }
        case WM_LBUTTONDBLCLK:
        {
            int line = -1, col = 0;
            HitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), line, col);
            if (line >= 0)
            {
                g_con.selAnchorLine = line;
                g_con.selAnchorCol = 0;
                g_con.selLine = line;
                g_con.selCol = static_cast<int>(g_con.lines[static_cast<size_t>(line)].cells.size());
                RedrawConsole();
            }
            return 0;
        }
        case WM_MOUSEMOVE:
        {
            const int y = GET_Y_LPARAM(lParam);
            if (g_con.barDragging)
            {
                RECT track, thumb, rcClient;
                GetClientRect(hwnd, &rcClient);
                GetScrollBarRects(track, thumb, rcClient.right);
                const int total = static_cast<int>(g_con.layout.size());
                const int maxFirst = total > g_con.rows ? total - g_con.rows : 0;
                const int span = (track.bottom - track.top) - (thumb.bottom - thumb.top);
                if (span > 0 && maxFirst > 0)
                {
                    const int pos = ClampInt(y - g_con.barGrabOffset - track.top, 0, span);
                    g_con.firstVisible = (pos * maxFirst) / span;
                    g_con.followTail = g_con.firstVisible >= maxFirst;
                    RedrawConsole();
                }
                return 0;
            }
            if (g_con.dragging)
            {
                int line = -1, col = 0;
                HitTest(GET_X_LPARAM(lParam), y, line, col);
                if (line >= 0)
                {
                    g_con.selLine = line;
                    g_con.selCol = col;
                }
                RedrawConsole();
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (g_con.dragging || g_con.barDragging)
            {
                g_con.dragging = false;
                g_con.barDragging = false;
                ReleaseCapture();
            }
            return 0;
        case WM_MOUSEWHEEL:
        {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (GetKeyState(VK_CONTROL) & 0x8000)
            {
                SetFontSize(g_con.fontPx + (delta > 0 ? 1 : -1));
                return 0;
            }
            const int step = delta > 0 ? -3 : 3;
            g_con.firstVisible += step;
            if (g_con.firstVisible < 0)
                g_con.firstVisible = 0;
            if (g_con.layoutDirty)
                BuildLayout();
            const int total = static_cast<int>(g_con.layout.size());
            const int maxFirst = total > g_con.rows ? total - g_con.rows : 0;
            if (g_con.firstVisible > maxFirst)
                g_con.firstVisible = maxFirst;
            g_con.followTail = g_con.firstVisible >= maxFirst;
            RedrawConsole();
            return 0;
        }
        case WM_RBUTTONUP:
            SetFocus(hwnd);
            ShowContextMenu(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_KEYDOWN:
        {
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

            if (g_con.findMode)
            {
                switch (wParam)
                {
                case VK_ESCAPE:
                    g_con.findMode = false;
                    g_con.findDirty = true;
                    RedrawConsole();
                    return 0;
                case VK_RETURN:
                case VK_F3:
                    FindStep(shift ? -1 : 1);
                    return 0;
                case VK_BACK:
                    if (!g_con.findQuery.empty())
                    {
                        g_con.findQuery.pop_back();
                        g_con.findIndex = -1;
                        g_con.findDirty = true;
                        RedrawConsole();
                    }
                    return 0;
                case VK_DELETE:
                    g_con.findQuery.clear();
                    g_con.findIndex = -1;
                    g_con.findDirty = true;
                    RedrawConsole();
                    return 0;
                default:
                    return 0;
                }
            }

            if (ctrl)
            {
                switch (wParam)
                {
                case 'C':
                case 'c':
                    if (g_con.selAnchorLine >= 0)
                        CopyOutput(false);
                    else if (g_con.running)
                        StopRunning();
                    return 0;
                case 'V':
                case 'v':
                    PasteToInput();
                    return 0;
                case 'F':
                case 'f':
                    g_con.findMode = true;
                    g_con.findIndex = -1;
                    g_con.findDirty = true;
                    RedrawConsole();
                    return 0;
                case 'A':
                case 'a':
                    if (!g_con.lines.empty())
                    {
                        g_con.selAnchorLine = 0;
                        g_con.selAnchorCol = 0;
                        g_con.selLine = static_cast<int>(g_con.lines.size()) - 1;
                        g_con.selCol = static_cast<int>(g_con.lines.back().cells.size());
                        RedrawConsole();
                    }
                    return 0;
                case 'L':
                case 'l':
                    ClearOutput();
                    return 0;
                case 'U':
                case 'u':
                    g_con.input.clear();
                    g_con.caret = 0;
                    RedrawConsole();
                    return 0;
                case 'W':
                case 'w':
                {
                    while (g_con.caret > 0 && g_con.input[static_cast<size_t>(g_con.caret - 1)] == L' ')
                    {
                        g_con.input.erase(g_con.input.begin() + (g_con.caret - 1));
                        --g_con.caret;
                    }
                    while (g_con.caret > 0 && g_con.input[static_cast<size_t>(g_con.caret - 1)] != L' ')
                    {
                        g_con.input.erase(g_con.input.begin() + (g_con.caret - 1));
                        --g_con.caret;
                    }
                    RedrawConsole();
                    return 0;
                }
                default:
                    break;
                }
                return 0;
            }

            switch (wParam)
            {
            case VK_RETURN:
                ExecuteInput();
                RedrawConsole();
                return 0;
            case VK_BACK:
                if (g_con.caret > 0)
                {
                    g_con.input.erase(g_con.input.begin() + (g_con.caret - 1));
                    --g_con.caret;
                }
                RedrawConsole();
                return 0;
            case VK_DELETE:
                if (g_con.caret < static_cast<int>(g_con.input.size()))
                    g_con.input.erase(g_con.input.begin() + g_con.caret);
                RedrawConsole();
                return 0;
            case VK_LEFT:
                if (g_con.caret > 0)
                    --g_con.caret;
                RedrawConsole();
                return 0;
            case VK_RIGHT:
                if (g_con.caret < static_cast<int>(g_con.input.size()))
                    ++g_con.caret;
                RedrawConsole();
                return 0;
            case VK_HOME:
                g_con.caret = 0;
                RedrawConsole();
                return 0;
            case VK_END:
                g_con.caret = static_cast<int>(g_con.input.size());
                RedrawConsole();
                return 0;
            case VK_TAB:
                if (!g_con.running)
                    CompleteInput();
                return 0;
            case VK_F3:
                if (!g_con.findQuery.empty())
                    FindStep(shift ? -1 : 1);
                return 0;
            case VK_PRIOR:
                g_con.firstVisible -= g_con.rows;
                if (g_con.firstVisible < 0)
                    g_con.firstVisible = 0;
                g_con.followTail = false;
                RedrawConsole();
                return 0;
            case VK_NEXT:
                g_con.firstVisible += g_con.rows;
                if (g_con.layoutDirty)
                    BuildLayout();
                {
                    const int total = static_cast<int>(g_con.layout.size());
                    const int maxFirst = total > g_con.rows ? total - g_con.rows : 0;
                    if (g_con.firstVisible > maxFirst)
                        g_con.firstVisible = maxFirst;
                    g_con.followTail = g_con.firstVisible >= maxFirst;
                }
                RedrawConsole();
                return 0;
            case VK_UP:
                if (!g_con.history.empty())
                {
                    if (g_con.historyIndex < 0)
                        g_con.historyIndex = static_cast<int>(g_con.history.size()) - 1;
                    else if (g_con.historyIndex > 0)
                        --g_con.historyIndex;
                    g_con.input = g_con.history[static_cast<size_t>(g_con.historyIndex)];
                    g_con.caret = static_cast<int>(g_con.input.size());
                    RedrawConsole();
                }
                return 0;
            case VK_DOWN:
                if (g_con.historyIndex >= 0)
                {
                    if (g_con.historyIndex + 1 < static_cast<int>(g_con.history.size()))
                    {
                        ++g_con.historyIndex;
                        g_con.input = g_con.history[static_cast<size_t>(g_con.historyIndex)];
                    }
                    else
                    {
                        g_con.historyIndex = -1;
                        g_con.input.clear();
                    }
                    g_con.caret = static_cast<int>(g_con.input.size());
                    RedrawConsole();
                }
                return 0;
            case VK_ESCAPE:
                ConsoleClose();
                return 0;
            case VK_INSERT:
                if (shift)
                {
                    PasteToInput();
                    return 0;
                }
                break;
            default:
                break;
            }
            return 0;
        }
        case WM_CHAR:
        {
            const wchar_t c = static_cast<wchar_t>(wParam);
            if (c < 32)
                return 0;
            if (g_con.findMode)
            {
                if (g_con.findQuery.size() < 200)
                {
                    g_con.findQuery += c;
                    g_con.findIndex = -1;
                    g_con.findDirty = true;
                    RedrawConsole();
                }
                return 0;
            }
            if (g_con.input.size() >= 2000)
                return 0;
            g_con.input.insert(g_con.input.begin() + g_con.caret, c);
            ++g_con.caret;
            RedrawConsole();
            return 0;
        }
        case WM_APP_CONSOLE_APPEND:
        {
            std::wstring *payload = reinterpret_cast<std::wstring *>(wParam);
            if (payload)
            {
                SplitAppend(*payload);
                delete payload;
            }
            return 0;
        }
        case WM_APP_CONSOLE_DONE:
        {
            g_con.running = false;
            CloseStdinWrite();
            if (g_con.job)
            {
                CloseHandle(g_con.job);
                g_con.job = nullptr;
            }
            const DWORD code = static_cast<DWORD>(wParam);
            if (code != 0)
                SplitAppend(Tr(L"（进程结束，退出代码 ", L"(process exited with code ") +
                            std::to_wstring(code) + Tr(L"）\r\n", L")\r\n"));
            RedrawConsole();
            return 0;
        }
        case WM_PAINT:
            PaintConsole(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (g_con.job)
                TerminateJobObject(g_con.job, 1);
            if (g_con.job)
            {
                CloseHandle(g_con.job);
                g_con.job = nullptr;
            }
            g_con.running = false;
            CloseStdinWrite();
            g_con.hwnd = nullptr;
            g_con.dragging = false;
            g_con.barDragging = false;
            if (g_con.font)
            {
                DeleteObject(g_con.font);
                g_con.font = nullptr;
            }
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void ConsoleInstall(HWND hwndMain)
{
    g_hwndMain = g_hwndMain ? g_hwndMain : hwndMain;
    LoadConsoleSettings();
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ConsoleProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    wc.lpszClassName = CONSOLE_CLASS;
    RegisterClassExW(&wc);
}

bool ConsoleIsOpen()
{
    return g_con.hwnd != nullptr && IsWindow(g_con.hwnd);
}

void ConsoleSetDefaultSize(int rows, int cols)
{
    g_defaultRows = ClampInt(rows, 3, 60);
    g_defaultCols = ClampInt(cols, 20, 300);
}

int ConsoleDefaultRows()
{
    return g_defaultRows;
}

int ConsoleDefaultCols()
{
    return g_defaultCols;
}

void ConsoleShow(HWND hwndMain)
{
    if (!ConsoleIsOpen())
    {
        // 启动目录：当前显示文件的目录；没有文件时用程序所在目录
        if (!g_state.filePath.empty())
            g_con.cwd = DirOfFile(g_state.filePath);
        if (g_con.cwd.empty())
            g_con.cwd = NormalizeDir(StorageExeDir());

        g_con.rows = ClampInt(g_defaultRows, 3, 60);
        g_con.cols = ClampInt(g_defaultCols, 20, 300);

        RECT rcMain = {0, 0, 0, 0};
        GetWindowRect(hwndMain, &rcMain);
        const int w = g_con.cols * g_con.cellW + g_con.padX * 2 + kBarGap;
        const int h = g_con.rows * g_con.lineH + g_con.inputH + g_con.padY * 2;
        RECT rc = {0, 0, w, h};
        AdjustWindowRectEx(&rc, WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX, FALSE, WS_EX_TOOLWINDOW);
        const int winW = rc.right - rc.left;
        const int winH = rc.bottom - rc.top;
        int x = rcMain.right - winW - 24;
        int y = rcMain.top + 60;
        if (x < 0)
            x = 40;
        if (y < 0)
            y = 40;

        const auto &lang = GetLangStrings();
        std::wstring title = lang.appName + L" - " + Tr(L"命令行", L"Console");
        g_con.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, CONSOLE_CLASS, title.c_str(),
                                     WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX,
                                     x, y, winW, winH,
                                     hwndMain, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!g_con.hwnd)
            return;
        EnsureSize();
        SplitAppend(Tr(L"命令行已就绪，工作目录：", L"Console ready. Working directory: ") + g_con.cwd +
                    Tr(L"\r\n输入 help 查看内置命令。\r\n", L"\r\nType help for the built-in commands.\r\n"));
    }
    else
    {
        EnsureSize();
    }
    ShowWindow(g_con.hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(g_con.hwnd);
    SetFocus(g_con.hwnd);
}

void ConsoleClose()
{
    if (ConsoleIsOpen())
        DestroyWindow(g_con.hwnd);
}

// 菜单「控制台 → 清空输出」：只清输出区，不动输入行、历史与正在运行的进程。
void ConsoleClear()
{
    if (!ConsoleIsOpen())
        return;
    g_con.pending.clear();
    g_con.curColor = 0;
    ClearOutput();
}

// 菜单「控制台 → 设置…」：行数 / 列数 / 字号 / 是否彩色，改完立即生效并写入注册表。
void ConsoleShowSettings(HWND owner)
{
    ShowSettingsDialog(g_con.hwnd ? g_con.hwnd : owner);
}

void ConsoleRunBuiltExe(const std::wstring &exePath)
{
    ConsoleShow(g_hwndMain);
    if (!ConsoleIsOpen())
        return;
    std::wstring dir = DirOfFile(exePath);
    if (!dir.empty())
        g_con.cwd = dir;
    const std::wstring quoted = L"\"" + exePath + L"\"";
    SplitAppend(g_con.cwd + L">" + quoted + L"\r\n");
    g_con.running = true;
    RunCommandAsync(quoted, g_con.cwd);
    RedrawConsole();
}
