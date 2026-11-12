#pragma once

#include <windows.h>
#include <string>

/*
   Single place that decides where user state lives.

   Installed build:  HKEY_CURRENT_USER\Software\LegacyNotepad  +  %LOCALAPPDATA%\LegacyNotepad
   Portable build:   <folder of the exe>\qingxinotepad.ini      +  <folder of the exe>\data

   Portable mode is switched on by dropping a marker file next to the exe
   (qingxinotepad.portable or portable.txt), so the very same binary can be
   shipped twice: as a green copy that carries its settings with it, and as
   the installed version driven by the installer.
*/

bool StorageIsPortable();
std::wstring StorageExeDir();
std::wstring StorageIniPath();

bool StorageGetDword(const wchar_t *key, const wchar_t *name, DWORD *out);
bool StorageGetString(const wchar_t *key, const wchar_t *name, std::wstring *out);
void StorageSetDword(const wchar_t *key, const wchar_t *name, DWORD value);
void StorageSetString(const wchar_t *key, const wchar_t *name, const std::wstring &value);

// Root folder for downloaded libraries / cached lists.
std::wstring StorageDataRoot();
