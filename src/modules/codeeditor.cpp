
#include "codeeditor.h"
#include "core/types.h"
#include "core/globals.h"
#include "theme.h"
#include "background.h"
#include "syntax.h"
#include "presets.h"
#include "symbols.h"
#include "resource.h"
#include <richedit.h>
#include <windowsx.h>
#include <imm.h>
#include <cwctype>
#include <wchar.h>
#include <string.h>
#include <algorithm>
#include <set>
#include <vector>

/* ------------------------------------------------------------------ */
/*  Internal state                                                     */
/* ------------------------------------------------------------------ */

struct UndoAction
{
    size_t start;
    size_t end;
    std::wstring removed;
    std::wstring inserted;
};

// A completion candidate: the text committed on accept plus the type prefix
// shown in the popup (F=library function, K=keyword, uF=user function,
// uV=user variable, uS=user struct, uC=user class, uE=enum, uT=alias,
// uD=macro, uM=module, C=builtin type, L=literal).
struct CompItem
{
    std::wstring name;
    std::wstring tag;
};

struct EditorState
{
    std::wstring text;
    size_t caret = 0;
    size_t anchor = 0;
    bool hasSel = false;

    int topRow = 0;
    int leftChar = 0; // horizontal scroll offset, in display columns
    bool wrap = false;

    HFONT font = nullptr;
    int charW = 8;
    int lineH = 16;
    int ascent = 12;
    int gutterW = 30;
    int lastLineCount = 1;

    bool focus = false;
    bool caretVisible = true;
    bool suppressSpace = false;

    std::vector<UndoAction> undo;
    std::vector<UndoAction> redo;

    const LanguageDef *lang = nullptr;
    std::vector<unsigned char> lineState;

    // Cached line-start offsets. Recomputing them by scanning from offset 0
    // on every LineStart() call makes whole-document passes quadratic, which
    // froze the UI for several megabytes of text.
    std::vector<size_t> lineStarts;
    bool lineIdxDirty = true;

    // Soft-wrap layout table.  Word wrap breaks at spaces, so a wrapped row
    // no longer starts at a multiple of the row capacity — the row origins
    // have to be recorded explicitly and shared by painting and hit testing.
    std::vector<int> wrapRows;     // visual rows per logical line
    std::vector<int> wrapPrefix;   // first visual row of each line (+1 sentinel)
    int wrapFC = 0;                // row capacity these tables were built for
    bool wrapDirty = true;

    bool dragging = false;

    // code completion
    HWND popup = nullptr;
    std::vector<CompItem> candidates;
    bool candidatesDirty = true;
    size_t lastBuildLen = 0;
    std::vector<CompItem> matches;
    int compSel = 0;
    int compTop = 0;   // first visible row index in the scrollable 3-row popup
    size_t compWordStart = 0;
    bool compActive = false;

    // whole-document symbols + diagnostics
    std::vector<UserSym> userSyms;
    std::vector<int> diagLines;      // 0-based lines to paint red
    std::vector<std::wstring> diagMsgs;
    std::vector<std::wstring> lastPoppedDiags;
    bool diagPopupArmed = false;

    // current file path, used to resolve local #include "..." headers
    std::wstring filePath;

    // Off-screen back buffer: the whole client area is composed into this
    // bitmap and blitted once, so scrolling never shows a half-painted frame.
    HDC backDC = nullptr;
    HBITMAP backBmp = nullptr;
    HBITMAP backOld = nullptr;
    int backW = 0;
    int backH = 0;
};

static const wchar_t *kCompletionClass = L"LegacyCodeCompletion";

static EditorState *GetState(HWND hwnd)
{
    return reinterpret_cast<EditorState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

/* ------------------------------------------------------------------ */
/*  Color helpers                                                      */
/* ------------------------------------------------------------------ */

static COLORREF ColBg()        { return IsDarkMode() ? RGB(30, 30, 30) : GetSysColor(COLOR_WINDOW); }
static COLORREF ColGutter()    { return IsDarkMode() ? RGB(37, 37, 38) : RGB(242, 242, 242); }
static COLORREF ColGutterText(){ return IsDarkMode() ? RGB(133, 133, 133) : RGB(110, 110, 110); }
static COLORREF ColGutterCur() { return IsDarkMode() ? RGB(255, 255, 255) : RGB(20, 20, 20); }
static COLORREF ColSelBg()     { return IsDarkMode() ? RGB(38, 79, 120) : RGB(173, 214, 255); }
static COLORREF ColCaret()     { return IsDarkMode() ? RGB(255, 255, 255) : RGB(0, 0, 0); }
static COLORREF ColErrBg()     { return IsDarkMode() ? RGB(96, 42, 42) : RGB(255, 224, 224); }

static COLORREF ColTok(TokType t)
{
    if (IsDarkMode())
    {
        switch (t)
        {
        case TokType::Keyword:      return RGB(86, 156, 214);
        case TokType::Type:         return RGB(78, 201, 176);
        case TokType::Literal:      return RGB(220, 220, 170);
        case TokType::String:       return RGB(206, 145, 120);
        case TokType::Comment:      return RGB(106, 153, 85);
        case TokType::Number:       return RGB(181, 206, 168);
        case TokType::Preprocessor: return RGB(197, 134, 192);
        default:                    return RGB(212, 212, 212);
        }
    }
    switch (t)
    {
    case TokType::Keyword:      return RGB(0, 0, 255);
    case TokType::Type:         return RGB(0, 119, 170);
    case TokType::Literal:      return RGB(121, 94, 38);
    case TokType::String:       return RGB(163, 21, 21);
    case TokType::Comment:      return RGB(0, 128, 0);
    case TokType::Number:       return RGB(9, 134, 88);
    case TokType::Preprocessor: return RGB(128, 0, 0);
    default:                    return RGB(0, 0, 0);
    }
}

/* ------------------------------------------------------------------ */
/*  Text model helpers (text is normalized to '\n' line endings)       */
/* ------------------------------------------------------------------ */

static void BuildLineIndex(EditorState *st)
{
    st->lineStarts.clear();
    st->lineStarts.push_back(0);
    const std::wstring &t = st->text;
    for (size_t i = 0; i < t.size(); ++i)
        if (t[i] == L'\n')
            st->lineStarts.push_back(i + 1);
    st->lineIdxDirty = false;
    st->wrapDirty = true;
}

static void InvalidateLineIndex(EditorState *st)
{
    st->lineIdxDirty = true;
}

static int LineCount(const EditorState *st)
{
    EditorState *s = const_cast<EditorState *>(st);
    if (s->lineIdxDirty)
        BuildLineIndex(s);
    return static_cast<int>(s->lineStarts.size());
}

static size_t LineStart(const EditorState *st, int line)
{
    EditorState *s = const_cast<EditorState *>(st);
    if (s->lineIdxDirty)
        BuildLineIndex(s);
    if (line <= 0)
        return 0;
    if (static_cast<size_t>(line) >= s->lineStarts.size())
        return s->text.size();
    return s->lineStarts[line];
}

static size_t LineContentLen(const EditorState *st, int line)
{
    size_t s = LineStart(st, line);
    size_t e = st->text.find(L'\n', s);
    if (e == std::wstring::npos)
        e = st->text.size();
    size_t len = e - s;
    if (len > 0 && st->text[e - 1] == L'\r')
        --len;
    return len;
}

static std::wstring LineContent(const EditorState *st, int line)
{
    return st->text.substr(LineStart(st, line), LineContentLen(st, line));
}

static int LineOfOffset(const EditorState *st, size_t pos)
{
    EditorState *s = const_cast<EditorState *>(st);
    if (s->lineIdxDirty)
        BuildLineIndex(s);
    if (pos > st->text.size())
        pos = st->text.size();
    auto it = std::upper_bound(s->lineStarts.begin(), s->lineStarts.end(), pos);
    int line = static_cast<int>(it - s->lineStarts.begin()) - 1;
    return line < 0 ? 0 : line;
}

static size_t LineCol(const EditorState *st, size_t pos)
{
    return pos - LineStart(st, LineOfOffset(st, pos));
}

/* ------------------------------------------------------------------ */
/*  Wide-character (CJK) display columns                               */
/*  A CJK / full-width char occupies 2 display columns; a surrogate    */
/*  pair is one char occupying 2 columns.                              */
/* ------------------------------------------------------------------ */

static bool IsSurHigh(wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; }
static bool IsSurLow(wchar_t c)  { return c >= 0xDC00 && c <= 0xDFFF; }

/*  零宽字符：组合符(Mn/Me)、双向控制符、变体选择符、零宽连接符。      */
/*  阿拉伯文/印地文/希伯来文的附加符号都属于这一类，占 0 列，光标和    */
/*  选区才不会因为“多算一列”而整体偏移。                              */
static bool IsZeroWidth(wchar_t c)
{
    if (c == 0x200B || c == 0x200C || c == 0x200D || c == 0x200E || c == 0x200F ||
        c == 0x061C || c == 0xFEFF ||
        (c >= 0x202A && c <= 0x202E) || (c >= 0x2060 && c <= 0x2069) ||
        (c >= 0xFE00 && c <= 0xFE0F) || (c >= 0xFFF9 && c <= 0xFFFB))
        return true;
    return (c >= 0x0300 && c <= 0x036F) || (c >= 0x0483 && c <= 0x0489) ||
           (c >= 0x0591 && c <= 0x05BD) || c == 0x05BF ||
           (c >= 0x05C1 && c <= 0x05C2) || (c >= 0x05C4 && c <= 0x05C5) || c == 0x05C7 ||
           (c >= 0x0610 && c <= 0x061A) || (c >= 0x064B && c <= 0x065F) || c == 0x0670 ||
           (c >= 0x06D6 && c <= 0x06DC) || (c >= 0x06DF && c <= 0x06E4) ||
           (c >= 0x06E7 && c <= 0x06E8) || (c >= 0x06EA && c <= 0x06ED) ||
           c == 0x0711 || (c >= 0x0730 && c <= 0x074A) || (c >= 0x07A6 && c <= 0x07B0) ||
           (c >= 0x0816 && c <= 0x082D) || (c >= 0x0859 && c <= 0x085B) ||
           (c >= 0x08D3 && c <= 0x0903) || (c >= 0x093A && c <= 0x093C) ||
           (c >= 0x0941 && c <= 0x0948) || c == 0x094D ||
           (c >= 0x0951 && c <= 0x0957) || (c >= 0x0962 && c <= 0x0963) ||
           (c >= 0x0E31 && c <= 0x0E3A) || (c >= 0x0E47 && c <= 0x0E4E) ||
           (c >= 0x0F71 && c <= 0x0F84) || (c >= 0x0F86 && c <= 0x0F87) ||
           (c >= 0x1AB0 && c <= 0x1AFF) || (c >= 0x1DC0 && c <= 0x1DFF) ||
           (c >= 0x20D0 && c <= 0x20F0) || (c >= 0xFE20 && c <= 0xFE2F);
}

/*  单词字符：字母/数字（含中文、阿拉伯文、西里尔文等），以及粘在      */
/*  基字符上的组合符和代理对。其余（空格、标点、符号）都算分隔符。      */
// 中日韩文字（汉字、假名、韩文、全角形式）习惯上“一字即一词”，
// 折行时可以在任意两个字之间断开，不需要把整段当作一个词挪到下一行。
static bool IsCjkBreakable(wchar_t c)
{
    return (c >= 0x1100 && c <= 0x11FF) || // 韩文字母
           (c >= 0x2E80 && c <= 0x9FFF) || // CJK 部首/符号/假名/汉字
           (c >= 0xAC00 && c <= 0xD7A3) || // 韩文音节
           (c >= 0xF900 && c <= 0xFAFF) || // CJK 兼容汉字
           (c >= 0xFE30 && c <= 0xFE4F) || // CJK 兼容形式
           (c >= 0xFF00 && c <= 0xFF60) || // 全角形式
           (c >= 0xFFE0 && c <= 0xFFE6);
}

static bool IsWordCodeUnit(wchar_t c)
{
    if (c >= 0xD800 && c <= 0xDFFF)
        return true; // 代理对：emoji 等补充平面字符
    if (c < 0x80)
    {
        bool alpha = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                     (c >= L'0' && c <= L'9');
        if (alpha)
            return true;
    }
    if (IsZeroWidth(c))
        return true; // 组合符跟着基字符走，不当作分隔点
    static signed char cache[0x10000] = {0}; // 0 未知，1 字母/数字，-1 其它
    unsigned short u = static_cast<unsigned short>(c);
    if (cache[u] == 0)
    {
        WORD t = 0;
        GetStringTypeW(CT_CTYPE1, &c, 1, &t);
        cache[u] = ((t & (C1_ALPHA | C1_DIGIT)) != 0) ? 1 : -1;
    }
    return cache[u] == 1;
}

// 前进一个字素簇（代理对 + 跟随的组合符整体算一个）
static size_t NextGrapheme(const std::wstring &s, size_t i, size_t len)
{
    if (i >= len)
        return len;
    size_t p = i + 1;
    if (IsSurHigh(s[i]) && p < len && IsSurLow(s[p]))
        ++p;
    while (p < len && IsZeroWidth(s[p]))
        ++p;
    return p;
}

static int CharCols(wchar_t c, wchar_t next)
{
    if (c >= 0x1100 && (c <= 0x115F || (c >= 0x2E80 && c <= 0xA4CF) ||
                        (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0xF900 && c <= 0xFAFF) ||
                        (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF60) ||
                        (c >= 0xFFE0 && c <= 0xFFE6)))
        return 2;
    if (IsSurHigh(c) && IsSurLow(next))
        return 2; // astral plane char (surrogate pair) = one 2-col char
    if (IsZeroWidth(c))
        return 0; // 组合符 / 双向控制符不占列
    return 1;
}

// Display columns occupied by s[0..n).
static int DispWidth(const std::wstring &s, size_t n)
{
    int w = 0;
    for (size_t i = 0; i < n; ++i)
    {
        wchar_t c = s[i];
        wchar_t nx = (i + 1 < s.size()) ? s[i + 1] : 0;
        int cols = CharCols(c, nx);
        w += cols;
        if (cols == 2 && IsSurHigh(c) && IsSurLow(nx))
            ++i; // consume the low surrogate as part of this char
    }
    return w;
}

// Index of the first char whose display column is >= dc.
static size_t IndexForDisp(const std::wstring &s, int dc)
{
    if (dc <= 0)
        return 0;
    int w = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        wchar_t c = s[i];
        wchar_t nx = (i + 1 < s.size()) ? s[i + 1] : 0;
        int cols = CharCols(c, nx);
        if (w + cols > dc)
            return i;
        w += cols;
        if (cols == 2 && IsSurHigh(c) && IsSurLow(nx))
            ++i;
    }
    return s.size();
}

