#include "symbols.h"
#include "syntax.h"
#include "presets.h"
#include <cwctype>
#include <algorithm>
#include <set>
#include <map>
#include <windows.h>

/* ------------------------------------------------------------------ */
/*  Small helpers                                                      */
/* ------------------------------------------------------------------ */

static bool InList(const wchar_t *const *list, const std::wstring &word)
{
    if (!list)
        return false;
    for (const wchar_t *const *p = list; p && *p; ++p)
        if (_wcsicmp(*p, word.c_str()) == 0)
            return true;
    return false;
}

static bool IsKeyword(const LanguageDef *L, const std::wstring &word)
{
    return L && InList(L->keywords, word);
}

static bool IsTypeName(const LanguageDef *L, const std::wstring &word)
{
    return L && InList(L->types, word);
}

/* ------------------------------------------------------------------ */
/*  Tokenizer for code lines (C-family and script-like languages)      */
/* ------------------------------------------------------------------ */

struct CTok
{
    std::wstring t;
    char k; // 'I' identifier, 'P' punct/op
};

static void TokenizeCodeLine(const LanguageDef *L, const std::wstring &line,
                             bool &inBlock, std::vector<CTok> &out)
{
    size_t i = 0, n = line.size();
    out.clear();
    while (i < n)
    {
        wchar_t c = line[i];
        // skip whitespace (never emit space tokens; they would break
        // prev-token based detection in the C-family scanner)
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\f' || c == L'\v')
        {
            ++i;
            continue;
        }
        // block comment
        if (inBlock)
        {
            if (L && L->blockStart && L->blockEnd && c == L->blockEnd[0] &&
                line.compare(i, 1, L->blockEnd) == 0 && L->blockEnd[1] == L'\0')
            {
                inBlock = false;
                ++i;
            }
            else if (L && L->blockEnd && line.compare(i, wcslen(L->blockEnd), L->blockEnd) == 0)
            {
                inBlock = false;
                i += wcslen(L->blockEnd);
            }
            else
                ++i;
            continue;
        }
        // line comment
        if (L && L->lineComment && line.compare(i, wcslen(L->lineComment), L->lineComment) == 0)
            break;
        // block comment start
        if (L && L->blockStart && line.compare(i, wcslen(L->blockStart), L->blockStart) == 0)
        {
            inBlock = true;
            i += wcslen(L->blockStart);
            continue;
        }
        // strings
        if ((L && L->stringDouble && c == L'"') || (L && L->stringSingle && c == L'\'') ||
            (L && L->stringBacktick && c == L'`'))
        {
            wchar_t q = c;
            ++i;
            while (i < n)
            {
                if (line[i] == L'\\')
                    i += 2;
                else if (line[i] == q)
                {
                    ++i;
                    break;
                }
                else
                    ++i;
            }
            continue;
        }
        if (IsIdentifierStart(c))
        {
            size_t j = i;
            while (j < n && IsIdentifierChar(line[j]))
                ++j;
            out.push_back({line.substr(i, j - i), 'I'});
            i = j;
            continue;
        }
        // operator / punct (keep 2-char ops as one token for '::' handling)
        if (i + 1 < n && ((c == L':' && line[i + 1] == L':') ||
                          (c == L'-' && line[i + 1] == L'>') ||
                          (c == L'<' && line[i + 1] == L'<') ||
                          (c == L'>' && line[i + 1] == L'>') ||
                          (c == L'=' && line[i + 1] == L'=') ||
                          (c == L'!' && line[i + 1] == L'=') ||
                          (c == L'&' && line[i + 1] == L'&') ||
                          (c == L'|' && line[i + 1] == L'|') ||
                          (c == L'+' && line[i + 1] == L'+') ||
                          (c == L'-' && line[i + 1] == L'-')))
        {
            out.push_back({line.substr(i, 2), 'P'});
            i += 2;
            continue;
        }
        out.push_back({std::wstring(1, c), 'P'});
        ++i;
    }
}

// A token that can appear as a type prefix in a C-style declaration:
// user-defined type / identifier, a known type name, or a primitive type
// keyword (int/void/char/... which live in the keyword list).
static bool IsTypeLikeToken(const LanguageDef *L, const CTok &t)
{
    if (t.k != 'I')
        return false;
    if (IsTypeName(L, t.t))
        return true;
    if (!IsKeyword(L, t.t))
        return true;
    static const wchar_t *const prim[] = {
        L"int", L"void", L"char", L"float", L"double", L"bool", L"long", L"short",
        L"signed", L"unsigned", L"wchar_t", L"char16_t", L"char32_t", L"char8_t",
        L"auto", L"const", L"constexpr", L"volatile", L"extern", L"register",
        L"struct", L"class", L"enum", L"union", L"typename", L"decltype",
        L"int8_t", L"int16_t", L"int32_t", L"int64_t", L"uint8_t", L"uint16_t",
        L"uint32_t", L"uint64_t", L"intptr_t", L"uintptr_t", L"ptrdiff_t", L"intmax_t",
        nullptr};
    return InList(prim, t.t);
}

/* ------------------------------------------------------------------ */
/*  C-family user symbol scanning + call collection                    */
/* ------------------------------------------------------------------ */

static bool IsCFamily(const wchar_t *id)
{
    static const wchar_t *const cfam[] = {
        L"cpp", L"c", L"cs", L"java", L"objc", L"go", L"rust",
        L"swift", L"kotlin", L"scala", L"groovy", L"vb", L"dart", L"php",
    };
    for (auto *f : cfam)
        if (_wcsicmp(f, id) == 0)
            return true;
    return false;
}

struct CallSite
{
    int line;
    std::wstring name;
};

