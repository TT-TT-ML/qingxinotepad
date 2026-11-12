#include "core/storage.h"
#include <windows.h>
#include <cwchar>

static std::wstring g_exeDir;
static int g_portable = -1;

std::wstring StorageExeDir()
{
    if (!g_exeDir.empty())
        return g_exeDir;
    wchar_t path[MAX_PATH * 2] = {};
    DWORD n = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(sizeof(path) / sizeof(path[0])));
    if (n == 0 || n >= sizeof(path) / sizeof(path[0]))
    {
        g_exeDir = L".";
        return g_exeDir;
    }
    std::wstring full(path, static_cast<size_t>(n));
    const size_t slash = full.find_last_of(L"\\/");
    g_exeDir = (slash == std::wstring::npos) ? std::wstring(L".") : full.substr(0, slash);
    return g_exeDir;
}

static bool FileExistsAt(const std::wstring &path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool StorageIsPortable()
{
    if (g_portable >= 0)
        return g_portable != 0;
    const std::wstring dir = StorageExeDir();
    g_portable = (FileExistsAt(dir + L"\\qingxinotepad.portable") || FileExistsAt(dir + L"\\portable.txt")) ? 1 : 0;
    return g_portable != 0;
}

std::wstring StorageIniPath()
{
    return StorageExeDir() + L"\\qingxinotepad.ini";
}

// The registry key is kept as-is so an upgraded install keeps its settings.
// In portable mode the part after the last backslash becomes the INI section.
static std::wstring SectionOf(const wchar_t *key)
{
    std::wstring k = (key && *key) ? key : L"Software\\LegacyNotepad";
    const size_t p = k.find_last_of(L'\\');
    if (p != std::wstring::npos && p + 1 < k.size())
        return k.substr(p + 1);
    return k;
}

bool StorageGetDword(const wchar_t *key, const wchar_t *name, DWORD *out)
{
    if (!name || !out)
        return false;
    if (StorageIsPortable())
    {
        std::wstring sec = SectionOf(key);
        wchar_t buf[64] = {};
        GetPrivateProfileStringW(sec.c_str(), name, L"", buf, 64, StorageIniPath().c_str());
        if (!buf[0])
            return false;
        wchar_t *end = nullptr;
        const unsigned long v = wcstoul(buf, &end, 10);
        if (!end || *end != L'\0')
            return false;
        *out = static_cast<DWORD>(v);
        return true;
    }
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;
    DWORD v = 0;
    DWORD size = sizeof(v);
    const bool ok = RegQueryValueExW(hKey, name, nullptr, nullptr,
                                     reinterpret_cast<LPBYTE>(&v), &size) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    if (ok)
        *out = v;
    return ok;
}

bool StorageGetString(const wchar_t *key, const wchar_t *name, std::wstring *out)
{
    if (!name || !out)
        return false;
    if (StorageIsPortable())
    {
        std::wstring sec = SectionOf(key);
        wchar_t buf[8192] = {};
        const DWORD got = GetPrivateProfileStringW(sec.c_str(), name, L"", buf, 8192, StorageIniPath().c_str());
        if (got == 0)
            return false;
        out->assign(buf, static_cast<size_t>(got));
        return true;
    }
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;
    wchar_t buf[8192] = {};
    DWORD size = sizeof(buf);
    const bool ok = RegQueryValueExW(hKey, name, nullptr, nullptr,
                                     reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    if (!ok)
        return false;
    out->assign(buf);
    return true;
}

void StorageSetDword(const wchar_t *key, const wchar_t *name, DWORD value)
{
    if (!name)
        return;
    if (StorageIsPortable())
    {
        std::wstring sec = SectionOf(key);
        wchar_t buf[32] = {};
        _ultow_s(static_cast<unsigned long>(value), buf, 32, 10);
        WritePrivateProfileStringW(sec.c_str(), name, buf, StorageIniPath().c_str());
        return;
    }
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        return;
    RegSetValueExW(hKey, name, 0, REG_DWORD,
                   reinterpret_cast<const BYTE *>(&value), sizeof(value));
    RegCloseKey(hKey);
}

void StorageSetString(const wchar_t *key, const wchar_t *name, const std::wstring &value)
{
    if (!name)
        return;
    if (StorageIsPortable())
    {
        std::wstring sec = SectionOf(key);
        WritePrivateProfileStringW(sec.c_str(), name, value.c_str(), StorageIniPath().c_str());
        return;
    }
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        return;
    RegSetValueExW(hKey, name, 0, REG_SZ,
                   reinterpret_cast<const BYTE *>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
}

std::wstring StorageDataRoot()
{
    if (StorageIsPortable())
        return StorageExeDir() + L"\\data";
    wchar_t buf[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH) > 0)
        return std::wstring(buf) + L"\\LegacyNotepad";
    return L"C:\\LegacyNotepad";
}
