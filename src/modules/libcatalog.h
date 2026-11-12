/*
  Library catalogue + download resolution.

  C++  : vcpkg port list (names + baseline versions) from the selected mirror;
         a port's upstream archive is resolved from its portfile.cmake.
  Python: full PyPI simple index from the selected mirror; per-package metadata
         and file URL come from the mirror's JSON API (official PyPI as fallback).
*/

#pragma once

#include <string>
#include <vector>

enum class LibLang;

struct LibEntry
{
    std::wstring name;
    std::wstring version;
    std::wstring summary;
};

// Loads (and caches for 24h) the whole list of that language.
bool LibLoadList(LibLang lang, const std::wstring &base, std::vector<LibEntry> &out,
                 std::wstring &err, bool forceRefresh = false);

// Fills version/summary of a Python package (no-op for C++).
bool LibFetchInfo(LibLang lang, const std::wstring &base, LibEntry &entry, std::wstring &err);

// Resolves the archive to download for that entry.
bool LibResolveDownload(LibLang lang, const std::wstring &base, const LibEntry &entry,
                        std::wstring &url, std::wstring &fileName, std::wstring &err);

// Creates and returns the download folder for that language.
std::wstring LibDownloadDir(LibLang lang);

// Folder holding the fetched lists.
std::wstring LibCacheDir();