static void ScanCFamily(const LanguageDef *L, const std::wstring &text,
                        std::vector<UserSym> &syms, std::vector<CallSite> &calls,
                        std::set<std::wstring> &includes, std::set<std::wstring> &defs)
{
    std::wstring langId = L ? L->id : L"";
    std::wstring fnKw; // function-definition keyword for go/rust/swift/kotlin/php
    if (langId == L"go") fnKw = L"func";
    else if (langId == L"rust") fnKw = L"fn";
    else if (langId == L"swift") fnKw = L"func";
    else if (langId == L"kotlin") fnKw = L"fun";
    else if (langId == L"php") fnKw = L"function";

    bool inBlock = false;
    size_t pos = 0, n = text.size();
    int lineNo [[maybe_unused]] = 0;
    while (pos <= n)
    {
        size_t e = text.find(L'\n', pos);
        if (e == std::wstring::npos)
            e = n;
        std::wstring line = text.substr(pos, e - pos);
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();
        bool preproc = L && L->preprocessor;

        // Preprocessor line: #include / #define
        if (preproc)
        {
            size_t first = line.find_first_not_of(L" \t");
            if (first != std::wstring::npos && line[first] == L'#')
            {
                size_t kw = line.find_first_not_of(L" \t", first + 1);
                if (kw != std::wstring::npos)
                {
                    std::wstring directive = line.substr(kw, line.find_first_of(L" \t", kw) - kw);
                    if (directive == L"include" && langId != L"cs" && langId != L"java")
                    {
                        size_t b = line.find_first_not_of(L" \t", kw + 7);
                        if (b != std::wstring::npos)
                        {
                            std::wstring h;
                            if (b < line.size() && (line[b] == L'<' || line[b] == L'"'))
                            {
                                wchar_t close = (line[b] == L'<') ? L'>' : L'"';
                                size_t c2 = line.find(close, b + 1);
                                if (c2 != std::wstring::npos)
                                    h = line.substr(b + 1, c2 - b - 1);
                                else
                                    h = line.substr(b + 1);
                            }
                            if (!h.empty())
                                includes.insert(h);
                        }
                    }
                    else if (directive == L"define")
                    {
                        size_t b = line.find_first_not_of(L" \t", kw + 6);
                        if (b != std::wstring::npos && b < line.size() && IsIdentifierStart(line[b]))
                        {
                            size_t j = b;
                            while (j < line.size() && IsIdentifierChar(line[j]))
                                ++j;
                            std::wstring m = line.substr(b, j - b);
                            syms.push_back({m, L'D'});
                            defs.insert(m);
                        }
                    }
                    else if (directive == L"import" || directive == L"include")
                    {
                        size_t b = line.find_first_not_of(L" \t", kw + 6);
                        if (b != std::wstring::npos && b < line.size() && IsIdentifierStart(line[b]))
                        {
                            size_t j = b;
                            while (j < line.size() && (IsIdentifierChar(line[j]) || line[j] == L'.' || line[j] == L'*'))
                                ++j;
                            std::wstring m = line.substr(b, j - b);
                            if (directive == L"import")
                                syms.push_back({m, L'M'});
                            else
                                includes.insert(m);
                        }
                    }
                }
                pos = e + 1;
                ++lineNo;
                continue;
            }
        }

        std::vector<CTok> toks;
        TokenizeCodeLine(L, line, inBlock, toks);

        // Java:  import a.b.C;   import static a.b.C;   import a.b.*;
        // Register the imported type's simple name as known so that using it
        // (e.g. `new Gson()`, `Environment.x = ...`) is not flagged unknown.
        if (langId == L"java" && !toks.empty() &&
            toks[0].k == 'I' && toks[0].t == L"import")
        {
            for (int j = static_cast<int>(toks.size()) - 1; j >= 0; --j)
            {
                if (toks[j].k == 'P' || toks[j].t == L"import" || toks[j].t == L"static" ||
                    toks[j].t == L"*")
                    continue;
                // last identifier segment = the imported class/type name
                defs.insert(toks[j].t);
                syms.push_back({toks[j].t, L'T'});
                break;
            }
            pos = e + 1;
            ++lineNo;
            continue;
        }

        bool stmtStart [[maybe_unused]] = true;
        int parenDepth = 0;
        bool angleOpen = false;

        for (int i = 0; i < static_cast<int>(toks.size()); ++i)
        {
            CTok &tk = toks[i];
            if (tk.k == 'P')
            {
                const std::wstring &t = tk.t;
                if (t == L"(")
                {
                    ++parenDepth;
                    stmtStart = false;
                }
                else if (t == L")")
                {
                    if (parenDepth > 0)
                        --parenDepth;
                    stmtStart = false;
                }
                else if (t == L"{")
                    stmtStart = true;
                else if (t == L";")
                {
                    stmtStart = true;
                    angleOpen = false;
                }
                else if (t == L"<" && parenDepth == 0)
                    angleOpen = true;
                else if (t == L">" && parenDepth == 0 && angleOpen)
                {
                    stmtStart = true;
                    angleOpen = false;
                }
                else if (t == L",")
                {
                    // keep stmtStart as-is
                }
                else
                    stmtStart = false;
                continue;
            }

            // tk.k == 'I'
            const std::wstring &w = tk.t;
            const CTok *next = (i + 1 < static_cast<int>(toks.size())) ? &toks[i + 1] : nullptr;
            const CTok *prev = (i > 0) ? &toks[i - 1] : nullptr;
            bool nextParen = next && next->k == 'P' && next->t == L"(";
            bool nextDecl = next && next->k == 'P' &&
                            (next->t == L";" || next->t == L"," || next->t == L"=" || next->t == L"[");

            // function-definition keywords (go/rust/swift/kotlin, or script def)
            if (!fnKw.empty() && prev && prev->k == 'I' && prev->t == fnKw && nextParen)
            {
                syms.push_back({w, L'F'});
                defs.insert(w);
                stmtStart = false;
                continue;
            }
            if (langId == L"js" || langId == L"ts")
            {
                if (prev && prev->k == 'I' && prev->t == L"function" && nextParen)
                {
                    syms.push_back({w, L'F'});
                    defs.insert(w);
                    stmtStart = false;
                    continue;
                }
                if (prev && prev->k == 'I' && prev->t == L"class")
                {
                    syms.push_back({w, L'C'});
                    stmtStart = false;
                    continue;
                }
                if (prev && prev->k == 'I' && (prev->t == L"const" || prev->t == L"let" || prev->t == L"var") && nextDecl)
                {
                    syms.push_back({w, L'V'});
                    stmtStart = false;
                    continue;
                }
            }
            if (langId == L"lua")
            {
                if (prev && prev->k == 'I' && (prev->t == L"function" || prev->t == L"local") && nextParen)
                {
                    syms.push_back({w, L'F'});
                    defs.insert(w);
                    stmtStart = false;
                    continue;
                }
            }
            if (langId == L"ruby" || langId == L"python")
            {
                if (prev && prev->k == 'I' && (prev->t == L"def" || prev->t == L"class") && nextParen)
                {
                    syms.push_back({w, (prev->t == L"class") ? L'C' : L'F'});
                    defs.insert(w);
                    stmtStart = false;
                    continue;
                }
            }

            // class/struct/enum/typedef declaration:  keyword then name
            if (prev && prev->k == 'I' && !IsKeyword(L, w))
            {
                wchar_t tag = 0;
                if (prev->t == L"class") tag = L'C';
                else if (prev->t == L"struct" || prev->t == L"union") tag = L'S';
                else if (prev->t == L"enum") tag = L'E';
                else if (prev->t == L"typedef" || prev->t == L"using") tag = L'T';
                if (tag)
                {
                    syms.push_back({w, tag});
                    stmtStart = false;
                    continue;
                }
            }

            if (nextParen)
            {
                // function definition:  <type-like> name (
                bool defLike = false;
                if (!IsKeyword(L, w) && parenDepth == 0)
                {
                    if (prev && prev->k == 'I')
                        defLike = IsTypeLikeToken(L, *prev);
                    else if (prev && prev->k == 'P' && (prev->t == L"*" || prev->t == L"&") && i >= 2)
                        defLike = IsTypeLikeToken(L, toks[i - 2]);
                    else if (prev && prev->k == 'P' && prev->t == L">")
                        defLike = true; // std::vector<int> foo(...)
                }
                if (defLike)
                {
                    syms.push_back({w, L'F'});
                    defs.insert(w);
                    stmtStart = false;
                    continue;
                }
                // function call
                if (!IsKeyword(L, w))
                {
                    const CTok *pp = prev;
                    bool member = pp && pp->k == 'P' && (pp->t == L"." || pp->t == L"->");
                    // constructor:  new Type( ... )  — a type, not an unknown call
                    bool isCtor = pp && pp->k == 'I' && pp->t == L"new";
                    if (!member && !isCtor)
                    {
                        std::wstring name = w;
                        // scope chain:  Ident :: Ident ( ... )
                        if (pp && pp->k == 'P' && pp->t == L"::")
                        {
                            int k = i - 2;
                            while (k >= 0 && toks[k].k == 'I')
                            {
                                name = toks[k].t + L"::" + name;
                                if (k - 1 >= 0 && toks[k - 1].k == 'P' && toks[k - 1].t == L"::")
                                    k -= 2;
                                else
                                    break;
                            }
                        }
                        calls.push_back({lineNo, name});
                    }
                }
                stmtStart = false;
                continue;
            }

            if (nextDecl && parenDepth == 0)
            {
                bool prevOk = true;
                if (prev && prev->k == 'I' && IsKeyword(L, prev->t) &&
                    !IsTypeLikeToken(L, *prev))
                    prevOk = false;
                if (prev && prev->k == 'P' &&
                    (prev->t == L")" || prev->t == L"]" || prev->t == L"}" || prev->t == L";"))
                    prevOk = false;
                if (prev && prev->k == 'I' &&
                    (prev->t == L"return" || prev->t == L"case" || prev->t == L"break" ||
                     prev->t == L"continue" || prev->t == L"namespace" || prev->t == L"using" ||
                     prev->t == L"import" || prev->t == L"include" || prev->t == L"new" ||
                     prev->t == L"delete" || prev->t == L"sizeof" || prev->t == L"throw" ||
                     prev->t == L"goto"))
                    prevOk = false;
                if (prevOk && !IsKeyword(L, w))
                {
                    syms.push_back({w, L'V'});
                    stmtStart = false;
                    continue;
                }
            }

            stmtStart = false;
        }

        pos = e + 1;
        ++lineNo;
    }
}