static int LineDispLen(const EditorState *st, int line)
{
    std::wstring content = LineContent(st, line);
    return DispWidth(content, content.size());
}

// Display column of a caret offset.
static int DispColAtOffset(const EditorState *st, size_t pos)
{
    int line = LineOfOffset(st, pos);
    size_t col = LineCol(st, pos);
    std::wstring content = LineContent(st, line);
    return DispWidth(content, (std::min)(col, content.size()));
}

static int MaxLineLen(const EditorState *st)
{
    int n = LineCount(st);
    int mx = 0;
    for (int i = 0; i < n; ++i)
        mx = (std::max)(mx, LineDispLen(st, i));
    return mx;
}

/* ------------------------------------------------------------------ */
/*  Layout helpers                                                     */
/* ------------------------------------------------------------------ */

static int ClientWidth(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    return rc.right - rc.left;
}

static int ClientHeight(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    return rc.bottom - rc.top;
}

static int TextWidth(HWND hwnd, const EditorState *st)
{
    return (std::max)(1, ClientWidth(hwnd) - st->gutterW);
}

static int FitChars(HWND hwnd, const EditorState *st)
{
    if (!st->wrap)
        return INT_MAX;
    int fc = TextWidth(hwnd, st) / (std::max)(1, st->charW);
    return (std::max)(1, fc);
}

static int VisibleRows(HWND hwnd, const EditorState *st)
{
    return (std::max)(1, ClientHeight(hwnd) / (std::max)(1, st->lineH));
}

/*  折行：一行放不下就折到下一视觉行，断行优先落在“词”的边界上。
    · 空格、制表符、标点、符号都算分隔符（不论什么语言）。
    · 一个连续的字母/数字串（含中文、阿拉伯文等）算一个词；这个词在
      行尾放不下就整体挪到下一行，分隔符留在上一行行尾。
    · 中日韩文字（汉字、假名、韩文、全角形式）按“一字即一词”处理：
      行尾放不下时直接在当前字前断开，不再整段挪行。
    · 连续串超过 15 个字符就不算“词”了，允许在其中硬断，避免超长
      URL / 压缩后的代码 / 无空格长串把光标逼到屏幕外。               */
static const int kMaxWordChars = 15;

static int WrapNextRow(const std::wstring &s, size_t start, size_t len, int fc)
{
    if (start >= len)
        return static_cast<int>(len);
    int col = 0;
    size_t i = start;
    while (i < len)
    {
        size_t g = NextGrapheme(s, i, len);
        int cols = 0;
        for (size_t k = i; k < g; ++k)
            cols += CharCols(s[k], (k + 1 < s.size()) ? s[k + 1] : 0);
        if (col + cols > fc)
            break;
        col += cols;
        i = g;
    }
    if (i >= len)
        return static_cast<int>(len);
    if (i <= start)
        return static_cast<int>(NextGrapheme(s, start, len)); // 一行放不下一个字符
    if (IsCjkBreakable(s[i]))
        return static_cast<int>(NextGrapheme(s, i, len)); // 中日韩一字一词：直接在这个字前断开
    if (!IsWordCodeUnit(s[i]))
        return static_cast<int>(NextGrapheme(s, i, len)); // 分隔符留在上一行行尾，下一行不以空格/标点开头
    // 断点落在一个词中间：把整个词挪到下一行（词太长就按原断点硬断）
    size_t ts = i;
    while (ts > start && IsWordCodeUnit(s[ts - 1]) && !IsCjkBreakable(s[ts - 1]))
        --ts;
    size_t te = ts;
    int wordLen = 0;
    while (te < len && IsWordCodeUnit(s[te]) && !IsCjkBreakable(s[te]))
    {
        te = NextGrapheme(s, te, len);
        ++wordLen;
    }
    if (ts > start && wordLen <= kMaxWordChars)
        return static_cast<int>(ts);
    return static_cast<int>(i);
}

// 每行的视觉行起始字符索引，末尾哨兵为字符串长度。
static void WrapRowStarts(const std::wstring &s, int fc, std::vector<int> &out)
{
    out.clear();
    out.push_back(0);
    size_t p = 0;
    while (p < s.size())
    {
        p = static_cast<size_t>(WrapNextRow(s, p, s.size(), fc));
        out.push_back(static_cast<int>(p));
    }
    if (out.size() < 2)
        out.push_back(static_cast<int>(s.size()));
}

static int WrapRowCount(const std::wstring &s, int fc)
{
    int rows = 0;
    size_t p = 0;
    do
    {
        p = static_cast<size_t>(WrapNextRow(s, p, s.size(), fc));
        ++rows;
    } while (p < s.size());
    return (std::max)(1, rows);
}

// 重建折行表：文本变化(lineIdxDirty) 或 行容量变化(fc) 时重算。
static void EnsureWrap(HWND hwnd, EditorState *st)
{
    if (!st->wrap)
    {
        st->wrapRows.clear();
        st->wrapPrefix.clear();
        st->wrapFC = 0;
        st->wrapDirty = false;
        return;
    }
    int fc = FitChars(hwnd, st);
    int n = LineCount(st);
    if (!st->wrapDirty && st->wrapFC == fc &&
        static_cast<int>(st->wrapPrefix.size()) == n + 1)
        return;
    st->wrapRows.assign((std::max)(1, n), 1);
    st->wrapPrefix.assign(n + 1, 0);
    int acc = 0;
    for (int i = 0; i < n; ++i)
    {
        std::wstring content = LineContent(st, i);
        int rows = WrapRowCount(content, fc);
        st->wrapRows[i] = rows;
        st->wrapPrefix[i] = acc;
        acc += rows;
    }
    st->wrapPrefix[n] = acc;
    st->wrapFC = fc;
    st->wrapDirty = false;
}

static int LineRows(HWND hwnd, const EditorState *st, int line)
{
    if (!st->wrap)
        return 1;
    EditorState *s = const_cast<EditorState *>(st);
    EnsureWrap(hwnd, s);
    if (line >= 0 && line < static_cast<int>(s->wrapRows.size()))
        return s->wrapRows[line];
    return 1;
}

static int TotalRows(HWND hwnd, const EditorState *st)
{
    if (!st->wrap)
        return LineCount(st);
    EditorState *s = const_cast<EditorState *>(st);
    EnsureWrap(hwnd, s);
    if (s->wrapPrefix.empty())
        return LineCount(st);
    return s->wrapPrefix.back();
}

// 视觉行 -> 逻辑行
static int LineOfRow(HWND hwnd, const EditorState *st, int visRow)
{
    if (!st->wrap)
        return visRow;
    EditorState *s = const_cast<EditorState *>(st);
    EnsureWrap(hwnd, s);
    int n = LineCount(st);
    if (s->wrapPrefix.size() < static_cast<size_t>(n + 1) || n == 0)
        return 0;
    int lo = 0, hi = n - 1, line = 0;
    while (lo <= hi)
    {
        int mid = (lo + hi) / 2;
        if (s->wrapPrefix[mid] <= visRow)
        {
            line = mid;
            lo = mid + 1;
        }
        else
        {
            hi = mid - 1;
        }
    }
    return line;
}

// Visual row of a (logical line, display column) pair.
static int RowOfLineCol(HWND hwnd, const EditorState *st, int line, int dispCol)
{
    if (!st->wrap)
        return line;
    EditorState *s = const_cast<EditorState *>(st);
    EnsureWrap(hwnd, s);
    if (line < 0)
        line = 0;
    if (line >= static_cast<int>(s->wrapRows.size()))
        return s->wrapPrefix.empty() ? line : s->wrapPrefix.back() - 1;
    int base = s->wrapPrefix[line];
    std::wstring content = LineContent(st, line);
    std::vector<int> starts;
    WrapRowStarts(content, FitChars(hwnd, st), starts);
    int within = 0;
    for (int k = 1; k + 1 < static_cast<int>(starts.size()); ++k)
    {
        if (DispWidth(content, starts[k]) <= dispCol)
            within = k;
        else
            break;
    }
    return base + within;
}

static int RowOfOffset(HWND hwnd, const EditorState *st, size_t pos)
{
    return RowOfLineCol(hwnd, st, LineOfOffset(st, pos), DispColAtOffset(st, pos));
}

// Returns (logical line, base display column of that visual row).
static std::pair<int, int> LineColOfRow(HWND hwnd, const EditorState *st, int visRow)
{
    if (!st->wrap)
        return {visRow, 0};
    int line = LineOfRow(hwnd, st, (std::max)(0, visRow));
    int n = LineCount(st);
    if (line >= n)
        line = (std::max)(0, n - 1);
    EditorState *s = const_cast<EditorState *>(st);
    EnsureWrap(hwnd, s);
    std::wstring content = LineContent(st, line);
    std::vector<int> starts;
    WrapRowStarts(content, FitChars(hwnd, st), starts);
    int within = (std::max)(0, visRow) - s->wrapPrefix[line];
    int last = static_cast<int>(starts.size()) - 2;
    if (within > last)
        within = last;
    if (within < 0)
        within = 0;
    return {line, DispWidth(content, starts[within])};
}

// Hit test: pixel row / display column (relative to the visual row start,
// leftChar excluded) -> absolute char offset.
static size_t OffsetAt(HWND hwnd, const EditorState *st, int visRow, int dispCol)
{
    if (visRow < 0)
        visRow = 0;
    if (!st->wrap)
    {
        int line = visRow;
        if (line >= LineCount(st))
            line = LineCount(st) - 1;
        std::wstring content = LineContent(st, line);
        return LineStart(st, line) + IndexForDisp(content, dispCol + st->leftChar);
    }
    int line = LineOfRow(hwnd, st, visRow);
    int n = LineCount(st);
    if (line >= n)
        line = (std::max)(0, n - 1);
    EditorState *s = const_cast<EditorState *>(st);
    EnsureWrap(hwnd, s);
    std::wstring content = LineContent(st, line);
    std::vector<int> starts;
    WrapRowStarts(content, FitChars(hwnd, st), starts);
    int within = visRow - s->wrapPrefix[line];
    int last = static_cast<int>(starts.size()) - 2;
    if (within > last)
        within = last;
    if (within < 0)
        within = 0;
    int baseDc = DispWidth(content, starts[within]);
    int rowEnd = starts[within + 1];
    size_t idx = IndexForDisp(content, baseDc + dispCol);
    if (idx > static_cast<size_t>(rowEnd))
        idx = static_cast<size_t>(rowEnd);
    return LineStart(st, line) + idx;
}

static void RecomputeGutter(EditorState *st)
{
    int n = LineCount(st);
    st->lastLineCount = n;
    int digits = 1;
    int v = n;
    while (v >= 10)
    {
        v /= 10;
        ++digits;
    }
    // Extra column on the left for the ┌ │ └ bracket of soft-wrapped lines.
    st->gutterW = digits * st->charW + 16 + (st->wrap ? (std::max)(8, st->charW) : 0);
}

static void RecomputeMetrics(HWND hwnd, EditorState *st)
{
    HDC dc = GetDC(hwnd);
    HFONT use = st->font ? st->font : static_cast<HFONT>(GetStockObject(ANSI_FIXED_FONT));
    HFONT old = reinterpret_cast<HFONT>(SelectObject(dc, use));
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    st->lineH = tm.tmHeight + tm.tmExternalLeading;
    st->ascent = tm.tmAscent;
    SIZE sz;
    GetTextExtentPoint32W(dc, L"W", 1, &sz);
    st->charW = (std::max)(1, static_cast<int>(sz.cx));
    SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
    RecomputeGutter(st);
}

