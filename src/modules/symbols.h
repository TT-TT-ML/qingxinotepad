/*
   Whole-document symbol scan (user-defined functions / classes / structs /
   variables / enums / macros) and lightweight call diagnostics:
   - undefined function calls   -> "unknown function" (red line)
   - known library function without its include -> "you need #include <xxx>"
*/

#pragma once
#include <string>
#include <vector>

struct UserSym
{
    std::wstring name;
    wchar_t tag; // 'F' function, 'V' variable, 'C' class, 'S' struct, 'E' enum,
                 // 'D' macro/define, 'T' type alias, 'M' module/import
    unsigned char src; // 0 = document / local header (shown in completion),
                       // 1 = system header (used for diagnostics only)
    UserSym() : tag(0), src(0) {}
    UserSym(const std::wstring &n, wchar_t t, unsigned char s = 0)
        : name(n), tag(t), src(s) {}
};

enum class DiagKind
{
    None,
    UnknownFunction, // called but not defined and not a known library function
    MissingInclude   // known library function, required header not included
};

struct Diag
{
    int line;                 // 0-based line number
    std::wstring name;        // called name (e.g. L"printf", L"std::sort")
    DiagKind kind;
    std::wstring header;      // for MissingInclude
};

// Scans the whole document for user-defined symbols (language-aware).
// filePath, when non-empty, also follows the document's local
// #include "..." headers and merges the symbols they declare.
void ScanUserSymbols(const std::wstring &text, const wchar_t *langId,
                     std::vector<UserSym> &out, const std::wstring &filePath = L"");

// One-pass scan: user symbols + call diagnostics (undefined / missing include).
// Prefer this over calling the two functions separately.
void ScanSemantic(const std::wstring &text, const wchar_t *langId,
                  std::vector<UserSym> &syms, std::vector<Diag> &diags,
                  const std::wstring &filePath = L"");

// Finds function calls that are undefined or missing their include.
// Only meaningful for C-family languages; other languages produce no diags.
void RunDiagnostics(const std::wstring &text, const wchar_t *langId,
                    const std::vector<UserSym> &syms, std::vector<Diag> &out);

// True when the language supports call diagnostics (C-family).
bool SupportsDiagnostics(const wchar_t *langId);
