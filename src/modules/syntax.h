/*
   ▄████████  ▄██████▄     ▄████████  ▄█        ▄██████▄   ▄██████▄     ▄███████▄
  ███    ███ ███    ███   ███    ███ ███       ███    ███ ███    ███   ███    ███
  ███    █▀  ███    ███   ███    ███ ███       ███    ███ ███    ███   ███    ███
 ▄███▄▄▄     ███    ███  ▄███▄▄▄▄██▀ ███       ███    ███ ███    ███   ███    ███
▀▀███▀▀▀     ███    ███ ▀▀███▀▀▀▀▀   ███       ███    ███ ███    ███ ▀█████████▀
  ███        ███    ███ ▀███████████ ███       ███    ███ ███    ███   ███
  ███        ███    ███   ███    ███ ███▌    ▄ ███    ███ ███    ███   ███
  ███         ▀██████▀    ███    ███ █████▄▄██  ▀██████▀   ▀██████▀   ▄████▀

  Syntax highlighting definitions and a lightweight tokenizer.
  Maps file extensions to language definitions and tokenizes lines into
  typed spans (keywords, strings, comments, numbers, preprocessor, ...).
  Multi-line block states (block comments / triple-quoted strings) are
  tracked per line so any line can be tokenized independently.
*/

#pragma once
#include <windows.h>
#include <string>
#include <vector>

enum class TokType
{
    Default,
    Keyword,
    Type,
    Literal,
    String,
    Comment,
    Number,
    Preprocessor
};

// Block state inherited across line boundaries.
enum LineBlockState : unsigned char
{
    LBS_None = 0,
    LBS_BlockComment,
    LBS_TripleDouble,
    LBS_TripleSingle
};

struct LanguageDef
{
    const wchar_t *id;        // internal id, e.g. "cpp"
    const wchar_t *display;   // display name, e.g. "C/C++"
    const wchar_t *const *keywords; // null-terminated
    const wchar_t *const *types;    // null-terminated
    const wchar_t *const *literals; // null-terminated (true/false/null/None...)
    const wchar_t *lineComment;     // nullptr if none, e.g. "//"
    const wchar_t *blockStart;      // nullptr if none, e.g. "/*"
    const wchar_t *blockEnd;        // nullptr if none, e.g. "*/"
    bool stringDouble;   // "..." string
    bool stringSingle;   // '...' string
    bool stringBacktick; // `...` string
    bool tripleString;   // """ ... """ / ''' ... ''' (Python, Rust)
    bool preprocessor;   // '#' first-non-space starts preprocessor line
};

struct LineToken
{
    TokType type;
    size_t start; // inclusive, in line coordinates
    size_t length;
};

// Find the language definition for a file path (by extension).
const LanguageDef *LanguageFromPath(const std::wstring &path);
const LanguageDef *LanguageById(const wchar_t *id);
int LanguageCount();
const LanguageDef *LanguageByIndex(int index);

// Tokenize one logical line. stateIn is the block state at line start;
// stateOut receives the block state at line end. Line must NOT contain
// '\n' (the caller strips the newline). A trailing '\r' is allowed.
void TokenizeLine(const LanguageDef *lang, const std::wstring &line,
                  unsigned char stateIn, unsigned char &stateOut,
                  std::vector<LineToken> &tokens);

bool IsIdentifierStart(wchar_t c);
bool IsIdentifierChar(wchar_t c);
