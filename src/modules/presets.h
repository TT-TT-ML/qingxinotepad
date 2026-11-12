/*
   Preset library functions and variables for code completion.
   Functions carry a required header so the "missing include" diagnostic can
   suggest it. Variables/constants carry a type tag shown as the completion
   prefix (V = variable/object, D = define/macro/constant, C = type,
   L = literal). An empty header means a language built-in that needs no
   include.
*/

#pragma once
#include <cstddef>

struct PresetFunc
{
    const wchar_t *name;
    const wchar_t *header; // L"" = no include needed; e.g. L"stdio.h", L"algorithm", L"fmt"
};

struct PresetVar
{
    const wchar_t *name;
    wchar_t tag;           // 'V' variable/object, 'D' define/macro/constant,
                           // 'C' type, 'L' literal
    const wchar_t *header; // L"" = no include needed
};

// Returns the preset function table for a language id (or nullptr if none).
const PresetFunc *PresetsFor(const wchar_t *langId, size_t &count);

// Returns the preset variable/constant table for a language id (or nullptr).
const PresetVar *PresetVarsFor(const wchar_t *langId, size_t &count);