/* ------------------------------------------------------------------ */
/*  Header tracing: #include "xxx.h"  and  #include <xxx.h>            */
/*  Reads the user's own headers AND the system (toolchain) headers    */
/*  they include, extracts the symbols they declare, and merges them   */
/*  into the known-symbol set so that calls to those names are not     */
/*  flagged "unknown".  Local-header symbols also feed completion;     */
/*  system-header symbols are used for diagnostics only (their sheer   */
/*  volume would swamp the completion list, and common std functions   */
/*  already come from the preset table).                               */
/*  Nested includes are followed recursively (cycle-guarded, with      */
/*  depth / file-count limits).  Results are cached per file keyed by  */
/*  last-write time, so re-scanning on each keystroke is a cheap       */
/*  cache lookup.                                                      */
/* ------------------------------------------------------------------ */

enum { SRC_USER = 0, SRC_SYSTEM = 1 };

static std::wstring FileDirOf(const std::wstring &path)
{
    size_t s = path.find_last_of(L"\\/");
    return (s == std::wstring::npos) ? std::wstring() : path.substr(0, s + 1);
}

static std::wstring LowerCaseW(const std::wstring &s)
{
    std::wstring r = s;
    for (auto &c : r)
        c = static_cast<wchar_t>(towlower(static_cast<wint_t>(c)));
    return r;
}

