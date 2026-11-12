
#include "editor.h"
#include "core/types.h"
#include "core/globals.h"
#include "codeeditor.h"
#include "syntax.h"
#include "ui.h"
#include "menu.h"

static const LanguageDef *g_currentLang = nullptr;

std::wstring GetEditorText()
{
    return CE_GetText(g_hwndEditor);
}

void SetEditorText(const std::wstring &text)
{
    CE_SetText(g_hwndEditor, text);
}

std::pair<int, int> GetCursorPos()
{
    return CE_GetCursor(g_hwndEditor);
}

void ApplyFont()
{
    if (g_state.hFont)
    {
        DeleteObject(g_state.hFont);
        g_state.hFont = nullptr;
    }
    int size = g_state.fontSize * g_state.zoomLevel / 100;
    size = (size < 8) ? 8 : (size > 500) ? 500
                                         : size;
    HDC hdc = GetDC(g_hwndMain);
    int height = -MulDiv(size, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(g_hwndMain, hdc);
    g_state.hFont = CreateFontW(height, 0, 0, 0, g_state.fontWeight, g_state.fontItalic, g_state.fontUnderline, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, g_state.fontName.c_str());
    CE_SetFont(g_hwndEditor, g_state.hFont);
}

void ApplyZoom()
{
    ApplyFont();
}

void ApplyWordWrap()
{
    CE_SetWordWrap(g_hwndEditor, g_state.wordWrap);
    SetFocus(g_hwndEditor);
}

void EditorSetLanguageFromPath(const std::wstring &path)
{
    g_currentLang = LanguageFromPath(path);
    CE_SetLanguage(g_hwndEditor, g_currentLang);
    EditorSetFilePath(path);
    UpdateStatus();
    RebuildBuildMenu();
}

void EditorSetFilePath(const std::wstring &path)
{
    CE_SetFilePath(g_hwndEditor, path);
}

const wchar_t *EditorLanguageName()
{
    if (g_currentLang && g_currentLang->display)
        return g_currentLang->display;
    return L"Plain Text";
}

const wchar_t *EditorLanguageId()
{
    if (g_currentLang && g_currentLang->id)
        return g_currentLang->id;
    return L"text";
}

void EditorUndo() { CE_Undo(g_hwndEditor); }
void EditorRedo() { CE_Redo(g_hwndEditor); }
void EditorSelectAll() { CE_SelectAll(g_hwndEditor); }
void EditorInsert(const std::wstring &s) { CE_InsertAtCaret(g_hwndEditor, s); }
