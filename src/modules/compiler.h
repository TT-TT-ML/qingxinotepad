/*
   ▄████████  ▄██████▄     ▄████████  ▄█        ▄██████▄   ▄██████▄     ▄███████▄
  ███    ███ ███    ███   ███    ███ ███       ███    ███ ███    ███   ███    ███
  ███    █▀  ███    ███   ███    ███ ███       ███    ███ ███    ███   ███    ███
 ▄███▄▄▄     ███    ███  ▄███▄▄▄▄██▀ ███       ███    ███ ███    ███   ███    ███
▀▀███▀▀▀     ███    ███ ▀▀███▀▀▀▀▀   ███       ███    ███ ███    ███ ▀█████████▀
  ███        ███    ███ ▀███████████ ███       ███    ███ ███    ███   ███
  ███        ███    ███   ███    ███ ███▌    ▄ ███    ███ ███    ███   ███
  ███         ▀██████▀    ███    ███ █████▄▄██  ▀██████▀   ▀██████▀   ▄████▀

  Build / compile integration: per-language compiler command (configurable and
  saved to the registry) plus a "compile current file" runner that captures the
  tool's output.
*/

#pragma once

#include <windows.h>
#include <string>

// Posted from the compile worker thread back to the main window when a
// background compile finishes. wParam = pointer to a heap-allocated
// CompileResult that the main thread must delete after use.
#define WM_APP_COMPILE_DONE (WM_APP + 5)

struct CompileResult
{
    bool success = false;
    bool timedOut = false;
    std::wstring output;
};

void LoadCompilerCommands();      // read saved per-language commands at startup
void BuildCompile();              // compile (or run) the current file on a worker thread
void BuildCompileLang(const std::wstring &id); // compile the current file as a specific language
void BuildCompileForce();         // "ignore checks, force compile": build while suppressing the code-check popup
void BuildCompileRun();           // 编译并运行：成功后把产物送进命令行窗口执行
void BuildShowSettings();         // modal dialog to view/edit per-language commands

// UI-thread handler for the compile-finished notification (WM_APP_COMPILE_DONE).
void CompileNotifyDone(HWND hwnd, WPARAM wParam);
