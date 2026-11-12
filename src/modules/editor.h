/*
   ▄████████  ▄██████▄     ▄████████  ▄█        ▄██████▄   ▄██████▄     ▄███████▄
  ███    ███ ███    ███   ███    ███ ███       ███    ███ ███    ███   ███    ███
  ███    █▀  ███    ███   ███    ███ ███       ███    ███ ███    ███   ███    ███
 ▄███▄▄▄     ███    ███  ▄███▄▄▄▄██▀ ███       ███    ███ ███    ███   ███    ███
▀▀███▀▀▀     ███    ███ ▀▀███▀▀▀▀▀   ███       ███    ███ ███    ███ ▀█████████▀
  ███        ███    ███ ▀███████████ ███       ███    ███ ███    ███   ███
  ███        ███    ███   ███    ███ ███▌    ▄ ███    ███ ███    ███   ███
  ███         ▀██████▀    ███    ███ █████▄▄██  ▀██████▀   ▀██████▀   ▄████▀

  Editor wrapper functions bridging the rest of the app to the custom,
  self-drawn code editor control (see codeeditor.h). Also owns the current
  syntax language (detected from the open file's extension).
*/

#pragma once
#include <windows.h>
#include <string>
#include <utility>

std::wstring GetEditorText();
void SetEditorText(const std::wstring &text);
std::pair<int, int> GetCursorPos();
void ApplyFont();
void ApplyZoom();
void ApplyWordWrap();

// Syntax language management.
void EditorSetLanguageFromPath(const std::wstring &path);
void EditorSetFilePath(const std::wstring &path);
const wchar_t *EditorLanguageName();
const wchar_t *EditorLanguageId();

// Editing shortcuts used by the menu / accelerator commands.
void EditorUndo();
void EditorRedo();
void EditorSelectAll();
void EditorInsert(const std::wstring &s);
