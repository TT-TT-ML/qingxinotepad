/*
  Self-drawn compile result window.

  Replaces the old MessageBox used after a build. Requirements:
    - no icon at all;
    - warnings in one block (yellow), errors in another block (red);
    - both blocks are read-only EDIT controls: text can be selected freely,
      and each block has its own 复制 button;
    - each block scrolls on its own.
*/

#pragma once

#include <windows.h>
#include <string>

// Show the result of a build. `output` is the raw compiler output (or the
// friendly "编译成功。" message). `success` selects the window title.
void ShowCompileResult(HWND parent, const std::wstring &output, bool success);
void CloseCompileResult();