static void RecomputeLineStates(EditorState *st, int fromLine)
{
    if (fromLine < 0)
        fromLine = 0;
    int n = LineCount(st);
    if (static_cast<int>(st->lineState.size()) != n + 1)
    {
        st->lineState.assign(n + 1, 0);
        fromLine = 0;
    }
    std::vector<LineToken> tok;
    for (int i = fromLine; i < n; ++i)
    {
        unsigned char in = st->lineState[i];
        unsigned char out;
        std::wstring content = LineContent(st, i);
        TokenizeLine(st->lang, content, in, out, tok);
        st->lineState[i + 1] = out;
    }
    if (LineCount(st) != st->lastLineCount)
        RecomputeGutter(st);
}

static void UpdateScrollbars(HWND hwnd, EditorState *st)
{
    int rows = TotalRows(hwnd, st);
    int vis = VisibleRows(hwnd, st);
    int maxRow = (std::max)(0, rows - vis);
    if (st->topRow > maxRow)
        st->topRow = maxRow;
    if (st->topRow < 0)
        st->topRow = 0;

    SCROLLINFO sv = {};
    sv.cbSize = sizeof(sv);
    sv.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    sv.nMin = 0;
    sv.nMax = (std::max)(0, rows - 1);
    sv.nPage = static_cast<UINT>(vis);
    sv.nPos = st->topRow;
    SetScrollInfo(hwnd, SB_VERT, &sv, TRUE);

    SCROLLINFO sh = {};
    sh.cbSize = sizeof(sh);
    sh.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    sh.nMin = 0;
    if (st->wrap)
    {
        sh.nMax = 0;
        sh.nPage = 1;
        sh.nPos = 0;
    }
    else
    {
        int maxLen = MaxLineLen(st);
        int fc = FitChars(hwnd, st);
        if (st->leftChar > (std::max)(0, maxLen - fc))
            st->leftChar = (std::max)(0, maxLen - fc);
        if (st->leftChar < 0)
            st->leftChar = 0;
        sh.nMax = maxLen;
        sh.nPage = static_cast<UINT>((std::max)(1, fc));
        sh.nPos = st->leftChar;
    }
    SetScrollInfo(hwnd, SB_HORZ, &sh, TRUE);
}

static void EnsureCaretVisible(HWND hwnd, EditorState *st)
{
    int vis = VisibleRows(hwnd, st);
    int row = RowOfOffset(hwnd, st, st->caret);
    int col = DispColAtOffset(st, st->caret);
    if (row < st->topRow)
        st->topRow = row;
    else if (row >= st->topRow + vis)
        st->topRow = row - vis + 1;
    if (!st->wrap)
    {
        int fc = FitChars(hwnd, st);
        if (col < st->leftChar)
            st->leftChar = col;
        else if (col >= st->leftChar + fc)
            st->leftChar = col - fc + 1;
    }
    UpdateScrollbars(hwnd, st);
}

static void InvalidateCaret(HWND hwnd, const EditorState *st)
{
    int vis = VisibleRows(hwnd, st);
    int row = RowOfOffset(hwnd, st, st->caret);
    if (row < st->topRow || row >= st->topRow + vis)
        return;
    int col = DispColAtOffset(st, st->caret);
    int baseDc = 0;
    if (st->wrap)
        baseDc = LineColOfRow(hwnd, st, row).second;
    int x = st->gutterW + (col - st->leftChar - baseDc) * st->charW;
    int y = (row - st->topRow) * st->lineH;
    RECT r = {x, y, x + 3, y + st->lineH};
    InvalidateRect(hwnd, &r, FALSE);
}

/* ------------------------------------------------------------------ */
/*  Notifications to the parent (main window)                          */
/* ------------------------------------------------------------------ */

static void NotifyChange(HWND hwnd)
{
    SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(IDC_EDITOR, EN_CHANGE), reinterpret_cast<LPARAM>(hwnd));
}

static void NotifySelChange(HWND hwnd)
{
    SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(IDC_EDITOR, EN_SELCHANGE), reinterpret_cast<LPARAM>(hwnd));
}

/* ------------------------------------------------------------------ */
/*  Editing primitives                                                 */
/* ------------------------------------------------------------------ */

static bool IsTypingChars(const std::wstring &s)
{
    for (wchar_t c : s)
        if (c < 32 || c == 127)
            return false;
    return true;
}

static void HideCompletion(HWND hwnd, EditorState *st)
{
    (void)hwnd;
    if (st->popup)
    {
        DestroyWindow(st->popup);
        st->popup = nullptr;
    }
    st->compActive = false;
}

// Re-scan the whole document: refresh user symbols (used by completion) and
// call diagnostics (red lines + debounced popup for unknown functions /
// missing includes).
static void RefreshSemantic(HWND hwnd, EditorState *st)
{
    st->diagLines.clear();
    st->diagMsgs.clear();
    st->userSyms.clear();
    const LanguageDef *L = st->lang;
    if (L && L->id)
    {
        std::vector<Diag> diags;
        ScanSemantic(st->text, L->id, st->userSyms, diags, st->filePath);
        // 代码检查被关掉时：符号表照收（补全要用），但不再记问题行 / 提示。
        for (const Diag &d : diags)
        {
            if (!g_state.codeCheck)
                break;
            st->diagLines.push_back(d.line);
            std::wstring msg;
            if (d.kind == DiagKind::MissingInclude)
                msg = L"you need #include <" + d.header + L">  (调用 " + d.name + L")";
            else
                msg = L"unknown function: " + d.name;
            st->diagMsgs.push_back(msg);
        }
    }
    // Arm a debounced popup when a NEW set of errors appears (not on every keystroke).
    if (!st->diagMsgs.empty() && st->diagMsgs != st->lastPoppedDiags && !st->diagPopupArmed)
    {
        st->diagPopupArmed = true;
        SetTimer(hwnd, 2, 1200, nullptr);
    }
    else if (st->diagMsgs.empty())
    {
        st->diagPopupArmed = false;
    }
}

