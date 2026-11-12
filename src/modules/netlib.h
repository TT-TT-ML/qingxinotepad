/*
  Minimal WinHTTP helpers for the library browser:
    - GET a URL into memory (UTF-8 bytes)
    - GET a URL into a file, reporting progress
    - measure the latency of a URL (time until the first response byte)
*/

#pragma once

#include <windows.h>
#include <string>

typedef void (*NetProgressFn)(void *ctx, unsigned long long got, unsigned long long total);

// Returns false on failure and fills err with a short Chinese description.
bool NetGetString(const std::wstring &url, std::string &out, std::wstring &err, DWORD timeoutMs = 20000);

bool NetDownloadFile(const std::wstring &url, const std::wstring &path, std::wstring &err,
                     NetProgressFn progress = nullptr, void *ctx = nullptr, DWORD timeoutMs = 0);

// Milliseconds until the response headers arrive; -1 when the URL is unreachable.
long NetLatency(const std::wstring &url, DWORD timeoutMs = 6000);

// 0 when the file exists and is readable, otherwise the Win32 error code.
DWORD NetReadLocalFile(const std::wstring &path, std::string &out);