// Collect include names, split into quoted (local) and angle-bracket (system).
static void CollectIncludes(const std::wstring &text, std::vector<std::wstring> &local,
                            std::vector<std::wstring> &sys)
{
    size_t pos = 0, n = text.size();
    while (pos <= n)
    {
        size_t e = text.find(L'\n', pos);
        if (e == std::wstring::npos)
            e = n;
        std::wstring line = text.substr(pos, e - pos);
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();
        size_t first = line.find_first_not_of(L" \t");
        if (first != std::wstring::npos && line[first] == L'#')
        {
            size_t kw = line.find_first_not_of(L" \t", first + 1);
            if (kw != std::wstring::npos)
            {
                size_t kend = line.find_first_of(L" \t", kw);
                std::wstring directive = line.substr(kw, (kend == std::wstring::npos) ? line.size() - kw : kend - kw);
                if (directive == L"include")
                {
                    size_t b = line.find_first_not_of(L" \t", kw + 7);
                    if (b != std::wstring::npos && b < line.size() && (line[b] == L'"' || line[b] == L'<'))
                    {
                        wchar_t close = (line[b] == L'"') ? L'"' : L'>';
                        size_t c2 = line.find(close, b + 1);
                        if (c2 != std::wstring::npos && c2 > b + 1)
                        {
                            std::wstring h = line.substr(b + 1, c2 - b - 1);
                            if (line[b] == L'"')
                                local.push_back(h);
                            else
                                sys.push_back(h);
                        }
                    }
                }
            }
        }
        pos = e + 1;
    }
}

// Read a header file into a wide string (UTF-8 / UTF-16 / ANSI tolerant).
static bool ReadTextFile(const std::wstring &path, std::wstring &out)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD size = GetFileSize(h, nullptr);
    std::vector<BYTE> buf(size ? size : 1);
    DWORD read = 0;
    ReadFile(h, buf.data(), size, &read, nullptr);
    CloseHandle(h);
    buf.resize(read);

    if (read >= 2 && buf[0] == 0xFF && buf[1] == 0xFE)
    { // UTF-16LE BOM
        size_t wn = (read - 2) / 2;
        out.assign(reinterpret_cast<wchar_t *>(buf.data() + 2), wn);
        return true;
    }
    size_t off = 0;
    if (read >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF)
        off = 3; // UTF-8 BOM
    int wn = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char *>(buf.data()) + off,
                                 static_cast<int>(read - off), nullptr, 0);
    if (wn > 0)
    {
        out.resize(wn);
        MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char *>(buf.data()) + off,
                            static_cast<int>(read - off), &out[0], wn);
        return true;
    }
    wn = MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char *>(buf.data()) + off,
                             static_cast<int>(read - off), nullptr, 0);
    if (wn > 0)
    {
        out.resize(wn);
        MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char *>(buf.data()) + off,
                            static_cast<int>(read - off), &out[0], wn);
        return true;
    }
    return false;
}

static unsigned __int64 FileFingerprint(const std::wstring &path)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    BY_HANDLE_FILE_INFORMATION info = {};
    if (!GetFileInformationByHandle(h, &info))
    {
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    return (static_cast<unsigned __int64>(info.ftLastWriteTime.dwHighDateTime) << 32) |
           info.ftLastWriteTime.dwLowDateTime;
}

struct NestedInfo
{
    std::wstring path; // resolved full path
    bool sys;          // true when part of a system-header tree
};
struct HeaderCacheEntry
{
    unsigned __int64 fp = 0;
    std::vector<UserSym> syms;         // symbols declared in this header's own body
    std::vector<NestedInfo> nested;    // its own includes, resolved to full paths
};
static std::map<std::wstring, HeaderCacheEntry> g_headerCache;

static void AddUniqueDir(std::vector<std::wstring> &dirs, const std::wstring &d)
{
    if (d.empty())
        return;
    std::wstring dir = d;
    if (dir.back() != L'\\')
        dir.push_back(L'\\');
    DWORD attr = GetFileAttributesW(dir.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
        return;
    std::wstring key = LowerCaseW(dir);
    for (const auto &x : dirs)
        if (LowerCaseW(x) == key)
            return;
    dirs.push_back(dir);
}

// Locate an executable by name on PATH.
static std::wstring FindOnPath(const wchar_t *name)
{
    wchar_t buf[4096];
    DWORD r = SearchPathW(nullptr, name, nullptr, 4096, buf, nullptr);
    if (r == 0 || r >= 4096)
        return std::wstring();
    return std::wstring(buf, r);
}

// Ask a real compiler (gcc / clang / MinGW …) which directories it searches
// for includes, by running "-E -v -" with empty input and parsing the
// "#include <...> search starts here:" block it prints.
static void RunCompilerIncludeProbe(const std::wstring &compiler, std::vector<std::wstring> &dirs)
{
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0))
        return;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hWrite;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = compiler + L" -E -v -";
    std::vector<wchar_t> cmdline(cmd.begin(), cmd.end());
    cmdline.push_back(0);
    if (!CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi))
    {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return;
    }
    CloseHandle(hWrite); // let the child's stdin see EOF immediately

    std::string out;
    char buf[4096];
    DWORD got;
    while (ReadFile(hRead, buf, sizeof(buf), &got, nullptr) && got > 0)
        out.append(buf, got);
    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    size_t b = out.find("search starts here:");
    if (b == std::string::npos)
        return;
    b += 19; // length of "search starts here:"
    size_t e = out.find("End of search list.", b);
    std::string block = (e == std::string::npos) ? out.substr(b) : out.substr(b, e - b);
    size_t p = 0;
    while (p <= block.size())
    {
        size_t q = block.find('\n', p);
        if (q == std::string::npos)
            q = block.size();
        std::string line = block.substr(p, q - p);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        size_t s = line.find_first_not_of(" \t");
        if (s != std::string::npos && line[s] != '-')
        {
            std::wstring w;
            int wn = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), nullptr, 0);
            if (wn > 0)
            {
                w.resize(wn);
                MultiByteToWideChar(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), &w[0], wn);
            }
            AddUniqueDir(dirs, w);
        }
        if (q >= block.size())
            break;
        p = q + 1;
    }
}

