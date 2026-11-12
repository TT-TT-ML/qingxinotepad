/*
   ▄████████  ▄██████▄     ▄████████  ▄█        ▄██████▄   ▄██████▄     ▄███████▄
  ███    ███ ███    ███   ███    ███ ███       ███    ███ ███    ███   ███    ███
  ███    █▀  ███    ███   ███    ███ ███       ███    ███ ███    ███   ███    ███
 ▄███▄▄▄     ███    ███  ▄███▄▄▄▄██▀ ███       ███    ███ ███    ███   ███    ███
▀▀███▀▀▀     ███    ███ ▀▀███▀▀▀▀▀   ███       ███    ███ ███    ███ ▀█████████▀
  ███        ███    ███ ▀███████████ ███       ███    ███ ███    ███   ███
  ███        ███    ███   ███    ███ ███▌    ▄ ███    ███ ███    ███   ███
  ███         ▀██████▀    ███    ███ █████▄▄██  ▀██████▀   ▀██████▀   ▄████▀

  A custom, fully self-drawn code editor control.
  Owns its own text model, caret, selection, scrolling, undo/redo, clipboard,
  real-time syntax highlighting and code completion. Replaces the RichEdit
  control ("abandon the rich-text approach"). Handles the standard edit
  messages used elsewhere in the app (EM_GETSEL / EM_SETSEL / EM_REPLACESEL /
  EM_SCROLLCARET / EM_EXLINEFROMCHAR / EM_LINEINDEX / EM_CANUNDO / EM_UNDO /
  WM_CUT / WM_COPY / WM_PASTE / WM_CLEAR) so find/replace/goto keep working.
*/

#pragma once
#include <windows.h>
#include <string>
#include <utility>

bool RegisterCodeEditorClasses(HINSTANCE hInstance);
HWND CreateCodeEditor(HWND parent, HMENU id, HINSTANCE hInstance);

// --- text / selection --------------------------------------------------
std::wstring CE_GetText(HWND hwnd);
void CE_SetText(HWND hwnd, const std::wstring &text);
void CE_GetSelection(HWND hwnd, DWORD *start, DWORD *end);
void CE_SetSelection(HWND hwnd, DWORD start, DWORD end);
void CE_ReplaceSelection(HWND hwnd, const wchar_t *text);
int CE_LineFromChar(HWND hwnd, DWORD pos);
int CE_LineIndex(HWND hwnd, int line);
void CE_ScrollCaret(HWND hwnd);
std::pair<int, int> CE_GetCursor(HWND hwnd);

// --- editing -----------------------------------------------------------
void CE_Undo(HWND hwnd);
void CE_Redo(HWND hwnd);
bool CE_CanUndo(HWND hwnd);
void CE_SelectAll(HWND hwnd);
void CE_InsertAtCaret(HWND hwnd, const std::wstring &s);

// --- appearance / language ---------------------------------------------
void CE_SetFont(HWND hwnd, HFONT font);
void CE_SetWordWrap(HWND hwnd, bool wrap);
void CE_Refresh(HWND hwnd);
void CE_SetLanguage(HWND hwnd, const struct LanguageDef *lang);
// Remember the current file's path so semantic scanning can resolve and
// follow the document's local #include "..." headers.
void CE_SetFilePath(HWND hwnd, const std::wstring &path);

// "Force compile": cancel any pending code-check popup (debounced WM_TIMER)
// so the build can proceed without the checker nagging.
void CE_SuppressPendingCheck(HWND hwnd);

// --- code completion ---------------------------------------------------
void CE_RequestCompletion(HWND hwnd);
void CE_CommitCompletion(HWND hwnd);

LRESULT CALLBACK CodeEditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
