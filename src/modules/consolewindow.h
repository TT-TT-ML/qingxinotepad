/*
   ▄████████  ▄██████▄     ▄████████  ▄█        ▄██████▄   ▄██████▄     ▄███████▄
  ███    ███ ███    ███   ███    ███ ███       ███    ███ ███    ███   ███    ███
  ███    █▀  ███    ███   ███    ███ ███       ███    ███ ███    ███   ███    ███
 ▄███▄▄▄     ███    ███  ▄███▄▄▄▄██▀ ███       ███    ███ ███    ███   ███    ███
▀▀███▀▀▀     ███    ███ ▀▀███▀▀▀▀▀   ███       ███    ███ ███    ███ ▀█████████▀
  ███        ███    ███ ▀███████████ ███       ███    ███ ███    ███   ███
  ███        ███    ███   ███    ███ ███▌    ▄ ███    ███ ███    ███   ███
  ███         ▀██████▀    ███    ███ █████▄▄██  ▀██████▀   ▀██████▀   ▄████▀

  Console window: a self-drawn command line panel. It runs cmd.exe (or a
  freshly built exe) with the working directory of the file currently shown,
  captures stdout/stderr and paints the result in a rows*cols character grid
  (default 5 rows by 50 columns).
*/

#pragma once

#include <windows.h>
#include <string>

// Register the console window class (call once after the main window exists).
void ConsoleInstall(HWND hwndMain);

// Open the console (creating it on first use) at the directory of the file
// that is currently shown, and give it focus.
void ConsoleShow(HWND hwndMain);

// Close the console window if it is open.
void ConsoleClose();

bool ConsoleIsOpen();

// 编译并运行：编译成功后把刚产出的 exe 送进命令行窗口执行。
void ConsoleRunBuiltExe(const std::wstring &exePath);

// 默认尺寸（行 × 每行字符），也用于启动参数设置。
void ConsoleSetDefaultSize(int rows, int cols);
int ConsoleDefaultRows();
int ConsoleDefaultCols();