static void DoInsert(HWND hwnd, EditorState *st, size_t start, size_t end, const std::wstring &repl)
{
    if (start > end)
        std::swap(start, end);
    if (start > st->text.size())
        start = st->text.size();
    if (end > st->text.size())
        end = st->text.size();

    if (!st->undo.empty())
    {
        UndoAction &last = st->undo.back();
        // 连续输入的字符合并成一笔撤销。若上一笔是「用首字符替换掉了选区」，
        // 也要继续并进去，这样整段输入只按一次 Ctrl+Z 就回到被替换前的原样。
        if (!last.inserted.empty() && last.start + last.inserted.size() == start &&
            IsTypingChars(repl) && IsTypingChars(last.inserted))
        {
            last.inserted += repl;
            st->redo.clear();
        }
        else
        {
            st->undo.push_back({start, end, st->text.substr(start, end - start), repl});
            st->redo.clear();
        }
    }
    else
    {
        st->undo.push_back({start, end, st->text.substr(start, end - start), repl});
        st->redo.clear();
    }

    InvalidateLineIndex(st);
    st->text.replace(start, end - start, repl);
    st->caret = start + repl.size();
    st->anchor = st->caret;
    st->hasSel = false;

    // Keep the completion candidate set fresh so identifiers typed in this
    // session appear as suggestions immediately (Dev-C++-style live completion).
    st->candidatesDirty = true;

    int changedLine = LineOfOffset(st, start);
    RecomputeLineStates(st, changedLine);
    RefreshSemantic(hwnd, st);
    NotifyChange(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void DeleteSelection(HWND hwnd, EditorState *st)
{
    if (!st->hasSel)
        return;
    size_t a = (std::min)(st->anchor, st->caret);
    size_t b = (std::max)(st->anchor, st->caret);
    DoInsert(hwnd, st, a, b, L"");
}

static size_t WordLeft(const EditorState *st, size_t pos)
{
    while (pos > 0 && !IsIdentifierChar(st->text[pos - 1]))
        --pos;
    while (pos > 0 && IsIdentifierChar(st->text[pos - 1]))
        --pos;
    return pos;
}

static size_t WordRight(const EditorState *st, size_t pos)
{
    size_t n = st->text.size();
    while (pos < n && !IsIdentifierChar(st->text[pos]))
        ++pos;
    while (pos < n && IsIdentifierChar(st->text[pos]))
        ++pos;
    return pos;
}

static void MoveCaretTo(HWND hwnd, EditorState *st, size_t pos, bool extend)
{
    if (pos > st->text.size())
        pos = st->text.size();
    st->caret = pos;
    if (extend)
        st->hasSel = (st->anchor != st->caret);
    else
    {
        st->anchor = pos;
        st->hasSel = false;
    }
    HideCompletion(hwnd, st);
    EnsureCaretVisible(hwnd, st);
    NotifySelChange(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

/* ------------------------------------------------------------------ */
/*  Symbol auto pairing (enforced bracket completion)                  */
/* ------------------------------------------------------------------ */

static wchar_t MatchingCloser(wchar_t open)
{
    switch (open)
    {
    case L'(': return L')';
    case L'[': return L']';
    case L'{': return L'}';
    case L'<': return L'>';
    case L'"': return L'"';   // 单双引号当括号处理：括号的两侧是同一种引号
    case L'\'': return L'\'';
    default:    return 0;
    }
}

// Pairing is offered in every source language; plain text and Markdown keep
// the raw typing behaviour of a notepad.
static bool PairEnabled(const EditorState *st)
{
    if (!st->lang || !st->lang->id)
        return false;
    const std::wstring id = st->lang->id;
    return id != L"text" && id != L"md";
}

// A partner is auto-inserted only when what follows the caret cannot belong to
// an identifier, is not the closing character itself and is not an opening
// quote, and never right after a backslash (escaped literal character).
static bool CanAutoPair(const EditorState *st, wchar_t closeC)
{
    if (st->hasSel)
        return true; // wrap the selection in the new pair
    if (st->caret > 0 && st->text[st->caret - 1] == L'\\')
        return false;
    if (st->caret >= st->text.size())
        return true;
    const wchar_t next = st->text[st->caret];
    if (next == closeC || IsIdentifierChar(next))
        return false;
    if (next == L'"' || next == L'\'')
        return false;
    return true;
}

static bool IsQuoteChar(wchar_t c)
{
    return c == L'"' || c == L'\'';
}

// 引号补全：后面已经是同一种引号就跳过去，否则成对插入。
static bool CanAutoPairQuote(const EditorState *st, wchar_t q)
{
    if (st->hasSel)
        return true;
    if (st->caret > 0 && st->text[st->caret - 1] == L'\\')
        return false;
    if (st->caret >= st->text.size())
        return true;
    return st->text[st->caret] != q;
}

// 结构体 / 类 / 枚举 / 联合体的花括号：右侧大括号后面要补一个分号，
// 其余花括号（函数体、if/for 块、裸语句块）保持原样不加分号。
// 大括号另起一行（Allman 风格）时，把上一行甚至继承列表一并纳入判断。
static void TrimRight(std::wstring &s)
{
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' || s.back() == L'\r'))
        s.pop_back();
}

static bool HasTypeKeyword(const std::wstring &text)
{
    static const wchar_t *const kTypes[] = { L"struct", L"class", L"enum", L"union", nullptr };
    size_t i = 0;
    while (i < text.size())
    {
        if (!IsIdentifierChar(text[i]))
        {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < text.size() && IsIdentifierChar(text[j]))
            ++j;
        const std::wstring w = text.substr(i, j - i);
        for (int k = 0; kTypes[k] != nullptr; ++k)
        {
            if (w == kTypes[k])
                return true;
        }
        i = j;
    }
    return false;
}

static bool StructBraceContext(const EditorState *st)
{
    const int line = LineOfOffset(st, st->caret);
    std::vector<std::wstring> segs;
    std::wstring cur = st->text.substr(LineStart(st, line), LineCol(st, st->caret));
    TrimRight(cur);
    segs.push_back(cur);

    // 当前行只有缩进（"{" 独占一行）时向上收集：继承列表以 , / : 结尾、
    // 遇到 ) 或 ; 结尾的行说明是函数体或语句块，就此打住。
    for (int up = 1; segs[0].empty() && up <= 4 && line - up >= 0; ++up)
    {
        std::wstring t = LineContent(st, line - up);
        TrimRight(t);
        if (t.empty())
            continue;
        const wchar_t tail = t.back();
        segs.push_back(t);
        if (tail != L',' && tail != L':')
            break;
    }

    // 最近的非空行以 ) 或 ; 结尾 → 函数体 / 语句块，不加分号
    const std::wstring *nearest = nullptr;
    for (size_t s = 0; s < segs.size(); ++s)
    {
        if (!segs[s].empty())
        {
            nearest = &segs[s];
            break;
        }
    }
    if (nearest == nullptr)
        return false;
    if (nearest->back() == L')' || nearest->back() == L';')
        return false;

    for (size_t s = 0; s < segs.size(); ++s)
    {
        if (HasTypeKeyword(segs[s]))
            return true;
    }
    return false;
}

// 插入 "{};" 并把光标放在两个大括号之间：随后按 Enter 仍会拆成
// "{" / 缩进空行 / "}" 三行，分号留在右大括号后面。
static void InsertStructBrace(HWND hwnd, EditorState *st)
{
    const size_t at = st->caret;
    DoInsert(hwnd, st, at, at, L"{};");
    st->caret = at + 1;
    st->anchor = st->caret;
    st->hasSel = false;
    EnsureCaretVisible(hwnd, st);
    NotifySelChange(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// 找出包围当前光标位置的那个 "{" 所在的行号（用于判断「是不是在类体 /
// switch 里」，并据此算标签的缩进）。找不到未配对的开括号时返回 -1。
static int EnclosingBlockLine(const EditorState *st)
{
    int depth = 0;
    size_t p = st->caret;
    while (p > 0)
    {
        --p;
        const wchar_t c = st->text[p];
        if (c == L'}')
        {
            ++depth;
            continue;
        }
        if (c != L'{')
            continue;
        if (depth > 0)
        {
            --depth;
            continue;
        }
        return LineOfOffset(st, p);
    }
    return -1;
}

// 该 "{" 所在的上下文行：形如 "class Foo" 时继续向上把继承列表一起纳入。
static std::wstring EnclosingBlockHead(const EditorState *st, int braceLine)
{
    if (braceLine < 0)
        return std::wstring();
    std::wstring t = LineContent(st, braceLine);
    TrimRight(t);
    int line = braceLine;
    while (!t.empty() && (t.back() == L':' || t.back() == L',') && line > 0)
    {
        --line;
        std::wstring up = LineContent(st, line);
        TrimRight(up);
        if (up.empty())
            break;
        t = up + L" " + t;
    }
    return t;
}

/*  语法的强制补全：在代码语言里打完关键字再敲空格，直接带出结构骨架。
    if / for / while / switch / catch      →  kw () {\n     \n}
    do / try / namespace                   →  kw {\n     \n}
    public / private / protected / default →  kw: 换行并保持缩进
    （访问说明符只在类体里展开，default 只在 switch 里展开）          */
enum SynKind
{
    SYN_PARENS, // kw () { … }
    SYN_BLOCK,  // kw { … }
    SYN_LABEL   // kw: 换行缩进
};

struct SynBlock
{
    const wchar_t *kw;
    SynKind kind;
};

static const SynBlock kSynBlocks[] = {
    { L"if", SYN_PARENS },      { L"for", SYN_PARENS },     { L"while", SYN_PARENS },
    { L"switch", SYN_PARENS },  { L"catch", SYN_PARENS },
    { L"do", SYN_BLOCK },       { L"try", SYN_BLOCK },      { L"namespace", SYN_BLOCK },
    { L"public", SYN_LABEL },   { L"private", SYN_LABEL },  { L"protected", SYN_LABEL },
    { L"default", SYN_LABEL },
    { nullptr, SYN_PARENS }
};

static bool ExpandSyntaxBlock(HWND hwnd, EditorState *st)
{
    if (!PairEnabled(st))
        return false;
    size_t i = st->caret;
    while (i > 0 && IsIdentifierChar(st->text[i - 1]))
        --i;
    if (i == st->caret)
        return false;
    const std::wstring word = st->text.substr(i, st->caret - i);

    const SynBlock *hit = nullptr;
    for (int k = 0; kSynBlocks[k].kw != nullptr; ++k)
    {
        if (word == kSynBlocks[k].kw)
        {
            hit = &kSynBlocks[k];
            break;
        }
    }
    if (!hit)
        return false;

    // `using namespace std;` 里的 namespace 是 using 指令，不是命名空间定义：
    // 它后面不需要大括号，别补骨架（否则一条 using 会被拆成三段）。
    if (word == L"namespace")
    {
        size_t q = i;
        while (q > 0 && (st->text[q - 1] == L' ' || st->text[q - 1] == L'\t'))
            --q;
        const size_t e = q;
        while (q > 0 && IsIdentifierChar(st->text[q - 1]))
            --q;
        if (e > q && st->text.substr(q, e - q) == L"using")
            return false;
    }
    int braceLine = -1;
    if (hit->kind == SYN_LABEL)
    {
        braceLine = EnclosingBlockLine(st);
        const std::wstring ctx = EnclosingBlockHead(st, braceLine);
        if (ctx.empty())
            return false;
        if (word == L"default")
        {
            // default: 只属于 switch
            if (ctx.find(L"switch") == std::wstring::npos)
                return false;
        }
        else if (!HasTypeKeyword(ctx))
        {
            // public / private / protected 只属于类体
            return false;
        }

        // 标签整行重排：把「本行缩进 + 关键字」换成「类/switch 缩进 + 一级 +
        // 关键字:」，下一行缩进再深一级。这样 public: 与 default: 总是落在
        // 该在位置，不依赖当时光标前的缩进是否规整。
        const std::wstring bline = LineContent(st, braceLine);
        size_t q = 0;
        while (q < bline.size() && (bline[q] == L' ' || bline[q] == L'\t'))
            ++q;
        const std::wstring labelIndent = bline.substr(0, q) + L"    ";
        const std::wstring bodyIndent = labelIndent + L"    ";
        const size_t ls = LineStart(st, LineOfOffset(st, st->caret));
        const size_t at = st->caret;
        const std::wstring repl = labelIndent + word + L":\n" + bodyIndent;
        DoInsert(hwnd, st, ls, at, repl);
        st->caret = ls + repl.size();
        st->anchor = st->caret;
        st->hasSel = false;
        EnsureCaretVisible(hwnd, st);
        NotifySelChange(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }

    // 行首缩进
    const int line = LineOfOffset(st, st->caret);
    const std::wstring head = LineContent(st, line);
    size_t p = 0;
    while (p < head.size() && (head[p] == L' ' || head[p] == L'\t'))
        ++p;
    const std::wstring indent = head.substr(0, p);
    const std::wstring inner = indent + L"    ";

    std::wstring ins;
    size_t caretOff = 0;
    if (hit->kind == SYN_PARENS)
    {
        ins = L" () {\n" + inner + L"\n" + indent + L"}";
        caretOff = 2; // 落在 ( 和 ) 之间
    }
    else
    {
        ins = L" {\n" + inner + L"\n" + indent + L"}";
        caretOff = 3; // 落在 { 之后
    }

    const size_t at = st->caret;
    DoInsert(hwnd, st, at, at, ins);
    st->caret = at + caretOff;
    st->anchor = st->caret;
    st->hasSel = false;
    EnsureCaretVisible(hwnd, st);
    NotifySelChange(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

// "<" is treated as a template bracket only on a line that carries a '#'
// (e.g. #include <vector>) or that already mentions an STL container, so a
// plain "a < b" comparison is never turned into "a <> b".
static bool AnglePairContext(const EditorState *st)
{
    if (!st->lang || !st->lang->id)
        return false;
    const std::wstring id = st->lang->id;
    if (id != L"cpp" && id != L"c" && id != L"cs" && id != L"objc" && id != L"java" &&
        id != L"ts" && id != L"rust")
        return false;

    const int line = LineOfOffset(st, st->caret);
    const std::wstring prefix =
        st->text.substr(LineStart(st, line), LineCol(st, st->caret));
    if (prefix.find(L'#') != std::wstring::npos)
        return true;
    // "template<" 也是模板语法，< 在这里同样成对出现
    if (prefix.find(L"template") != std::wstring::npos)
        return true;

    static const wchar_t *const kStlNames[] = {
        L"vector", L"map", L"set", L"list", L"deque", L"queue", L"stack", L"array",
        L"string", L"wstring", L"pair", L"tuple", L"optional", L"variant", L"function",
        L"bitset", L"unordered_map", L"unordered_set", L"multimap", L"multiset",
        L"priority_queue", L"shared_ptr", L"unique_ptr", L"weak_ptr", L"span",
        L"string_view", L"forward_list", L"valarray", L"complex", L"allocator",
        L"iterator", L"std", nullptr
    };

    size_t i = 0;
    while (i < prefix.size())
    {
        if (!IsIdentifierChar(prefix[i]))
        {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < prefix.size() && IsIdentifierChar(prefix[j]))
            ++j;
        const std::wstring word = prefix.substr(i, j - i);
        for (int k = 0; kStlNames[k] != nullptr; ++k)
        {
            if (word == kStlNames[k])
                return true;
        }
        i = j;
    }
    return false;
}

// Insert the pair and leave the caret between the two characters; with a
// selection the pair wraps the selected text and keeps it selected.
static void InsertPair(HWND hwnd, EditorState *st, wchar_t open)
{
    const wchar_t closeC = MatchingCloser(open);
    if (closeC == 0)
        return;

    if (st->hasSel)
    {
        const size_t a = (std::min)(st->anchor, st->caret);
        const size_t b = (std::max)(st->anchor, st->caret);
        const std::wstring inner = st->text.substr(a, b - a);
        DoInsert(hwnd, st, a, b,
                 std::wstring(1, open) + inner + std::wstring(1, closeC));
        st->anchor = a + 1;
        st->caret = a + 1 + inner.size();
        st->hasSel = (st->caret != st->anchor);
    }
    else
    {
        const size_t at = st->caret;
        DoInsert(hwnd, st, at, at,
                 std::wstring(1, open) + std::wstring(1, closeC));
        st->caret = at + 1;
        st->anchor = st->caret;
        st->hasSel = false;
    }
    EnsureCaretVisible(hwnd, st);
    NotifySelChange(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void MoveVertical(HWND hwnd, EditorState *st, int deltaRows, bool extend)
{
    if (st->wrap)
    {
        // 折行只是显示效果：自动换行出来的几个视觉行严格来说是同一行，
        // 所以上下移动按“逻辑行”走，不会停在同一逻辑行的下一个视觉行上。
        int line = LineOfOffset(st, st->caret);
        int nl = line + deltaRows;
        int last = (std::max)(0, LineCount(st) - 1);
        if (nl < 0)
            nl = 0;
        if (nl > last)
            nl = last;

        // 保持当前显示列不变，越界就落在行尾。
        int col = DispColAtOffset(st, st->caret);
        if (col < 0)
            col = 0;
        std::wstring content = LineContent(st, nl);
        size_t idx = IndexForDisp(content, col);
        if (idx > content.size())
            idx = content.size();
        MoveCaretTo(hwnd, st, LineStart(st, nl) + idx, extend);
        return;
    }

    int total = (std::max)(1, TotalRows(hwnd, st));
    int row = RowOfOffset(hwnd, st, st->caret);
    int nrow = row + deltaRows;
    nrow = (std::max)(0, (std::min)(nrow, total - 1));

    int col = DispColAtOffset(st, st->caret);
    size_t newPos = OffsetAt(hwnd, st, nrow, col);
    MoveCaretTo(hwnd, st, newPos, extend);
}

/* ------------------------------------------------------------------ */
/*  Clipboard                                                          */
/* ------------------------------------------------------------------ */

static std::wstring SelectionText(const EditorState *st)
{
    if (!st->hasSel)
        return L"";
    size_t a = (std::min)(st->anchor, st->caret);
    size_t b = (std::max)(st->anchor, st->caret);
    return st->text.substr(a, b - a);
}

static void DoCopy(HWND hwnd, const EditorState *st)
{
    std::wstring sel = SelectionText(st);
    if (sel.empty())
        return;
    if (!OpenClipboard(hwnd))
        return;
    EmptyClipboard();
    size_t bytes = (sel.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h)
    {
        void *p = GlobalLock(h);
        memcpy(p, sel.c_str(), bytes);
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
}

static void DoPaste(HWND hwnd, EditorState *st)
{
    if (!OpenClipboard(hwnd))
        return;
    std::wstring s;
    if (IsClipboardFormatAvailable(CF_UNICODETEXT))
    {
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h)
        {
            const wchar_t *p = static_cast<const wchar_t *>(GlobalLock(h));
            if (p)
            {
                s = p;
                GlobalUnlock(h);
            }
        }
    }
    CloseClipboard();
    if (s.empty())
        return;
    size_t a = st->hasSel ? (std::min)(st->anchor, st->caret) : st->caret;
    size_t b = st->hasSel ? (std::max)(st->anchor, st->caret) : st->caret;
    DoInsert(hwnd, st, a, b, s);
}

static void DoCut(HWND hwnd, EditorState *st)
{
    DoCopy(hwnd, st);
    DeleteSelection(hwnd, st);
}

/* ------------------------------------------------------------------ */
/*  Code completion                                                    */
/* ------------------------------------------------------------------ */

static void BuildCandidates(EditorState *st)
{
    std::set<std::wstring> seen;
    st->candidates.clear();
    const LanguageDef *L = st->lang;

    // 1. User-defined symbols (functions / classes / structs / variables /
    //    enums / macros / modules) scanned from the whole document and from
    //    the user's local headers. System-header symbols (src==1) are kept
    //    only for diagnostics, not completion (too noisy; std functions are
    //    covered by the preset table below).
    for (const UserSym &s : st->userSyms)
    {
        if (s.src == 1)
            continue;
        std::wstring tag = L"u";
        tag += s.tag;
        if (seen.insert(s.name).second)
            st->candidates.push_back({s.name, tag});
    }
    // 2. Preset library functions (with their header requirement for checks).
    if (L && L->id)
    {
        size_t pc = 0;
        const PresetFunc *presets = PresetsFor(L->id, pc);
        for (size_t i = 0; i < pc; ++i)
            if (seen.insert(presets[i].name).second)
                st->candidates.push_back({presets[i].name, L"F"});
    }
    // 2b. Preset variables / constants (typed prefix from the tag field).
    if (L && L->id)
    {
        size_t vc = 0;
        const PresetVar *vars = PresetVarsFor(L->id, vc);
        for (size_t i = 0; i < vc; ++i)
        {
            std::wstring tag(1, vars[i].tag);
            if (seen.insert(vars[i].name).second)
                st->candidates.push_back({vars[i].name, tag});
        }
    }
    // 3. Language keywords / built-in types / literals.
    if (L)
    {
        for (const wchar_t *const *p = L->keywords; p && *p; ++p)
            if (seen.insert(*p).second)
                st->candidates.push_back({*p, L"K"});
        for (const wchar_t *const *p = L->types; p && *p; ++p)
            if (seen.insert(*p).second)
                st->candidates.push_back({*p, L"C"});
        for (const wchar_t *const *p = L->literals; p && *p; ++p)
            if (seen.insert(*p).second)
                st->candidates.push_back({*p, L"L"});
    }
    // 4. Any other identifier in the document (>=2 chars) -> user variable guess.
    size_t i = 0;
    const size_t n = st->text.size();
    while (i < n)
    {
        if (IsIdentifierStart(st->text[i]))
        {
            size_t j = i;
            while (j < n && IsIdentifierChar(st->text[j]))
                ++j;
            if (j - i >= 2)
            {
                std::wstring w = st->text.substr(i, j - i);
                if (seen.insert(w).second)
                    st->candidates.push_back({w, L"uV"});
            }
            i = j;
        }
        else
            ++i;
    }
    if (st->candidates.size() > 2000)
        st->candidates.resize(2000);
    st->candidatesDirty = false;
    st->lastBuildLen = st->text.size();
}

static void FilterMatches(EditorState *st, const std::wstring &prefix)
{
    st->matches.clear();
    for (const auto &c : st->candidates)
    {
        if (prefix.size() <= c.name.size() &&
            std::equal(prefix.begin(), prefix.end(), c.name.begin(),
                       [](wchar_t x, wchar_t y) { return towlower(x) == towlower(y); }))
            st->matches.push_back(c);
    }
    std::stable_sort(st->matches.begin(), st->matches.end(),
                     [&](const CompItem &a, const CompItem &b)
                     {
                         // Best-match ranking: exact-case prefix first, then shorter,
                         // then case-insensitive alphabetical.
                         bool ea = (a.name.size() >= prefix.size() &&
                                    a.name.compare(0, prefix.size(), prefix) == 0);
                         bool eb = (b.name.size() >= prefix.size() &&
                                    b.name.compare(0, prefix.size(), prefix) == 0);
                         if (ea != eb)
                             return ea;
                         if (a.name.size() != b.name.size())
                             return a.name.size() < b.name.size();
                         return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
                     });
    st->compSel = 0;
    st->compTop = 0;
}

static void UpdateCompTop(EditorState *st)
{
    int n = static_cast<int>(st->matches.size());
    if (n <= 3)
    {
        st->compTop = 0;
        return;
    }
    int want = st->compSel - 1; // keep the selection in the middle visible row
    if (want < 0)
        want = 0;
    if (want > n - 3)
        want = n - 3;
    st->compTop = want;
}

static void ShowCompletion(HWND hwnd, EditorState *st)
{
    if (st->matches.empty())
    {
        HideCompletion(hwnd, st);
        return;
    }
    if (!st->popup)
    {
        st->popup = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
                                    kCompletionClass, L"", WS_POPUP, 0, 0, 80, 40,
                                    GetParent(hwnd), nullptr, GetModuleHandleW(nullptr), nullptr);
        if (st->popup)
        {
            SetWindowLongPtrW(st->popup, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(hwnd));
            // Semi-transparent popup list.
            SetLayeredWindowAttributes(st->popup, 0, 200, LWA_ALPHA);
        }
    }
    if (!st->popup)
        return;

    int itemH = (std::max)(12, st->lineH);
    size_t shown = (std::min)(st->matches.size(), static_cast<size_t>(3));
    if (shown < 1)
        shown = 1;
    UpdateCompTop(st);

    int row = RowOfOffset(hwnd, st, st->caret);
    int col = DispColAtOffset(st, st->caret);
    int baseDc = 0;
    if (st->wrap)
        baseDc = LineColOfRow(hwnd, st, row).second;
    int px = st->gutterW + (col - st->leftChar - baseDc) * st->charW;
    int py = (row - st->topRow) * st->lineH + st->lineH;
    POINT pt = {px, py};
    ClientToScreen(hwnd, &pt);

    int maxW = 0;
    for (const auto &m : st->matches)
    {
        int w = static_cast<int>(m.tag.size()) + 1; // tag + separating space (tags are ASCII)
        w += DispWidth(m.name, m.name.size());
        maxW = (std::max)(maxW, w);
    }
    int popupW = (std::max)(96, (std::min)(360, maxW * st->charW + 24));
    int popupH = static_cast<int>(shown) * itemH + 4;

    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    HMONITOR hm = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    if (GetMonitorInfoW(hm, &mi))
    {
        RECT wa = mi.rcWork;
        if (pt.y + popupH > wa.bottom)
            pt.y -= popupH + st->lineH;
        if (pt.x + popupW > wa.right)
            pt.x = wa.right - popupW;
        if (pt.x < wa.left)
            pt.x = wa.left;
        if (pt.y < wa.top)
            pt.y = wa.top;
    }
    SetWindowPos(st->popup, HWND_TOPMOST, pt.x, pt.y, popupW, popupH, SWP_NOACTIVATE);
    ShowWindow(st->popup, SW_SHOWNA);
    InvalidateRect(st->popup, nullptr, TRUE);
}

static void TriggerCompletion(HWND hwnd, EditorState *st, bool force)
{
    size_t ws = st->caret;
    while (ws > 0 && IsIdentifierChar(st->text[ws - 1]))
        --ws;
    st->compWordStart = ws;

    if (st->candidatesDirty || st->text.size() > st->lastBuildLen + 512)
        BuildCandidates(st);

    std::wstring prefix = st->text.substr(ws, st->caret - ws);

    // 光标前还没有正在输入的词（刚敲完空格 / 括号 / 标点）时不该弹窗：那时
    // 候选表会和屏幕上这个位置毫无关系，看着像是“自动补全乱弹”。
    if (prefix.empty() && !force)
    {
        st->compActive = false;
        HideCompletion(hwnd, st);
        return;
    }

    FilterMatches(st, prefix);

    // 打完的这个词本身就已经是唯一候选 —— 没有可补的内容，不弹窗。
    if (!prefix.empty() && !st->matches.empty())
    {
        bool anyLonger = false;
        for (const auto &m : st->matches)
        {
            if (m.name.size() > prefix.size())
            {
                anyLonger = true;
                break;
            }
        }
        if (!anyLonger)
            st->matches.clear();
    }

    if (!st->matches.empty() || (force && !prefix.empty()))
    {
        st->compActive = true;
        ShowCompletion(hwnd, st);
    }
    else
    {
        st->compActive = false;
        HideCompletion(hwnd, st);
    }
}

static void AcceptCompletion(HWND hwnd, EditorState *st, size_t index)
{
    if (!st->compActive || index >= st->matches.size())
    {
        HideCompletion(hwnd, st);
        return;
    }
    std::wstring word = st->matches[index].name;
    size_t ws = (std::min)(st->compWordStart, st->text.size());
    HideCompletion(hwnd, st);
    DoInsert(hwnd, st, ws, st->caret, word);
    EnsureCaretVisible(hwnd, st);
    InvalidateRect(hwnd, nullptr, FALSE);
}

LRESULT CALLBACK CompletionWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    HWND editor = reinterpret_cast<HWND>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    EditorState *st = editor ? GetState(editor) : nullptr;
    if (!st)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        COLORREF bg = IsDarkMode() ? RGB(37, 37, 38) : RGB(240, 240, 240);
        COLORREF fg = IsDarkMode() ? RGB(220, 220, 220) : RGB(0, 0, 0);
        COLORREF selBg = IsDarkMode() ? RGB(51, 110, 158) : RGB(150, 190, 240);
        COLORREF selFg = IsDarkMode() ? RGB(255, 255, 255) : RGB(0, 0, 0);

        HBRUSH hb = CreateSolidBrush(bg);
        FillRect(dc, &rc, hb);
        DeleteObject(hb);

        HFONT use = st->font ? st->font : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HFONT old = reinterpret_cast<HFONT>(SelectObject(dc, use));
        SetBkMode(dc, TRANSPARENT);

        int itemH = (std::max)(12, st->lineH);
        int shown = (std::min)(static_cast<int>(st->matches.size()), 3);
        int top = st->compTop;
        for (int i = 0; i < shown; ++i)
        {
            int mi = top + i;
            RECT it = {2, i * itemH + 2, rc.right - 2, (i + 1) * itemH + 2};
            if (mi == st->compSel)
            {
                HBRUSH sh = CreateSolidBrush(selBg);
                FillRect(dc, &it, sh);
                DeleteObject(sh);
                SetTextColor(dc, selFg);
            }
            else
                SetTextColor(dc, fg);
            const CompItem &ci = st->matches[mi];
            std::wstring tagLine = ci.tag + L" ";
            SIZE szTag = {};
            GetTextExtentPoint32W(dc, tagLine.c_str(), static_cast<int>(tagLine.size()), &szTag);
            // Type prefix in a muted color, then the name.
            RECT tr = it;
            tr.left += 4;
            SetTextColor(dc, mi == st->compSel ? selFg : (IsDarkMode() ? RGB(150, 150, 150) : RGB(120, 120, 120)));
            DrawTextW(dc, tagLine.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
            tr.left += szTag.cx;
            SetTextColor(dc, mi == st->compSel ? selFg : fg);
            DrawTextW(dc, ci.name.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        }
        // "more items below" indicator when the list is scrollable
        if (static_cast<int>(st->matches.size()) > top + shown)
        {
            RECT mr = {rc.left, rc.bottom - 9, rc.right, rc.bottom};
            SetTextColor(dc, RGB(130, 130, 130));
            DrawTextW(dc, L"\u25BC", -1, &mr, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }
        SelectObject(dc, old);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        int itemH = (std::max)(12, st->lineH);
        int y = GET_Y_LPARAM(lParam);
        int row = (y - 2) / itemH;
        int idx = st->compTop + row;
        if (idx < 0)
            idx = 0;
        if (idx >= static_cast<int>(st->matches.size()))
            idx = static_cast<int>(st->matches.size()) - 1;
        if (idx != st->compSel)
        {
            st->compSel = idx;
            UpdateCompTop(st);
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    }
    case WM_LBUTTONUP:
    {
        int itemH = (std::max)(12, st->lineH);
        int y = GET_Y_LPARAM(lParam);
        int row = (y - 2) / itemH;
        int idx = st->compTop + row;
        if (idx >= 0 && idx < static_cast<int>(st->matches.size()))
        {
            st->compSel = idx;
            AcceptCompletion(editor, st, static_cast<size_t>(idx));
        }
        else
            HideCompletion(editor, st);
        return 0;
    }
    case WM_LBUTTONDOWN:
        return 0;
    case WM_MOUSEWHEEL:
    {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int step = (delta < 0) ? 1 : -1;
        int n = static_cast<int>(st->matches.size());
        st->compSel += step;
        if (st->compSel < 0)
            st->compSel = 0;
        if (st->compSel >= n)
            st->compSel = (n > 0) ? n - 1 : 0;
        UpdateCompTop(st);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

std::wstring CE_GetText(HWND hwnd)
{
    EditorState *st = GetState(hwnd);
    return st ? st->text : L"";
}

void CE_SetText(HWND hwnd, const std::wstring &text)
{
    EditorState *st = GetState(hwnd);
    if (!st)
        return;
    std::wstring norm;
    norm.reserve(text.size());
    for (wchar_t c : text)
        if (c != L'\r')
            norm += c;
    st->text = norm;
    InvalidateLineIndex(st);
    st->caret = 0;
    st->anchor = 0;
    st->hasSel = false;
    st->topRow = 0;
    st->leftChar = 0;
    st->undo.clear();
    st->redo.clear();
    st->candidatesDirty = true;
    st->lastBuildLen = 0;
    RecomputeMetrics(hwnd, st);
    RecomputeLineStates(st, 0);
    RefreshSemantic(hwnd, st);
    UpdateScrollbars(hwnd, st);
    InvalidateRect(hwnd, nullptr, TRUE);
}

void CE_GetSelection(HWND hwnd, DWORD *start, DWORD *end)
{
    EditorState *st = GetState(hwnd);
    if (!st)
        return;
    if (start)
        *start = static_cast<DWORD>((std::min)(st->anchor, st->caret));
    if (end)
        *end = static_cast<DWORD>((std::max)(st->anchor, st->caret));
}

void CE_SetSelection(HWND hwnd, DWORD start, DWORD end)
{
    EditorState *st = GetState(hwnd);
    if (!st)
        return;
    if (end == static_cast<DWORD>(-1))
        end = static_cast<DWORD>(st->text.size());
    if (start > st->text.size())
        start = static_cast<DWORD>(st->text.size());
    if (end > st->text.size())
        end = static_cast<DWORD>(st->text.size());
    st->anchor = start;
    st->caret = end;
    st->hasSel = (start != end);
    HideCompletion(hwnd, st);
    EnsureCaretVisible(hwnd, st);
    NotifySelChange(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void CE_ReplaceSelection(HWND hwnd, const wchar_t *text)
{
    EditorState *st = GetState(hwnd);
    if (!st)
        return;
    if (!text)
        text = L"";
    size_t a = st->hasSel ? (std::min)(st->anchor, st->caret) : st->caret;
    size_t b = st->hasSel ? (std::max)(st->anchor, st->caret) : st->caret;
    HideCompletion(hwnd, st);
    DoInsert(hwnd, st, a, b, text);
}

int CE_LineFromChar(HWND hwnd, DWORD pos)
{
    EditorState *st = GetState(hwnd);
    return st ? LineOfOffset(st, pos) : 0;
}

int CE_LineIndex(HWND hwnd, int line)
{
    EditorState *st = GetState(hwnd);
    if (!st)
        return -1;
    if (line < 0 || line >= LineCount(st))
        return -1;
    return static_cast<int>(LineStart(st, line));
}

void CE_ScrollCaret(HWND hwnd)
{
    EditorState *st = GetState(hwnd);
    if (!st)
        return;
    EnsureCaretVisible(hwnd, st);
    InvalidateRect(hwnd, nullptr, FALSE);
}

std::pair<int, int> CE_GetCursor(HWND hwnd)
{
    EditorState *st = GetState(hwnd);
    if (!st)
        return {1, 1};
    // Column is reported in display columns (CJK char = 2 columns) so the
    // status bar matches what the user sees.
    return {LineOfOffset(st, st->caret) + 1, DispColAtOffset(st, st->caret) + 1};
}

void CE_Undo(HWND hwnd)
{
    EditorState *st = GetState(hwnd);
    if (!st || st->undo.empty())
        return;
    UndoAction a = st->undo.back();
    st->undo.pop_back();
    InvalidateLineIndex(st);
    st->text.replace(a.start, a.inserted.size(), a.removed);
    InvalidateLineIndex(st);
    st->caret = a.start;
    st->anchor = a.start;
    st->hasSel = false;
    st->redo.push_back(a);
    HideCompletion(hwnd, st);
    st->candidatesDirty = true;
    RecomputeLineStates(st, LineOfOffset(st, a.start));
    RefreshSemantic(hwnd, st);
    UpdateScrollbars(hwnd, st);
    NotifyChange(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void CE_Redo(HWND hwnd)
{
    EditorState *st = GetState(hwnd);
    if (!st || st->redo.empty())
        return;
    UndoAction a = st->redo.back();
    st->redo.pop_back();
    st->text.replace(a.start, a.removed.size(), a.inserted);
    InvalidateLineIndex(st);
    st->caret = a.start + a.inserted.size();
    st->anchor = st->caret;
    st->hasSel = false;
    st->undo.push_back(a);
    HideCompletion(hwnd, st);
    st->candidatesDirty = true;
    RecomputeLineStates(st, LineOfOffset(st, a.start));
    RefreshSemantic(hwnd, st);
    UpdateScrollbars(hwnd, st);
    NotifyChange(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

bool CE_CanUndo(HWND hwnd)
{
    EditorState *st = GetState(hwnd);
    return st && !st->undo.empty();
}

void CE_SelectAll(HWND hwnd)
{
    EditorState *st = GetState(hwnd);
    if (!st)
        return;
    st->anchor = 0;
    st->caret = st->text.size();
    st->hasSel = !st->text.empty();
    HideCompletion(hwnd, st);
    NotifySelChange(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void CE_InsertAtCaret(HWND hwnd, const std::wstring &s)
{
    EditorState *st = GetState(hwnd);
    if (!st)
        return;
    size_t a = st->hasSel ? (std::min)(st->anchor, st->caret) : st->caret;
    size_t b = st->hasSel ? (std::max)(st->anchor, st->caret) : st->caret;
    HideCompletion(hwnd, st);
    DoInsert(hwnd, st, a, b, s);
}

void CE_SetFont(HWND hwnd, HFONT font)
{
    EditorState *st = GetState(hwnd);
    if (!st)
        return;
    st->font = font;
    RecomputeMetrics(hwnd, st);
    UpdateScrollbars(hwnd, st);
    InvalidateRect(hwnd, nullptr, TRUE);
}

void CE_SetWordWrap(HWND hwnd, bool wrap)
{
    EditorState *st = GetState(hwnd);
    if (!st)
        return;
    st->wrap = wrap;
    if (wrap)
        st->leftChar = 0;
    RecomputeGutter(st);   // 折行时行号栏要多留一列画 ┌ │ └
    UpdateScrollbars(hwnd, st);
    InvalidateRect(hwnd, nullptr, TRUE);
}

void CE_Refresh(HWND hwnd)
{
    InvalidateRect(hwnd, nullptr, TRUE);
}

// 「编译 → 代码检查」开关切换后同步编辑器：关掉时立刻抹掉红线和待弹的提示，
// 重新打开时重扫一遍把问题标回来。
void CE_SetCodeCheck(HWND hwnd)
{
    EditorState *st = GetState(hwnd);
    if (!st)
        return;
    if (!g_state.codeCheck)
    {
        st->diagLines.clear();
        st->diagMsgs.clear();
        st->diagPopupArmed = false;
        st->lastPoppedDiags.clear();
        KillTimer(hwnd, 2);
    }
    else
    {
        RefreshSemantic(hwnd, st);
    }
    InvalidateRect(hwnd, nullptr, TRUE);
}

void CE_SetLanguage(HWND hwnd, const LanguageDef *lang)
{
    EditorState *st = GetState(hwnd);
    if (!st)
        return;
    st->lang = lang;
    st->candidatesDirty = true;
    st->lastBuildLen = 0;
    RecomputeLineStates(st, 0);
    RefreshSemantic(hwnd, st);
    InvalidateRect(hwnd, nullptr, TRUE);
}

void CE_SetFilePath(HWND hwnd, const std::wstring &path)
{
    EditorState *st = GetState(hwnd);
    if (!st)
        return;
    if (st->filePath != path)
    {
        st->filePath = path;
        RefreshSemantic(hwnd, st); // pick up local-header symbols for the new dir
        InvalidateRect(hwnd, nullptr, TRUE);
    }
}

void CE_SuppressPendingCheck(HWND hwnd)
{
    EditorState *st = GetState(hwnd);
    if (!st)
        return;
    KillTimer(hwnd, 2);
    if (st->diagPopupArmed && !st->diagMsgs.empty())
    {
        st->diagPopupArmed = false;
        st->lastPoppedDiags = st->diagMsgs; // don't re-nag for the same errors
    }
}

void CE_RequestCompletion(HWND hwnd)
{
    EditorState *st = GetState(hwnd);
    if (!st)
        return;
    TriggerCompletion(hwnd, st, true);
}

void CE_CommitCompletion(HWND hwnd)
{
    EditorState *st = GetState(hwnd);
    if (!st)
        return;
    AcceptCompletion(hwnd, st, static_cast<size_t>(st->compSel));
}

/* ------------------------------------------------------------------ */
/*  Window procedure                                                   */
/* ------------------------------------------------------------------ */

// Blits the composed frame onto the window DC, limited to the damaged rect so
// a caret blink does not push a whole screen worth of pixels.
static void BlitBackBuffer(HDC hdc, HDC memDC, const RECT &damage)
{
    int x = damage.left;
    int y = damage.top;
    int w = damage.right - damage.left;
    int h = damage.bottom - damage.top;
    if (w <= 0 || h <= 0)
        return;
    BitBlt(hdc, x, y, w, h, memDC, x, y, SRCCOPY);
}

// Returns a DC whose bitmap matches the client size, or nullptr when a
// back buffer cannot be allocated (then the caller paints straight to screen).
static HDC EnsureBackBuffer(EditorState *st, HDC refDC, int width, int height)
{
    if (!st || width <= 0 || height <= 0)
        return nullptr;
    if (!st->backDC)
    {
        st->backDC = CreateCompatibleDC(refDC);
        if (!st->backDC)
            return nullptr;
    }
    if (!st->backBmp || st->backW != width || st->backH != height)
    {
        if (st->backBmp)
        {
            SelectObject(st->backDC, st->backOld);
            DeleteObject(st->backBmp);
            st->backBmp = nullptr;
        }
        st->backBmp = CreateCompatibleBitmap(refDC, width, height);
        if (!st->backBmp)
            return nullptr;
        st->backOld = reinterpret_cast<HBITMAP>(SelectObject(st->backDC, st->backBmp));
        st->backW = width;
        st->backH = height;
    }
    return st->backDC;
}

LRESULT CALLBACK CodeEditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_NCCREATE:
    {
        EditorState *ns = new EditorState();
        ns->font = g_state.hFont;
        ns->lang = LanguageById(L"text");
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ns));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    case WM_CREATE:
    {
        EditorState *st = GetState(hwnd);
        if (st)
        {
            RecomputeMetrics(hwnd, st);
            RecomputeLineStates(st, 0);
        }
        return 0;
    }
    case WM_NCDESTROY:
    {
        EditorState *st = GetState(hwnd);
        if (st)
        {
            if (st->popup)
                DestroyWindow(st->popup);
            if (st->backDC)
            {
                if (st->backBmp)
                {
                    SelectObject(st->backDC, st->backOld);
                    DeleteObject(st->backBmp);
                }
                DeleteDC(st->backDC);
            }
            delete st;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    case WM_ERASEBKGND:
        return 1; // everything is painted in WM_PAINT

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        EditorState *st = GetState(hwnd);

        // Compose the frame off screen, then blit it in one go. Without this
        // the direct-to-window painting showed the background fill first and
        // the text afterwards, which read as flicker on every scroll tick.
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;
        HDC memDC = EnsureBackBuffer(st, hdc, width, height);
        HDC dc = memDC ? memDC : hdc;

        if (g_state.background.enabled && g_bgImage)
        {
            UpdateBackgroundBitmap(hwnd);
            if (g_bgBitmap)
            {
                HDC mem = CreateCompatibleDC(dc);
                HBITMAP old = reinterpret_cast<HBITMAP>(SelectObject(mem, g_bgBitmap));
                BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
                SelectObject(mem, old);
                DeleteDC(mem);
            }
            else
            {
                HBRUSH hb = CreateSolidBrush(ColBg());
                FillRect(dc, &rc, hb);
                DeleteObject(hb);
            }
        }
        else
        {
            HBRUSH hb = CreateSolidBrush(ColBg());
            FillRect(dc, &rc, hb);
            DeleteObject(hb);
        }

        if (!st)
        {
            if (memDC)
                BlitBackBuffer(hdc, memDC, ps.rcPaint);
            EndPaint(hwnd, &ps);
            return 0;
        }

        if (LineCount(st) != st->lastLineCount)
            RecomputeGutter(st);

        int fc = FitChars(hwnd, st);
        int vis = VisibleRows(hwnd, st);
        int nlines = LineCount(st);
        int totalRows = TotalRows(hwnd, st);
        int caretLine = LineOfOffset(st, st->caret);

        RECT gr = {rc.left, rc.top, st->gutterW, rc.bottom};
        HBRUSH ghb = CreateSolidBrush(ColGutter());
        FillRect(dc, &gr, ghb);
        DeleteObject(ghb);

        HFONT use = st->font ? st->font : static_cast<HFONT>(GetStockObject(ANSI_FIXED_FONT));
        HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(dc, use));
        SetBkMode(dc, TRANSPARENT);

        size_t selA = (std::min)(st->anchor, st->caret);
        size_t selB = (std::max)(st->anchor, st->caret);

        std::vector<LineToken> tok;
        for (int vr = 0; vr < vis; ++vr)
        {
            int row = st->topRow + vr;
            int y = vr * st->lineH;
            if (row < 0 || row >= totalRows)
                break;

            auto p = LineColOfRow(hwnd, st, row);
            int line = p.first;
            int baseDc = p.second;
            if (line < 0 || line >= nlines)
                continue;

            std::wstring content = LineContent(st, line);
            size_t len = content.size();
            std::vector<int> starts;
            int within = 0;
            size_t rowStartIdx = 0;
            size_t rowEndIdx = len;
            if (st->wrap)
            {
                WrapRowStarts(content, fc, starts);
                EditorState *s = const_cast<EditorState *>(st);
                EnsureWrap(hwnd, s);
                within = row - s->wrapPrefix[line];
                int last = static_cast<int>(starts.size()) - 2;
                if (within > last)
                    within = last;
                if (within < 0)
                    within = 0;
                rowStartIdx = static_cast<size_t>(starts[within]);
                rowEndIdx = static_cast<size_t>(starts[within + 1]);
            }
            if (rowStartIdx >= len && len > 0)
                continue;

            bool lineRed = std::find(st->diagLines.begin(), st->diagLines.end(), line) != st->diagLines.end();

            // Error row: paint the text area with a red background, but keep
            // the syntax text colours intact (no forced red text).
            if (lineRed)
            {
                HBRUSH ehb = CreateSolidBrush(ColErrBg());
                RECT er = {st->gutterW, y, rc.right, y + st->lineH};
                FillRect(dc, &er, ehb);
                DeleteObject(ehb);
            }

            // Gutter numbering.  A hard line that got soft-wrapped into several
            // visual rows counts as ONE line: the number is printed only once,
            // vertically centred over the block, and the block is bracketed on
            // the left with ┌ / │ / └ so it is obvious the rows belong together.
            int lineRows = LineRows(hwnd, st, line);
            {
                bool cur = (line == caretLine);
                SetTextColor(dc, cur ? ColGutterCur() : ColGutterText());
                wchar_t buf[24];
                swprintf(buf, 24, L"%d", line + 1);
                if (lineRows <= 1)
                {
                    RECT tr = {2, y, st->gutterW - 4, y + st->lineH};
                    DrawTextW(dc, buf, -1, &tr, DT_SINGLELINE | DT_RIGHT | DT_VCENTER);
                }
                else
                {
                    // 折行块左侧的中括号：竖线逐行相接，首行/末行加一横臂。
                    // 用线条画而不是字形，字形在不同字体里高度不一致会在拐角断线。
                    const int bx = 7;
                    const int arm = (std::max)(9, st->charW / 2 + 2);
                    HPEN pen = CreatePen(PS_SOLID, 1, cur ? ColGutterCur() : ColGutterText());
                    HGDIOBJ oldPen = SelectObject(dc, pen);
                    MoveToEx(dc, bx, y, nullptr);
                    LineTo(dc, bx, y + st->lineH);
                    if (within == 0)
                    {
                        MoveToEx(dc, bx, y + 1, nullptr);
                        LineTo(dc, bx + arm, y + 1);
                    }
                    else if (within >= lineRows - 1)
                    {
                        MoveToEx(dc, bx, y + st->lineH - 1, nullptr);
                        LineTo(dc, bx + arm, y + st->lineH - 1);
                    }
                    SelectObject(dc, oldPen);
                    DeleteObject(pen);
                    // The number appears once, vertically centred on the whole block.
                    // 偶数个视觉行没有正中间那一行，就把数字画在中间两行的交界处。
                    int mid = (lineRows - 1) / 2;
                    if (within == mid)
                    {
                        int hgt = ((lineRows % 2) == 0) ? 2 * st->lineH : st->lineH;
                        RECT tr = {bx + arm + 2, y, st->gutterW - 4, y + hgt};
                        DrawTextW(dc, buf, -1, &tr, DT_SINGLELINE | DT_RIGHT | DT_VCENTER);
                    }
                }
            }

            // Cumulative display columns for every char offset in this line.
            std::vector<int> colX(content.size() + 1);
            colX[0] = 0;
            for (size_t k = 0; k < content.size(); ++k)
            {
                wchar_t nx = (k + 1 < content.size()) ? content[k + 1] : 0;
                colX[k + 1] = colX[k] + CharCols(content[k], nx);
            }

            size_t ls = LineStart(st, line);
            size_t ovA = (std::max)(ls + rowStartIdx, selA);
            size_t ovB = (std::min)(ls + rowEndIdx, selB);
            if (ovA < ovB)
            {
                size_t ca = ovA - ls;
                size_t cb = ovB - ls;
                int x0 = st->gutterW + (colX[ca] - st->leftChar - baseDc) * st->charW;
                int x1 = st->gutterW + (colX[cb] - st->leftChar - baseDc) * st->charW;
                HBRUSH shb = CreateSolidBrush(ColSelBg());
                RECT sr = {x0, y, x1, y + st->lineH};
                FillRect(dc, &sr, shb);
                DeleteObject(shb);
            }

            if (len > 0)
            {
                unsigned char in = (static_cast<int>(st->lineState.size()) > line) ? st->lineState[line] : 0;
                unsigned char out;
                TokenizeLine(st->lang, content, in, out, tok);
                for (const LineToken &t : tok)
                {
                    size_t ts = (std::max)(t.start, rowStartIdx);
                    size_t te = (std::min)(t.start + t.length, rowEndIdx);
                    if (ts >= te)
                        continue;
                    int x0 = st->gutterW + (colX[ts] - st->leftChar - baseDc) * st->charW;
                    if (x0 + st->charW <= st->gutterW)
                        continue;
                    SetTextColor(dc, ColTok(t.type));
                    TextOutW(dc, x0, y, content.c_str() + static_cast<ptrdiff_t>(ts),
                             static_cast<int>(te - ts));
                }
            }
        }

        if (st->focus && st->caretVisible)
        {
            int row = RowOfOffset(hwnd, st, st->caret);
            if (row >= st->topRow && row < st->topRow + vis)
            {
                int col = DispColAtOffset(st, st->caret);
                int baseDc = 0;
                if (st->wrap)
                    baseDc = LineColOfRow(hwnd, st, row).second;
                int x = st->gutterW + (col - st->leftChar - baseDc) * st->charW;
                int y = (row - st->topRow) * st->lineH;
                HBRUSH cb = CreateSolidBrush(ColCaret());
                RECT cr = {x, y, x + 2, y + st->lineH};
                FillRect(dc, &cr, cb);
                DeleteObject(cb);
            }
        }

        SelectObject(dc, oldFont);
        if (memDC)
            BlitBackBuffer(hdc, memDC, ps.rcPaint);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        if (GetState(hwnd))
        {
            UpdateScrollbars(hwnd, GetState(hwnd));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_SETFOCUS:
    {
        EditorState *st = GetState(hwnd);
        if (st)
        {
            st->focus = true;
            st->caretVisible = true;
            SetTimer(hwnd, 1, 500, nullptr);
            InvalidateCaret(hwnd, st);
        }
        return 0;
    }
    case WM_KILLFOCUS:
    {
        EditorState *st = GetState(hwnd);
        if (st)
        {
            st->focus = false;
            st->caretVisible = false;
            KillTimer(hwnd, 1);
            HideCompletion(hwnd, st);
            InvalidateCaret(hwnd, st);
        }
        return 0;
    }
    case WM_TIMER:
    {
        EditorState *st = GetState(hwnd);
        if (wParam == 1 && st)
        {
            st->caretVisible = !st->caretVisible;
            InvalidateCaret(hwnd, st);
        }
        if (wParam == 2 && st)
        {
            KillTimer(hwnd, 2);
            if (st->diagPopupArmed && !st->diagMsgs.empty() && g_state.codeCheck)
            {
                st->diagPopupArmed = false;
                st->lastPoppedDiags = st->diagMsgs;
                if (!g_ignoreCheck) // "force compile": suppress the check popup
                {
                    std::wstring dlgMsg;
                    size_t shownMax = 6;
                    for (size_t i = 0; i < st->diagMsgs.size() && i < shownMax; ++i)
                    {
                        if (i)
                            dlgMsg += L"\n";
                        dlgMsg += L"- " + st->diagMsgs[i];
                    }
                    if (st->diagMsgs.size() > shownMax)
                        dlgMsg += L"\n- ... (共 " + std::to_wstring(st->diagMsgs.size()) + L" 处问题)";
                    MessageBoxW(GetParent(hwnd), dlgMsg.c_str(), L"代码检查", MB_ICONWARNING | MB_OK);
                }
            }
        }
        return 0;
    }
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
        return TRUE;

    case WM_IME_COMPOSITION:
    {
        EditorState *st = GetState(hwnd);
        if (!st)
            break;
        // Insert the IME-committed text (Chinese/Japanese/Korean input).
        if (lParam & GCS_RESULTSTR)
        {
            HIMC himc = ImmGetContext(hwnd);
            if (himc)
            {
                DWORD len = ImmGetCompositionStringW(himc, GCS_RESULTSTR, nullptr, 0);
                if (len > 0)
                {
                    std::wstring s(len / sizeof(wchar_t), L'\0');
                    ImmGetCompositionStringW(himc, GCS_RESULTSTR, &s[0], len);
                    HideCompletion(hwnd, st);
                    DoInsert(hwnd, st, st->caret, st->caret, s);
                }
                ImmReleaseContext(hwnd, himc);
            }
            return 0;
        }
        if (lParam & GCS_COMPSTR)
            HideCompletion(hwnd, st);
        break;
    }
    case WM_IME_STARTCOMPOSITION:
        HideCompletion(hwnd, GetState(hwnd));
        break;

    case WM_CHAR:
    {
        EditorState *st = GetState(hwnd);
        if (!st)
            break;
        wchar_t c = static_cast<wchar_t>(wParam);
        if (st->suppressSpace && c == L' ')
        {
            st->suppressSpace = false;
            if (!st->hasSel)
                return 0;   // 自动配对后紧跟的那个空格要吞掉；有选区时仍按替换处理
        }
        if (c < 32)
            return 0; // editing controls are handled in WM_KEYDOWN
        bool idChar = IsIdentifierChar(c);
        if (st->compActive && !idChar)
            HideCompletion(hwnd, st);

        // 语法的强制补全：关键字后敲空格直接带出结构骨架（空格被吞掉）。
        if (c == L' ' && !st->hasSel && ExpandSyntaxBlock(hwnd, st))
            return 0;

        if (PairEnabled(st))
        {
            if (c == L'{' && StructBraceContext(st) && CanAutoPair(st, L'}'))
            {
                InsertStructBrace(hwnd, st);
                return 0;
            }
            if (c == L'(' || c == L'[' || c == L'{')
            {
                if (CanAutoPair(st, MatchingCloser(c)))
                {
                    InsertPair(hwnd, st, c);
                    return 0;
                }
            }
            else if (c == L'<')
            {
                if (AnglePairContext(st) && CanAutoPair(st, L'>'))
                {
                    InsertPair(hwnd, st, c);
                    return 0;
                }
            }
            else if (IsQuoteChar(c))
            {
                // 单双引号当括号：后面已经是同一个引号就跳过，否则补一个收尾引号。
                if (!st->hasSel && st->caret < st->text.size() &&
                    st->text[st->caret] == c)
                {
                    MoveCaretTo(hwnd, st, st->caret + 1, false);
                    return 0;
                }
                if (CanAutoPairQuote(st, c))
                {
                    InsertPair(hwnd, st, c);
                    return 0;
                }
            }
            else if (c == L')' || c == L']' || c == L'}' || c == L'>')
            {
                // Typing the closing character in front of its own partner
                // just steps over it instead of duplicating the pair.
                if (!st->hasSel && st->caret < st->text.size() &&
                    st->text[st->caret] == c)
                {
                    MoveCaretTo(hwnd, st, st->caret + 1, false);
                    return 0;
                }
            }
        }

        // 有选区时输入字符应当替换掉选中的部分（和记事本/所有编辑器一致），
        // 不能只在光标处插入、把选中的文字留在原地。
        size_t insA = st->caret;
        size_t insB = st->caret;
        if (st->hasSel)
        {
            insA = (std::min)(st->anchor, st->caret);
            insB = (std::max)(st->anchor, st->caret);
        }
        DoInsert(hwnd, st, insA, insB, std::wstring(1, c));
        if (idChar)
            TriggerCompletion(hwnd, st, false);
        return 0;
    }

    case WM_KEYDOWN:
    {
        EditorState *st = GetState(hwnd);
        if (!st)
            break;
        UINT vk = static_cast<UINT>(wParam);
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

        if (st->compActive)
        {
            if (vk == VK_UP)
            {
                if (st->compSel > 0)
                    --st->compSel;
                UpdateCompTop(st);
                if (st->popup)
                    InvalidateRect(st->popup, nullptr, TRUE);
                return 0;
            }
            if (vk == VK_DOWN)
            {
                int n = static_cast<int>(st->matches.size());
                if (st->compSel < n - 1)
                    ++st->compSel;
                UpdateCompTop(st);
                if (st->popup)
                    InvalidateRect(st->popup, nullptr, TRUE);
                return 0;
            }
            if (vk == VK_RETURN || vk == VK_TAB)
            {
                AcceptCompletion(hwnd, st, static_cast<size_t>(st->compSel));
                return 0;
            }
            if (vk == VK_ESCAPE)
            {
                HideCompletion(hwnd, st);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (ctrl && vk >= '1' && vk <= '9')
            {
                int idx = st->compTop + (vk - '1');
                if (idx >= 0 && idx < static_cast<int>(st->matches.size()))
                    AcceptCompletion(hwnd, st, static_cast<size_t>(idx));
                return 0;
            }
            HideCompletion(hwnd, st);
        }

        if (ctrl && vk == 'Z') { CE_Undo(hwnd); return 0; }
        if (ctrl && vk == 'Y') { CE_Redo(hwnd); return 0; }
        if (ctrl && vk == 'C') { DoCopy(hwnd, st); return 0; }
        if (ctrl && vk == 'X') { DoCut(hwnd, st); return 0; }
        if (ctrl && vk == 'V') { DoPaste(hwnd, st); return 0; }
        if (ctrl && vk == 'A') { CE_SelectAll(hwnd); return 0; }
        if (ctrl && vk == VK_SPACE)
        {
            st->suppressSpace = true;
            TriggerCompletion(hwnd, st, true);
            return 0;
        }

        if (alt)
            break;

        switch (vk)
        {
        case VK_LEFT:
            MoveCaretTo(hwnd, st, ctrl ? WordLeft(st, st->caret)
                                       : (st->caret > 0 ? st->caret - 1 : 0), shift);
            return 0;
        case VK_RIGHT:
            MoveCaretTo(hwnd, st, ctrl ? WordRight(st, st->caret)
                                       : (st->caret < st->text.size() ? st->caret + 1 : st->text.size()), shift);
            return 0;
        case VK_UP:
            MoveVertical(hwnd, st, -1, shift);
            return 0;
        case VK_DOWN:
            MoveVertical(hwnd, st, 1, shift);
            return 0;
        case VK_PRIOR:
            MoveVertical(hwnd, st, -VisibleRows(hwnd, st), shift);
            return 0;
        case VK_NEXT:
            MoveVertical(hwnd, st, VisibleRows(hwnd, st), shift);
            return 0;
        case VK_HOME:
            MoveCaretTo(hwnd, st, ctrl ? 0 : LineStart(st, LineOfOffset(st, st->caret)), shift);
            return 0;
        case VK_END:
        {
            int line = LineOfOffset(st, st->caret);
            MoveCaretTo(hwnd, st, ctrl ? st->text.size()
                                       : LineStart(st, line) + LineContentLen(st, line), shift);
            return 0;
        }
        case VK_BACK:
            if (ctrl)
            {
                if (st->hasSel)
                    DeleteSelection(hwnd, st);
                else
                    DoInsert(hwnd, st, WordLeft(st, st->caret), st->caret, L"");
            }
            else if (st->hasSel)
                DeleteSelection(hwnd, st);
            else if (st->caret > 0)
            {
                // An indentation run is a block of four spaces: backspace over
                // one of them removes the whole run down to the previous
                // four-column tab stop instead of leaving a ragged indent.
                size_t del = 1;
                if (st->text[st->caret - 1] == L' ')
                {
                    const int line = LineOfOffset(st, st->caret);
                    const size_t ls = LineStart(st, line);
                    bool indentOnly = true;
                    for (size_t i = ls; i < st->caret; ++i)
                    {
                        if (st->text[i] != L' ')
                        {
                            indentOnly = false;
                            break;
                        }
                    }
                    const int col = DispColAtOffset(st, st->caret);
                    const size_t step = static_cast<size_t>(col % 4 == 0 ? 4 : col % 4);
                    if (indentOnly && st->caret - ls >= step)
                        del = step;
                }
                DoInsert(hwnd, st, st->caret - del, st->caret, L"");
            }
            return 0;
        case VK_DELETE:
            if (ctrl)
            {
                if (st->hasSel)
                    DeleteSelection(hwnd, st);
                else
                    DoInsert(hwnd, st, st->caret, WordRight(st, st->caret), L"");
            }
            else if (st->hasSel)
                DeleteSelection(hwnd, st);
            else if (st->caret < st->text.size())
                DoInsert(hwnd, st, st->caret, st->caret + 1, L"");
            return 0;
        case VK_RETURN:
        {
            // Enter typed between an auto-paired "{" and "}" opens the body:
            // the brace pair is split into three lines with the caret landing
            // on the indented empty line in between.
            if (!st->hasSel && st->caret > 0 && st->caret < st->text.size() &&
                st->text[st->caret - 1] == L'{' && st->text[st->caret] == L'}')
            {
                const int line = LineOfOffset(st, st->caret);
                const std::wstring head = LineContent(st, line);
                size_t k = 0;
                while (k < head.size() && (head[k] == L' ' || head[k] == L'\t'))
                    ++k;
                const std::wstring indent = head.substr(0, k);
                const size_t at = st->caret;
                const std::wstring repl =
                    L"\n" + indent + L"    " + L"\n" + indent;
                DoInsert(hwnd, st, at, at, repl);
                st->caret = at + 1 + indent.size() + 4;
                st->anchor = st->caret;
                st->hasSel = false;
                EnsureCaretVisible(hwnd, st);
                NotifySelChange(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            int line = LineOfOffset(st, st->caret);
            std::wstring content = LineContent(st, line);
            size_t k = 0;
            while (k < content.size() && (content[k] == L' ' || content[k] == L'\t'))
                ++k;
            std::wstring repl = L"\n" + content.substr(0, k);
            if (st->hasSel)
                DoInsert(hwnd, st, (std::min)(st->anchor, st->caret),
                         (std::max)(st->anchor, st->caret), repl);
            else
                DoInsert(hwnd, st, st->caret, st->caret, repl);
            return 0;
        }
        case VK_TAB:
            if (st->hasSel)
            {
                DoInsert(hwnd, st, (std::min)(st->anchor, st->caret),
                         (std::max)(st->anchor, st->caret), L"    ");
            }
            else
            {
                int col = DispColAtOffset(st, st->caret);
                int tab = 4;
                int spaces = tab - (col % tab);
                DoInsert(hwnd, st, st->caret, st->caret, std::wstring(static_cast<size_t>(spaces), L' '));
            }
            return 0;
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        EditorState *st = GetState(hwnd);
        if (!st)
            break;
        HideCompletion(hwnd, st);
        SetFocus(hwnd);
        SetCapture(hwnd);
        st->dragging = true;
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        int row = st->topRow + y / (std::max)(1, st->lineH);
        int disp = (x - st->gutterW) / (std::max)(1, st->charW);
        if (disp < 0)
            disp = 0;
        size_t pos = OffsetAt(hwnd, st, row, disp);
        st->anchor = pos;
        st->caret = pos;
        st->hasSel = false;
        EnsureCaretVisible(hwnd, st);
        NotifySelChange(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        EditorState *st = GetState(hwnd);
        if (!st)
            break;
        if (st->dragging && (wParam & MK_LBUTTON))
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            int row = st->topRow + y / (std::max)(1, st->lineH);
            int disp = (x - st->gutterW) / (std::max)(1, st->charW);
            if (disp < 0)
                disp = 0;
            size_t pos = OffsetAt(hwnd, st, row, disp);
            if (pos != st->caret)
            {
                st->caret = pos;
                st->hasSel = (st->anchor != st->caret);
                EnsureCaretVisible(hwnd, st);
                NotifySelChange(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    }
    case WM_LBUTTONUP:
    {
        EditorState *st = GetState(hwnd);
        if (st)
        {
            st->dragging = false;
            ReleaseCapture();
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK:
    {
        EditorState *st = GetState(hwnd);
        if (!st)
            break;
        HideCompletion(hwnd, st);
        SetCapture(hwnd);
        st->dragging = true;
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        int row = st->topRow + y / (std::max)(1, st->lineH);
        int disp = (x - st->gutterW) / (std::max)(1, st->charW);
        if (disp < 0)
            disp = 0;
        size_t pos = OffsetAt(hwnd, st, row, disp);
        size_t a = pos, b = pos;
        while (a > 0 && IsIdentifierChar(st->text[a - 1]))
            --a;
        while (b < st->text.size() && IsIdentifierChar(st->text[b]))
            ++b;
        st->anchor = a;
        st->caret = b;
        st->hasSel = (a != b);
        NotifySelChange(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        EditorState *st = GetState(hwnd);
        if (!st)
            break;
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (GetKeyState(VK_CONTROL) & 0x8000)
        {
            SendMessageW(GetParent(hwnd), WM_COMMAND, delta > 0 ? IDM_VIEW_ZOOMIN : IDM_VIEW_ZOOMOUT, 0);
            return 0;
        }
        if (LOWORD(wParam) & MK_SHIFT)
        {
            if (!st->wrap)
            {
                st->leftChar += (delta > 0 ? -3 : 3);
                UpdateScrollbars(hwnd, st);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        UINT lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        if (lines == static_cast<UINT>(WHEEL_PAGESCROLL))
            lines = static_cast<UINT>(VisibleRows(hwnd, st));
        int step = (delta > 0 ? -static_cast<int>(lines) : static_cast<int>(lines));
        st->topRow += step;
        UpdateScrollbars(hwnd, st);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_VSCROLL:
    {
        EditorState *st = GetState(hwnd);
        if (!st)
            break;
        int vis = VisibleRows(hwnd, st);
        int total = TotalRows(hwnd, st);
        int maxRow = (std::max)(0, total - vis);
        switch (LOWORD(wParam))
        {
        case SB_LINEUP: st->topRow -= 1; break;
        case SB_LINEDOWN: st->topRow += 1; break;
        case SB_PAGEUP: st->topRow -= vis; break;
        case SB_PAGEDOWN: st->topRow += vis; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
        {
            SCROLLINFO si = {};
            si.cbSize = sizeof(si);
            si.fMask = SIF_TRACKPOS;
            GetScrollInfo(hwnd, SB_VERT, &si);
            st->topRow = si.nTrackPos;
            break;
        }
        case SB_TOP: st->topRow = 0; break;
        case SB_BOTTOM: st->topRow = maxRow; break;
        default: return 0;
        }
        if (st->topRow < 0)
            st->topRow = 0;
        if (st->topRow > maxRow)
            st->topRow = maxRow;
        UpdateScrollbars(hwnd, st);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_HSCROLL:
    {
        EditorState *st = GetState(hwnd);
        if (!st || st->wrap)
            return 0;
        int fc = FitChars(hwnd, st);
        int maxLen = MaxLineLen(st);
        int maxChar = (std::max)(0, maxLen - fc);
        switch (LOWORD(wParam))
        {
        case SB_LINELEFT: st->leftChar -= 1; break;
        case SB_LINERIGHT: st->leftChar += 1; break;
        case SB_PAGELEFT: st->leftChar -= fc; break;
        case SB_PAGERIGHT: st->leftChar += fc; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
        {
            SCROLLINFO si = {};
            si.cbSize = sizeof(si);
            si.fMask = SIF_TRACKPOS;
            GetScrollInfo(hwnd, SB_HORZ, &si);
            st->leftChar = si.nTrackPos;
            break;
        }
        case SB_LEFT: st->leftChar = 0; break;
        case SB_RIGHT: st->leftChar = maxChar; break;
        default: return 0;
        }
        if (st->leftChar < 0)
            st->leftChar = 0;
        if (st->leftChar > maxChar)
            st->leftChar = maxChar;
        UpdateScrollbars(hwnd, st);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    // RichEdit messages used elsewhere in the app (find/replace/goto dialogs).
    case EM_GETSEL:
    {
        EditorState *st = GetState(hwnd);
        if (!st)
            break;
        if (wParam)
            *reinterpret_cast<DWORD *>(wParam) = static_cast<DWORD>((std::min)(st->anchor, st->caret));
        if (lParam)
            *reinterpret_cast<DWORD *>(lParam) = static_cast<DWORD>((std::max)(st->anchor, st->caret));
        return 0;
    }
    case EM_SETSEL:
        CE_SetSelection(hwnd, static_cast<DWORD>(wParam), static_cast<DWORD>(lParam));
        return 0;
    case EM_REPLACESEL:
        CE_ReplaceSelection(hwnd, reinterpret_cast<const wchar_t *>(lParam));
        return 0;
    case EM_SCROLLCARET:
        CE_ScrollCaret(hwnd);
        return 0;
    case EM_EXLINEFROMCHAR:
        return CE_LineFromChar(hwnd, static_cast<DWORD>(lParam));
    case EM_LINEINDEX:
        return CE_LineIndex(hwnd, static_cast<int>(wParam));
    case EM_CANUNDO:
        return CE_CanUndo(hwnd) ? 1 : 0;
    case EM_UNDO:
        CE_Undo(hwnd);
        return 0;
    case EM_SETBKGNDCOLOR:
    case EM_SETCHARFORMAT:
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;

    case WM_CUT:
        DoCut(hwnd, GetState(hwnd));
        return 0;
    case WM_COPY:
        DoCopy(hwnd, GetState(hwnd));
        return 0;
    case WM_PASTE:
        DoPaste(hwnd, GetState(hwnd));
        return 0;
    case WM_CLEAR:
        DeleteSelection(hwnd, GetState(hwnd));
        return 0;

    case WM_GETTEXT:
    {
        EditorState *st = GetState(hwnd);
        if (!st)
            break;
        int max = static_cast<int>(wParam);
        if (max > 1 && lParam)
        {
            std::wstring s = st->text;
            if (static_cast<int>(s.size()) >= max)
                s.resize(max - 1);
            wcscpy_s(reinterpret_cast<wchar_t *>(lParam), max, s.c_str());
            return static_cast<int>(s.size());
        }
        return 0;
    }
    case WM_SETTEXT:
        CE_SetText(hwnd, reinterpret_cast<const wchar_t *>(lParam));
        return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* ------------------------------------------------------------------ */
/*  Registration & creation                                            */
/* ------------------------------------------------------------------ */

bool RegisterCodeEditorClasses(HINSTANCE hInstance)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.hInstance = hInstance;
    wc.lpfnWndProc = CodeEditorWndProc;
    wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    wc.lpszClassName = L"LegacyCodeEdit";
    if (!RegisterClassExW(&wc))
        return false;

    WNDCLASSEXW wcp = {};
    wcp.cbSize = sizeof(wcp);
    wcp.hInstance = hInstance;
    wcp.lpfnWndProc = CompletionWndProc;
    wcp.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcp.lpszClassName = kCompletionClass;
    if (!RegisterClassExW(&wcp))
        return false;
    return true;
}

HWND CreateCodeEditor(HWND parent, HMENU id, HINSTANCE hInstance)
{
    return CreateWindowExW(0, L"LegacyCodeEdit", nullptr,
                           WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL,
                           0, 0, 100, 100, parent, id, hInstance, nullptr);
}
