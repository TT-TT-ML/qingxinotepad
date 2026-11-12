#include "libcatalog.h"
#include "libsource.h"
#include "netlib.h"
#include "core/storage.h"

#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>

namespace
{

const unsigned long long kCacheTtlSeconds = 24ull * 3600ull;

std::wstring Utf8ToWide(const std::string &s)
{
    if (s.empty())
        return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), len);
    return w;
}

std::string WideToUtf8(const std::wstring &s)
{
    if (s.empty())
        return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string a(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), a.data(), len, nullptr, nullptr);
    return a;
}

bool FileExists(const std::wstring &p)
{
    DWORD attr = GetFileAttributesW(p.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool DirExists(const std::wstring &p)
{
    DWORD attr = GetFileAttributesW(p.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

void EnsureDir(const std::wstring &p)
{
    if (p.empty() || DirExists(p))
        return;
    size_t slash = p.find_last_of(L'\\');
    if (slash != std::wstring::npos && slash > 2)
        EnsureDir(p.substr(0, slash));
    CreateDirectoryW(p.c_str(), nullptr);
}

bool WriteAll(const std::wstring &path, const std::string &data)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    std::size_t done = 0;
    while (done < data.size())
    {
        DWORD got = 0;
        DWORD want = static_cast<DWORD>(data.size() - done);
        if (!WriteFile(h, data.data() + done, want, &got, nullptr) || got == 0)
            break;
        done += got;
    }
    CloseHandle(h);
    return done == data.size();
}

unsigned long long FileAgeSeconds(const std::wstring &path)
{
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
        return 0xFFFFFFFFull;
    FILETIME now = {};
    GetSystemTimeAsFileTime(&now);
    unsigned __int64 a = (static_cast<unsigned __int64>(fad.ftLastWriteTime.dwHighDateTime) << 32) |
                         fad.ftLastWriteTime.dwLowDateTime;
    unsigned __int64 b = (static_cast<unsigned __int64>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    if (b <= a)
        return 0;
    return (b - a) / 10000000ull;
}

std::wstring LangKey(LibLang lang)
{
    return (lang == LibLang::Cpp) ? L"cpp" : L"python";
}

// ---------------------------------------------------------------- C++ list

void ParseBaseline(const std::string &body, std::vector<LibEntry> &out)
{
    out.clear();
    std::size_t p = body.find("\"baseline\"");
    while (p != std::string::npos)
    {
        const std::size_t brace = body.rfind('{', p);
        if (brace == std::string::npos || brace < 3)
            break;
        std::size_t q1 = body.rfind('"', brace - 1);
        if (q1 == std::string::npos || q1 < 2)
            break;
        std::size_t q0 = body.rfind('"', q1 - 1);
        if (q0 == std::string::npos)
            break;
        std::string name = body.substr(q0 + 1, q1 - q0 - 1);

        std::size_t v0 = body.find('"', p + 10);
        if (v0 == std::string::npos)
            break;
        std::size_t v1 = body.find('"', v0 + 1);
        if (v1 == std::string::npos)
            break;
        std::string ver = body.substr(v0 + 1, v1 - v0 - 1);

        if (!name.empty() && name.size() < 64 && name.find('\\') == std::string::npos)
        {
            LibEntry e;
            e.name = Utf8ToWide(name);
            e.version = Utf8ToWide(ver);
            e.summary = L"vcpkg 端口";
            out.push_back(e);
        }
        p = body.find("\"baseline\"", v1);
    }
    std::sort(out.begin(), out.end(), [](const LibEntry &a, const LibEntry &b) { return a.name < b.name; });
}

// ------------------------------------------------------------ Python list

bool NameCharOk(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '.' || c == '-' || c == '_';
}

void ParseSimpleIndex(const std::string &body, std::vector<LibEntry> &out)
{
    out.clear();
    std::size_t p = 0;
    for (;;)
    {
        p = body.find("<a ", p);
        if (p == std::string::npos)
            break;
        std::size_t gt = body.find('>', p);
        if (gt == std::string::npos)
            break;
        std::size_t lt = body.find('<', gt + 1);
        if (lt == std::string::npos)
            break;
        std::string name = body.substr(gt + 1, lt - gt - 1);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\r' || name.back() == '\n'))
            name.pop_back();
        if (!name.empty() && name.size() < 80)
        {
            bool ok = true;
            for (char c : name)
            {
                if (!NameCharOk(c))
                {
                    ok = false;
                    break;
                }
            }
            if (ok)
            {
                LibEntry e;
                e.name = Utf8ToWide(name);
                // No per-entry summary: the PyPI index has ~600k names and a
                // duplicated literal for each of them costs real memory. The
                // list paints a placeholder instead.
                out.push_back(e);
            }
        }
        p = lt;
    }
}

std::string SerializeList(const std::vector<LibEntry> &list)
{
    std::string data;
    data.reserve(list.size() * 32);
    for (const LibEntry &e : list)
    {
        data += WideToUtf8(e.name);
        data += '\t';
        data += WideToUtf8(e.version);
        data += '\n';
    }
    return data;
}

void DeserializeList(const std::string &data, std::vector<LibEntry> &out)
{
    out.clear();
    std::size_t p = 0;
    while (p < data.size())
    {
        std::size_t nl = data.find('\n', p);
        if (nl == std::string::npos)
            nl = data.size();
        std::string line = data.substr(p, nl - p);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::size_t tab = line.find('\t');
        LibEntry e;
        if (tab == std::string::npos)
        {
            e.name = Utf8ToWide(line);
        }
        else
        {
            e.name = Utf8ToWide(line.substr(0, tab));
            e.version = Utf8ToWide(line.substr(tab + 1));
        }
        if (!e.name.empty())
            out.push_back(e);
        p = nl + 1;
    }
}

// -------------------------------------------------------------- downloads

int ScorePythonFile(const std::string &fname)
{
    auto has = [&fname](const char *s) { return fname.find(s) != std::string::npos; };
    if (has(".whl"))
    {
        if (has("cp313") && has("win_amd64"))
            return 100;
        if (has("cp313"))
            return 90;
        if (has("py3-none-any"))
            return 80;
        return 70;
    }
    if (has(".tar.gz"))
        return 50;
    if (has(".zip"))
        return 40;
    return 0;
}

// Extracts the version segment of a PyPI file name, which sits between the
// package name and the first tag part: "requests-2.32.3-py3-none-any.whl".
std::string FileVersion(const std::string &fname)
{
    std::size_t dash = fname.find('-');
    if (dash == std::string::npos)
        return std::string();
    std::size_t end = fname.find('-', dash + 1);
    if (end == std::string::npos)
        end = fname.find('.', dash + 1);
    if (end == std::string::npos)
        end = fname.size();
    return fname.substr(dash + 1, end - dash - 1);
}

bool VersionGreater(const std::string &a, const std::string &b)
{
    std::size_t i = 0, j = 0;
    while (i < a.size() || j < b.size())
    {
        long na = 0, nb = 0;
        bool da = false, db = false;
        while (i < a.size() && a[i] >= '0' && a[i] <= '9')
        {
            na = na * 10 + (a[i] - '0');
            da = true;
            ++i;
        }
        while (j < b.size() && b[j] >= '0' && b[j] <= '9')
        {
            nb = nb * 10 + (b[j] - '0');
            db = true;
            ++j;
        }
        if (da && !db)
            return true;
        if (!da && db)
            return false;
        if (na != nb)
            return na > nb;
        if (i < a.size() && a[i] != '.' && a[i] != '-' && a[i] != '+')
            ++i;
        if (j < b.size() && b[j] != '.' && b[j] != '-' && b[j] != '+')
            ++j;
        while (i < a.size() && (a[i] == '.' || a[i] == '-' || a[i] == '+'))
            ++i;
        while (j < b.size() && (b[j] == '.' || b[j] == '-' || b[j] == '+'))
            ++j;
    }
    return false;
}

bool FindPythonFile(const std::string &json, std::wstring &url, std::wstring &fileName)
{
    int bestScore = 0;
    std::string bestVer;
    bool haveBest = false;
    std::size_t p = 0;
    for (;;)
    {
        p = json.find("\"filename\"", p);
        if (p == std::string::npos)
            break;
        std::size_t fnStart = json.find('"', json.find(':', p)) + 1;
        std::size_t fnEnd = json.find('"', fnStart);
        if (fnEnd == std::string::npos)
            break;
        std::string fname = json.substr(fnStart, fnEnd - fnStart);

        // The "url" key of the same file object follows the filename.
        std::size_t uKey = json.find("\"url\"", fnEnd);
        std::size_t nextFn = json.find("\"filename\"", fnEnd + 6);
        (void)nextFn;
        if (uKey == std::string::npos)
            break;
        std::size_t uStart = json.find('"', json.find(':', uKey)) + 1;
        std::size_t uEnd = json.find('"', uStart);
        if (uEnd == std::string::npos)
            break;
        std::string u = json.substr(uStart, uEnd - uStart);

        const int score = ScorePythonFile(fname);
        const std::string ver = FileVersion(fname);
        const bool better = !haveBest || score > bestScore ||
                            (score == bestScore && VersionGreater(ver, bestVer));
        if (better)
        {
            bestScore = score;
            bestVer = ver;
            haveBest = true;
            url = Utf8ToWide(u);
            fileName = Utf8ToWide(fname);
        }
        p = uEnd;
    }
    return bestScore > 0;
}

bool FindCppArchive(const std::string &portfile, std::wstring &url)
{
    static const char *kExts[] = {".zip", ".tar.gz", ".tgz", ".tar.xz", ".tar.bz2"};
    std::size_t p = 0;
    while ((p = portfile.find("https://", p)) != std::string::npos)
    {
        std::size_t end = p;
        while (end < portfile.size())
        {
            const char c = portfile[end];
            if (c == '"' || c == ' ' || c == '\r' || c == '\n' || c == ')' || c == '\'')
                break;
            ++end;
        }
        std::string u = portfile.substr(p, end - p);
        bool archive = false;
        for (const char *ext : kExts)
        {
            if (u.find(ext) != std::string::npos)
            {
                archive = true;
                break;
            }
        }
        if (archive && u.size() > 12)
        {
            url = Utf8ToWide(u);
            return true;
        }
        p = end;
    }
    return false;
}

// Reads one whitespace-delimited token following a keyword inside a cmake call.
std::string AfterKeyword(const std::string &block, const std::string &key)
{
    std::size_t p = block.find(key);
    if (p == std::string::npos)
        return std::string();
    p += key.size();
    while (p < block.size() && (block[p] == ' ' || block[p] == '\t'))
        ++p;
    if (p < block.size() && block[p] == '"')
        ++p;
    std::size_t end = p;
    while (end < block.size() && block[end] != '"' && block[end] != ' ' && block[end] != '\t' &&
           block[end] != '\r' && block[end] != '\n' && block[end] != ')')
        ++end;
    return block.substr(p, end - p);
}

// vcpkg ports fetched with vcpkg_from_github carry no literal URL, only REPO
// and REF. Their source archive is the GitHub tag tarball, which is derivable.
bool FindGithubArchive(const std::string &portfile, const std::string &version, std::wstring &url,
                       std::wstring &fileName)
{
    std::size_t p = portfile.find("vcpkg_from_github");
    if (p == std::string::npos)
        return false;
    std::size_t close = portfile.find(')', p);
    if (close == std::string::npos)
        close = portfile.size();
    const std::string block = portfile.substr(p, close - p);

    std::string repo = AfterKeyword(block, "REPO");
    std::string ref = AfterKeyword(block, "REF");
    if (repo.empty() || ref.empty())
        return false;

    // REF may be written with any cmake placeholder, e.g. "${VERSION}" or
    // "${OATPP_VERSION}". The port's own version from the baseline is the
    // value those placeholders resolve to when vcpkg runs.
    std::size_t at = ref.find("${");
    if (at != std::string::npos)
    {
        std::size_t braceEnd = ref.find('}', at);
        if (braceEnd == std::string::npos)
            return false;
        if (version.empty())
            return false;
        ref.replace(at, braceEnd - at + 1, version);
    }

    std::string flat = repo;
    std::replace(flat.begin(), flat.end(), '/', '-');
    std::string archive = "https://github.com/" + repo + "/archive/refs/tags/" + ref + ".tar.gz";
    url = Utf8ToWide(archive);
    fileName = Utf8ToWide(flat + "-" + ref + ".tar.gz");
    return true;
}

} // namespace

std::wstring LibCacheDir()
{
    // Installed build: %LOCALAPPDATA%\LegacyNotepad
    // Portable build: <folder of the exe>\data
    std::wstring root = StorageDataRoot();
    EnsureDir(root);
    return root;
}

std::wstring LibDownloadDir(LibLang lang)
{
    std::wstring dir = LibCacheDir() + L"\\libs\\" + LangKey(lang);
    EnsureDir(dir);
    return dir;
}

bool LibLoadList(LibLang lang, const std::wstring &base, std::vector<LibEntry> &out, std::wstring &err,
                 bool forceRefresh)
{
    (void)base;
    out.clear();
    const std::wstring cache = LibCacheDir() + L"\\list_" + LangKey(lang) + L".txt";
    const unsigned long long age = FileAgeSeconds(cache);
    if (!forceRefresh && FileExists(cache) && age <= kCacheTtlSeconds)
    {
        std::string data;
        if (NetReadLocalFile(cache, data) == 0 && !data.empty())
        {
            DeserializeList(data, out);
            if (!out.empty())
                return true;
        }
    }

    std::wstring url;
    if (lang == LibLang::Cpp)
        url = base + L"versions/baseline.json";
    else
        url = base + L"simple/";

    std::string body;
    // The PyPI simple index is ~30 MB, so it needs a much longer budget than
    // the small vcpkg baseline file.
    const DWORD budget = (lang == LibLang::Cpp) ? 45000 : 120000;
    if (!NetGetString(url, body, err, budget))
    {
        // Stale cache is better than an empty window.
        if (FileExists(cache))
        {
            std::string data;
            if (NetReadLocalFile(cache, data) == 0 && !data.empty())
            {
                DeserializeList(data, out);
                err.clear();
                if (!out.empty())
                    return true;
            }
        }
        return false;
    }

    if (lang == LibLang::Cpp)
        ParseBaseline(body, out);
    else
        ParseSimpleIndex(body, out);

    if (out.empty())
    {
        err = L"列表解析失败（返回内容不是预期格式）";
        return false;
    }

    WriteAll(cache, SerializeList(out));
    return true;
}

bool LibFetchInfo(LibLang lang, const std::wstring &base, LibEntry &entry, std::wstring &err)
{
    if (lang == LibLang::Cpp)
        return true; // version already comes from the port list

    std::string body;
    std::wstring perr;
    const std::wstring nameUtf8 = entry.name;
    std::wstring url = base + L"pypi/" + nameUtf8 + L"/json";
    if (!NetGetString(url, body, perr, 15000))
    {
        url = L"https://pypi.org/pypi/" + nameUtf8 + L"/json";
        if (!NetGetString(url, body, perr, 15000))
        {
            err = perr;
            return false;
        }
    }

    std::size_t vKey = body.find("\"version\"");
    if (vKey != std::string::npos)
    {
        std::size_t v0 = body.find('"', body.find(':', vKey)) + 1;
        std::size_t v1 = body.find('"', v0);
        if (v1 != std::string::npos && v1 > v0)
            entry.version = Utf8ToWide(body.substr(v0, v1 - v0));
    }
    std::size_t sKey = body.find("\"summary\"");
    if (sKey != std::string::npos)
    {
        std::size_t s0 = body.find('"', body.find(':', sKey));
        if (s0 != std::string::npos)
        {
            ++s0;
            std::size_t s1 = s0;
            while (s1 < body.size() && body[s1] != '"')
            {
                if (body[s1] == '\\')
                    ++s1;
                ++s1;
            }
            if (s1 <= body.size())
                entry.summary = Utf8ToWide(body.substr(s0, s1 - s0));
        }
    }
    if (entry.summary.empty())
        entry.summary = L"PyPI 包";
    return true;
}

bool LibResolveDownload(LibLang lang, const std::wstring &base, const LibEntry &entry,
                        std::wstring &url, std::wstring &fileName, std::wstring &err)
{
    url.clear();
    fileName.clear();

    if (lang == LibLang::Python)
    {
        std::string body;
        std::wstring perr;
        std::wstring meta = base + L"pypi/" + entry.name + L"/json";
        if (!NetGetString(meta, body, perr, 15000))
        {
            meta = L"https://pypi.org/pypi/" + entry.name + L"/json";
            if (!NetGetString(meta, body, perr, 15000))
            {
                err = perr;
                return false;
            }
        }
        if (!FindPythonFile(body, url, fileName))
        {
            err = L"没有找到可下载的安装包文件";
            return false;
        }
        return true;
    }

    std::string portfile;
    std::wstring perr;
    std::wstring pf = base + L"ports/" + entry.name + L"/portfile.cmake";
    if (!NetGetString(pf, portfile, perr, 15000))
    {
        pf = L"https://raw.githubusercontent.com/microsoft/vcpkg/master/ports/" + entry.name + L"/portfile.cmake";
        if (!NetGetString(pf, portfile, perr, 15000))
        {
            err = perr;
            return false;
        }
    }
    if (!FindCppArchive(portfile, url))
    {
        if (!FindGithubArchive(portfile, WideToUtf8(entry.version), url, fileName))
        {
            err = L"该端口没有可直接下载的源码包（可能需要 git/SCons 拉取）";
            return false;
        }
        return true;
    }
    std::size_t slash = url.find_last_of(L'/');
    if (slash != std::wstring::npos)
        fileName = url.substr(slash + 1);
    if (fileName.empty())
        fileName = entry.name + L"-" + entry.version + L".zip";
    return true;
}