// Probe non-MSVC toolchains (MinGW / clang / TDM-GCC …): compilers already on
// PATH plus the usual install locations, asking the real compiler for its
// include search path.
static void ProbeNonMsvcToolchains(std::vector<std::wstring> &dirs)
{
    const wchar_t *names[] = { L"gcc.exe", L"g++.exe", L"clang.exe", L"clang++.exe",
                               L"cc.exe", L"tcc.exe" };
    for (auto *n : names)
    {
        std::wstring full = FindOnPath(n);
        if (!full.empty())
        {
            RunCompilerIncludeProbe(full, dirs);
            break; // one working compiler's include set is enough
        }
    }
    const wchar_t *roots[] = { L"C:\\mingw", L"C:\\MinGW", L"C:\\TDM-GCC-64",
                               L"C:\\msys64\\mingw64", L"C:\\msys64\\mingw32",
                               L"D:\\mingw64", L"D:\\MinGW" };
    for (auto *r : roots)
    {
        std::wstring gcc = std::wstring(r) + L"\\bin\\gcc.exe";
        if (GetFileAttributesW(gcc.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            RunCompilerIncludeProbe(gcc, dirs);
            break;
        }
    }
}

// Discover the compiler toolchain's include directories: the INCLUDE
// environment variable (set by the MSVC toolchain), a best-effort probe of
// common VS / Windows-Kit install locations, and any non-MSV C compiler
// (MinGW / clang / …) found on the machine.
static void ProbeIncludeDirs(std::vector<std::wstring> &dirs)
{
    wchar_t buf[16384];
    DWORD n = GetEnvironmentVariableW(L"INCLUDE", buf, 16384);
    if (n > 0 && n < 16384)
    {
        std::wstring s(buf, n);
        size_t p = 0;
        while (p <= s.size())
        {
            size_t q = s.find(L';', p);
            if (q == std::wstring::npos)
                q = s.size();
            if (q > p)
                AddUniqueDir(dirs, s.substr(p, q - p));
            if (q >= s.size())
                break;
            p = q + 1;
        }
    }

    WIN32_FIND_DATAW fd;
    const wchar_t *roots[] = { L"C:\\Program Files\\Microsoft Visual Studio",
                               L"C:\\Program Files (x86)\\Microsoft Visual Studio" };
    for (auto *root : roots)
    {
        for (const wchar_t *yy : { L"2019", L"2022" })
        {
            std::wstring base = std::wstring(root) + L"\\" + yy;
            HANDLE h = FindFirstFileW((base + L"\\*").c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE)
                continue;
            do
            {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                    wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                    continue;
                std::wstring msvc = base + L"\\" + fd.cFileName + L"\\VC\\Tools\\MSVC";
                HANDLE h2 = FindFirstFileW((msvc + L"\\*").c_str(), &fd);
                if (h2 == INVALID_HANDLE_VALUE)
                    continue;
                do
                {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY &&
                        wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L".."))
                        AddUniqueDir(dirs, msvc + L"\\" + fd.cFileName + L"\\include");
                } while (FindNextFileW(h2, &fd));
                FindClose(h2);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }

    const wchar_t *kitRoots[] = { L"C:\\Program Files (x86)\\Windows Kits\\10\\Include",
                                  L"C:\\Program Files\\Windows Kits\\10\\Include" };
    for (auto *kr : kitRoots)
    {
        HANDLE h = FindFirstFileW((std::wstring(kr) + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        do
        {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;
            // windows.h and the Win32 API live in the SDK's um/ and shared/,
            // while C runtime lives in ucrt/. winrt/ holds C++/WinRT headers.
            for (const wchar_t *sub : { L"um", L"shared", L"ucrt", L"winrt", L"cppwinrt" })
            {
                std::wstring d = std::wstring(kr) + L"\\" + fd.cFileName + L"\\" + sub;
                if (GetFileAttributesW(d.c_str()) != INVALID_FILE_ATTRIBUTES)
                    AddUniqueDir(dirs, d);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    ProbeNonMsvcToolchains(dirs);
}

// Lazily-probed, immutable include-dir list for this session.
static const std::vector<std::wstring> &SystemIncludeDirs()
{
    static std::vector<std::wstring> dirs;
    if (dirs.empty())
        ProbeIncludeDirs(dirs);
    return dirs;
}

// Resolve an include name to a full path. Local names are resolved against
// the current file's directory; system names are searched in the include dirs.
static bool ResolveInclude(const std::wstring &name, bool sys, const std::wstring &curDir,
                           const std::vector<std::wstring> &includeDirs, std::wstring &out)
{
    if (sys)
    {
        for (const auto &d : includeDirs)
        {
            std::wstring p = d + name;
            if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                out = p;
                return true;
            }
        }
        return false;
    }
    std::wstring p = curDir + name;
    if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        out = p;
        return true;
    }
    return false;
}

static void TraceHeaderByPath(const std::wstring &path, bool sys, const wchar_t *langId,
                              const std::vector<std::wstring> &includeDirs,
                              std::set<std::wstring> &visited, int depth, int &fileCount,
                              unsigned __int64 deadline, std::vector<UserSym> &out)
{
    if (depth >= 24 || fileCount >= 300 || GetTickCount64() > deadline)
        return;
    std::wstring key = LowerCaseW(path);
    if (visited.count(key))
        return;
    visited.insert(key);

    unsigned __int64 fp = FileFingerprint(path);
    if (fp == 0)
        return;
    ++fileCount;

    auto it = g_headerCache.find(key);
    if (it == g_headerCache.end() || it->second.fp != fp)
    {
        std::wstring content;
        if (!ReadTextFile(path, content))
            return;
        const LanguageDef *L = LanguageById(langId);
        std::vector<UserSym> raw;
        std::vector<CallSite> calls;
        std::set<std::wstring> inc, defs;
        ScanCFamily(L, content, raw, calls, inc, defs);

        // System headers declare the Win32 / SDK API with calling-convention
        // macros (e.g. `WINUSERAPI int WINAPI MessageBoxW(...)`); the scanner
        // reads those names as *calls*.  Since system symbols only widen the
        // "defined" set (they never enter completion), promote every call-like
        // name in a system header to a declared function so the API is known.
        if (sys)
        {
            for (const CallSite &cs : calls)
            {
                if (!cs.name.empty() && !IsKeyword(L, cs.name))
                    raw.push_back({cs.name, L'F'});
            }
        }

        std::vector<NestedInfo> nested;
        std::vector<std::wstring> local, sname;
        CollectIncludes(content, local, sname);
        std::wstring dir = FileDirOf(path);
        for (const auto &h : local)
        {
            std::wstring p;
            if (ResolveInclude(h, false, dir, includeDirs, p))
                nested.push_back({p, sys});
        }
        for (const auto &h : sname)
        {
            std::wstring p;
            if (ResolveInclude(h, true, dir, includeDirs, p))
                nested.push_back({p, true});
        }

        if (g_headerCache.size() > 2048)
            g_headerCache.clear();
        g_headerCache[key] = {fp, std::move(raw), std::move(nested)};
        it = g_headerCache.find(key);
    }

    unsigned char src = sys ? static_cast<unsigned char>(SRC_SYSTEM)
                            : static_cast<unsigned char>(SRC_USER);
    for (const UserSym &s : it->second.syms)
    {
        UserSym t = s;
        t.src = src;
        out.push_back(t);
    }
    for (const auto &np : it->second.nested)
        TraceHeaderByPath(np.path, np.sys, langId, includeDirs, visited, depth + 1, fileCount, deadline, out);
}

// Aggregate cache: the full set of include-traced symbols for a document,
// keyed by the document path and invalidated when the set of resolved
// include roots (path + mtime) changes. Re-scanning on every keystroke would
// otherwise re-read the entire header tree (windows.h alone is hundreds of
// files); with this cache a keystroke is a cheap signature check.
struct TraceAggregate
{
    std::wstring sig;
    std::vector<UserSym> syms;
};
static std::map<std::wstring, TraceAggregate> g_traceAgg;

// Entry point: follow every #include of the current document — both the
// user's local "xxx.h" headers and the toolchain's <xxx> headers — and
// merge the declared symbols into out. Only meaningful for C-family.
static void TraceIncludes(const std::wstring &text, const wchar_t *langId,
                          const std::wstring &filePath, std::vector<UserSym> &out)
{
    if (filePath.empty() || !SupportsDiagnostics(langId))
        return;
    std::vector<std::wstring> local, sname;
    CollectIncludes(text, local, sname);
    if (local.empty() && sname.empty())
        return;
    const std::vector<std::wstring> &dirs = SystemIncludeDirs();
    std::wstring dir = FileDirOf(filePath);

    // Resolve the direct include roots once and build a signature of them.
    struct Root { std::wstring path; bool sys; };
    std::vector<Root> roots;
    std::wstring sig;
    for (const auto &h : local)
    {
        std::wstring p;
        if (ResolveInclude(h, false, dir, dirs, p))
        {
            roots.push_back({p, false});
            sig += L'L'; sig += LowerCaseW(p); sig += L'#';
            sig += std::to_wstring(FileFingerprint(p)); sig += L';';
        }
    }
    for (const auto &h : sname)
    {
        std::wstring p;
        if (ResolveInclude(h, true, dir, dirs, p))
        {
            roots.push_back({p, true});
            sig += L'S'; sig += LowerCaseW(p); sig += L'#';
            sig += std::to_wstring(FileFingerprint(p)); sig += L';';
        }
    }

    std::wstring aggKey = LowerCaseW(filePath);
    auto ai = g_traceAgg.find(aggKey);
    if (ai != g_traceAgg.end() && ai->second.sig == sig)
    {
        out.insert(out.end(), ai->second.syms.begin(), ai->second.syms.end());
        return;
    }

    std::vector<UserSym> traced;
    std::set<std::wstring> visited;
    int fileCount = 0;
    unsigned __int64 deadline = GetTickCount64() + 1200; // cap first-scan cost
    for (const auto &r : roots)
        TraceHeaderByPath(r.path, r.sys, langId, dirs, visited, 0, fileCount, deadline, traced);

    if (g_traceAgg.size() > 32)
        g_traceAgg.clear();
    g_traceAgg[aggKey] = {sig, traced};
    out.insert(out.end(), traced.begin(), traced.end());
}

/* ------------------------------------------------------------------ */
/*  Script-language scans (python / ruby / lua / js)                   */
/* ------------------------------------------------------------------ */

static void ScanScript(const wchar_t *langId, const std::wstring &text,
                       std::vector<UserSym> &syms)
{
    size_t pos = 0, n = text.size();
    int lineNo [[maybe_unused]] = 0;
    while (pos <= n)
    {
        size_t e = text.find(L'\n', pos);
        if (e == std::wstring::npos)
            e = n;
        std::wstring line = text.substr(pos, e - pos);
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();
        size_t first = line.find_first_not_of(L" \t");
        if (first != std::wstring::npos)
        {
            std::wstring s = line.substr(first);
            if (_wcsicmp(langId, L"py") == 0)
            {
                if (s.compare(0, 4, L"def ") == 0)
                {
                    size_t b = 4;
                    while (b < s.size() && s[b] == L' ')
                        ++b;
                    size_t j = b;
                    while (j < s.size() && IsIdentifierChar(s[j]))
                        ++j;
                    if (j > b)
                        syms.push_back({s.substr(b, j - b), L'F'});
                }
                else if (s.compare(0, 6, L"class ") == 0)
                {
                    size_t b = 6;
                    while (b < s.size() && s[b] == L' ')
                        ++b;
                    size_t j = b;
                    while (j < s.size() && IsIdentifierChar(s[j]))
                        ++j;
                    if (j > b)
                        syms.push_back({s.substr(b, j - b), L'C'});
                }
                else
                {
                    size_t eq = s.find(L'=');
                    size_t comma = s.find(L',');
                    size_t stop = (std::min)((eq == std::wstring::npos) ? s.size() : eq,
                                             (comma == std::wstring::npos) ? s.size() : comma);
                    size_t b = 0;
                    while (b < stop && (s[b] == L' ' || s[b] == L'\t'))
                        ++b;
                    size_t j = b;
                    while (j < stop && IsIdentifierChar(s[j]))
                        ++j;
                    if (j > b && j - b >= 2)
                    {
                        std::wstring w = s.substr(b, j - b);
                        static const std::set<std::wstring> pyKw = {
                            L"import", L"from", L"if", L"elif", L"else", L"for", L"while",
                            L"return", L"def", L"class", L"with", L"try", L"except", L"finally",
                            L"raise", L"pass", L"break", L"continue", L"lambda", L"global",
                            L"nonlocal", L"yield", L"assert", L"del", L"in", L"not", L"and",
                            L"or", L"is", L"None", L"True", L"False", L"print", L"len", L"type",
                        };
                        if (eq != std::wstring::npos && !pyKw.count(w))
                            syms.push_back({w, L'V'});
                    }
                }
            }
            else if (_wcsicmp(langId, L"js") == 0 || _wcsicmp(langId, L"ts") == 0)
            {
                // handled inside ScanCFamily for js/ts
            }
            else if (_wcsicmp(langId, L"rb") == 0)
            {
                if (s.compare(0, 4, L"def ") == 0)
                {
                    size_t b = 4;
                    while (b < s.size() && s[b] == L' ')
                        ++b;
                    size_t j = b;
                    while (j < s.size() && IsIdentifierChar(s[j]))
                        ++j;
                    if (j > b)
                        syms.push_back({s.substr(b, j - b), L'F'});
                }
                else if (s.compare(0, 6, L"class ") == 0)
                {
                    size_t b = 6;
                    size_t j = b;
                    while (j < s.size() && IsIdentifierChar(s[j]))
                        ++j;
                    if (j > b)
                        syms.push_back({s.substr(b, j - b), L'C'});
                }
                else
                {
                    size_t eq = s.find(L'=');
                    if (eq != std::wstring::npos)
                    {
                        size_t b = 0;
                        while (b < s.size() && (s[b] == L' ' || s[b] == L'\t'))
                            ++b;
                        size_t j = b;
                        while (j < eq && IsIdentifierChar(s[j]))
                            ++j;
                        if (j > b)
                            syms.push_back({s.substr(b, j - b), L'V'});
                    }
                }
            }
            else if (_wcsicmp(langId, L"lua") == 0)
            {
                size_t eq = s.find(L'=');
                if (eq != std::wstring::npos && eq > 0)
                {
                    size_t b = 0;
                    while (b < s.size() && (s[b] == L' ' || s[b] == L'\t'))
                        ++b;
                    size_t j = b;
                    while (j < eq && IsIdentifierChar(s[j]))
                        ++j;
                    if (j > b)
                        syms.push_back({s.substr(b, j - b), L'V'});
                }
            }
        }
        pos = e + 1;
        ++lineNo;
    }
}

/* ------------------------------------------------------------------ */
/*  Diagnostics construction (shared core)                             */
/* ------------------------------------------------------------------ */

bool SupportsDiagnostics(const wchar_t *langId)
{
    if (!langId)
        return false;
    return _wcsicmp(langId, L"cpp") == 0 || _wcsicmp(langId, L"c") == 0 ||
           _wcsicmp(langId, L"cs") == 0 || _wcsicmp(langId, L"java") == 0 ||
           _wcsicmp(langId, L"objc") == 0;
}

// Finds a preset entry by name. The second pass accepts the unqualified form
// of std:: names (stoull, sort, sqrt, ...), which is what code written with
// "using namespace std;" actually calls.
static const PresetFunc *FindPreset(const PresetFunc *presets, size_t count, const std::wstring &name)
{
    if (!presets)
        return nullptr;
    for (size_t i = 0; i < count; ++i)
    {
        if (_wcsicmp(presets[i].name, name.c_str()) == 0)
            return &presets[i];
    }
    for (size_t i = 0; i < count; ++i)
    {
        const wchar_t *p = presets[i].name;
        if (wcsncmp(p, L"std::", 5) == 0 && _wcsicmp(p + 5, name.c_str()) == 0)
            return &presets[i];
    }
    return nullptr;
}

// Headers that pull <string> in on every mainstream C++ stdlib: asking the
// user for #include <string> when <iostream> is already there is pure noise.
static bool HeaderImpliedByCpp(const std::set<std::wstring> &includes, const std::wstring &h)
{
    if (_wcsicmp(h.c_str(), L"string") != 0)
        return false;
    static const wchar_t *hosts[] = {
        L"iostream", L"sstream", L"fstream", L"iomanip",
        L"stdexcept", L"string_view", L"filesystem",
    };
    for (const wchar_t *x : hosts)
        if (includes.count(x) > 0)
            return true;
    return false;
}

static void BuildDiags(const LanguageDef *L, const wchar_t *langId,
                       const std::vector<CallSite> &calls,
                       const std::set<std::wstring> &includes,
                       const std::set<std::wstring> &defs,
                       const std::vector<UserSym> &syms,
                       std::vector<Diag> &out)
{
    out.clear();
    if (!SupportsDiagnostics(langId))
        return;

    // known user-defined names (functions + macros) + always-ok names
    std::set<std::wstring> defined;
    for (const UserSym &s : syms)
        if (s.tag == L'F' || s.tag == L'D' || s.tag == L'C' || s.tag == L'S')
            defined.insert(s.name);
    for (const auto &d : defs)
        defined.insert(d);
    static const std::set<std::wstring> alwaysOk = {
        L"main", L"min", L"max", L"endl", L"cin", L"cout", L"cerr", L"clog",
        L"true", L"false", L"null", L"NULL", L"nullptr",
    };

    // <bits/stdc++.h> (GCC's umbrella header) pulls in the whole standard
    // library at once, so every "you need #include <xxx>" suggestion derived
    // from the preset table would be a false positive there.
    const bool universalInclude = includes.count(L"bits/stdc++.h") > 0 ||
                                  includes.count(L"bits/stdc++.hpp") > 0;

    size_t presetCount = 0;
    const PresetFunc *presets = PresetsFor(langId, presetCount);
    // C++ inherits the whole C library, so names that only live in the C table
    // (setlocale, strftime, tolower, ...) are legal in a .cpp file too.
    size_t cTableCount = 0;
    const PresetFunc *cTable = (_wcsicmp(langId, L"cpp") == 0) ? PresetsFor(L"c", cTableCount) : nullptr;

    for (const CallSite &cs : calls)
    {
        // Namespace-qualified calls (std::sort, std::move, fs::exists, ...):
        // the qualifier means the symbol belongs to a library namespace we
        // cannot resolve from the local headers. Accept them instead of
        // reporting "unknown function" — this is what std::/:: prefixes look
        // like, and false positives there were the main complaint.
        if (cs.name.find(L"::") != std::wstring::npos)
            continue;
        if (defined.count(cs.name))
            continue;
        if (alwaysOk.count(cs.name))
            continue;
        if (!IsKeyword(L, cs.name))
        {
            const PresetFunc *found = FindPreset(presets, presetCount, cs.name);
            if (!found && cTable)
                found = FindPreset(cTable, cTableCount, cs.name);
            if (found)
            {
                const std::wstring &h = found->header;
                if (!h.empty())
                {
                    bool okInc = universalInclude || includes.count(h) > 0;
                    if (!okInc && _wcsicmp(langId, L"cpp") == 0 && h.size() > 2 &&
                        h[h.size() - 2] == L'.' && h.back() == L'h')
                    {
                        // stdio.h  ->  cstdio ;  string.h -> cstring
                        std::wstring alt = L"c" + h.substr(0, h.size() - 2);
                        okInc = includes.count(alt) > 0;
                    }
                    if (!okInc && _wcsicmp(langId, L"cpp") == 0 && HeaderImpliedByCpp(includes, h))
                        okInc = true;
                    if (!okInc)
                        out.push_back({cs.line, cs.name, DiagKind::MissingInclude, h});
                }
            }
            else
            {
                // With <bits/stdc++.h> the whole standard library is in scope,
                // and our preset table is not an exhaustive list of it: flagging
                // anything that table happens to miss produced a steady stream
                // of false "unknown function" rows. Real errors still come from
                // the compiler, so stay quiet here. Set the flag below to false
                // to get strict checking back for umbrella-header files.
                static const bool kQuietUnderUniversalInclude = true;
                if (kQuietUnderUniversalInclude && universalInclude)
                    continue;
                // A known type used with constructor-style call? Skip if it's a type.
                if (L && IsTypeName(L, cs.name))
                    continue;
                out.push_back({cs.line, cs.name, DiagKind::UnknownFunction, L""});
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Public entry points                                                */
/* ------------------------------------------------------------------ */

static void SemanticCore(const std::wstring &text, const wchar_t *langId,
                         std::vector<UserSym> &syms, std::vector<Diag> &diags,
                         const std::wstring &filePath)
{
    syms.clear();
    diags.clear();
    if (text.empty() || !langId)
        return;
    const LanguageDef *L = LanguageById(langId);
    std::vector<CallSite> calls;
    std::set<std::wstring> includes, defs;
    std::vector<UserSym> raw;
    if (IsCFamily(langId) || _wcsicmp(langId, L"js") == 0 || _wcsicmp(langId, L"ts") == 0)
        ScanCFamily(L, text, raw, calls, includes, defs);
    else if (_wcsicmp(langId, L"py") == 0 || _wcsicmp(langId, L"rb") == 0 ||
             _wcsicmp(langId, L"lua") == 0)
        ScanScript(langId, text, raw);

    // Trace the document's #include headers — both the user's local
    // "xxx.h" and the toolchain's <xxx> — and merge the symbols they
    // declare (functions / variables / types / macros), so calls to them
    // are not flagged unknown. Local symbols also feed completion.
    TraceIncludes(text, langId, filePath, raw);

    // Deduplicate, preferring the first (definition) occurrence. When the
    // same name appears with different tags, prefer a definition-like tag
    // (F/D/C/S/T/E) over a weak one (V), so a real function/type declaration
    // wins over a mere variable occurrence. Crucially, keep the src flag so
    // system-header symbols stay marked src=1 (diagnostics-only, not in
    // completion).
    auto TagRank = [](wchar_t t) -> int {
        switch (t)
        {
        case L'F': case L'D': case L'C': case L'S': case L'T': case L'E':
            return 1;
        default:
            return 0;
        }
    };
    std::map<std::wstring, UserSym> best;
    for (const UserSym &s : raw)
    {
        auto it = best.find(s.name);
        if (it == best.end())
            best[s.name] = s;
        else if (TagRank(s.tag) > TagRank(it->second.tag))
            best[s.name] = s;
    }
    for (const auto &kv : best)
        syms.push_back(kv.second);

    if (SupportsDiagnostics(langId))
        BuildDiags(L, langId, calls, includes, defs, syms, diags);
}

void ScanUserSymbols(const std::wstring &text, const wchar_t *langId,
                     std::vector<UserSym> &out, const std::wstring &filePath)
{
    std::vector<Diag> diags;
    SemanticCore(text, langId, out, diags, filePath);
}

void ScanSemantic(const std::wstring &text, const wchar_t *langId,
                  std::vector<UserSym> &syms, std::vector<Diag> &diags,
                  const std::wstring &filePath)
{
    SemanticCore(text, langId, syms, diags, filePath);
}

void RunDiagnostics(const std::wstring &text, const wchar_t *langId,
                    const std::vector<UserSym> &syms, std::vector<Diag> &out)
{
    out.clear();
    if (!SupportsDiagnostics(langId))
        return;
    const LanguageDef *L = LanguageById(langId);
    std::vector<CallSite> calls;
    std::set<std::wstring> includes, defs;
    std::vector<UserSym> scratch;
    ScanCFamily(L, text, scratch, calls, includes, defs);
    BuildDiags(L, langId, calls, includes, defs, syms, out);
}
