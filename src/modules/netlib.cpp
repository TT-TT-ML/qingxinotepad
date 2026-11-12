#include "netlib.h"

#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>

namespace
{

struct UrlParts
{
    bool https = false;
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = 0;
};

bool CrackUrl(const std::wstring &url, UrlParts &p)
{
    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    uc.dwHostNameLength = static_cast<DWORD>(-1);
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &uc))
        return false;

    p.host.assign(uc.lpszHostName, uc.dwHostNameLength);
    p.path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo && uc.dwExtraInfoLength)
        p.path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    p.https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    p.port = uc.nPort;
    return !p.host.empty();
}

HINTERNET OpenSession(DWORD timeoutMs)
{
    HINTERNET session = WinHttpOpen(L"QingxinNotepad-LibBrowser/1.0",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        session = WinHttpOpen(L"QingxinNotepad-LibBrowser/1.0",
                              WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session)
        WinHttpSetTimeouts(session, 6000, 6000, timeoutMs ? timeoutMs : 20000,
                           timeoutMs ? timeoutMs : 20000);
    return session;
}

HINTERNET SendGet(HINTERNET session, const UrlParts &p, DWORD timeoutMs)
{
    HINTERNET conn = WinHttpConnect(session, p.host.c_str(), p.port, 0);
    if (!conn)
        return nullptr;

    DWORD flags = p.https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", p.path.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, nullptr, flags);
    if (!req)
    {
        WinHttpCloseHandle(conn);
        return nullptr;
    }

    DWORD redirects = 10;
    WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
    WinHttpSetTimeouts(req, 6000, 6000, timeoutMs, timeoutMs);

    // No Accept-Encoding: WinHTTP does not decode gzip bodies, so asking for
    // compressed content would hand us bytes we cannot parse.
    WinHttpAddRequestHeaders(req, L"User-Agent: QingxinNotepad-LibBrowser/1.0\r\n", static_cast<DWORD>(-1),
                             WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    // The connection handle is owned by the request handle chain.
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
    {
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        return nullptr;
    }
    if (!WinHttpReceiveResponse(req, nullptr))
    {
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        return nullptr;
    }
    return req; // caller closes req then conn
}

void CloseChain(HINTERNET req, HINTERNET session)
{
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(session);
}

std::wstring ErrorText(const wchar_t *stage)
{
    DWORD code = GetLastError();
    std::wstring s = stage;
    s += L"（错误码 ";
    s += std::to_wstring(code);
    s += L"）";
    return s;
}

} // namespace

bool NetGetString(const std::wstring &url, std::string &out, std::wstring &err, DWORD timeoutMs)
{
    out.clear();
    UrlParts p;
    if (!CrackUrl(url, p))
    {
        err = L"网址无效";
        return false;
    }
    HINTERNET session = OpenSession(timeoutMs);
    if (!session)
    {
        err = L"无法初始化网络会话";
        return false;
    }
    HINTERNET req = SendGet(session, p, timeoutMs);
    if (!req)
    {
        err = ErrorText(L"连接失败");
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD status = 0;
    {
        DWORD size = sizeof(status);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
        {
            if (status >= 400)
            {
                CloseChain(req, session);
                err = L"服务器返回 HTTP " + std::to_wstring(status);
                return false;
            }
        }
    }

    std::string body;
    for (;;)
    {
        DWORD chunk = 0;
        if (!WinHttpQueryDataAvailable(req, &chunk))
            break;
        if (chunk == 0)
            break;
        std::vector<char> buf(chunk + 1);
        DWORD read = 0;
        if (!WinHttpReadData(req, buf.data(), chunk, &read))
            break;
        body.append(buf.data(), read);
    }

    CloseChain(req, session);

    if (body.empty())
    {
        err = L"服务器没有返回内容";
        return false;
    }
    out.swap(body);
    return true;
}

bool NetDownloadFile(const std::wstring &url, const std::wstring &path, std::wstring &err,
                     NetProgressFn progress, void *ctx, DWORD timeoutMs)
{
    UrlParts p;
    if (!CrackUrl(url, p))
    {
        err = L"网址无效";
        return false;
    }
    HINTERNET session = OpenSession(timeoutMs);
    if (!session)
    {
        err = L"无法初始化网络会话";
        return false;
    }
    HINTERNET req = SendGet(session, p, timeoutMs ? timeoutMs : 60000);
    if (!req)
    {
        err = ErrorText(L"连接失败");
        WinHttpCloseHandle(session);
        return false;
    }

    // A 404 page would otherwise be saved as if it were the real archive.
    DWORD status = 0;
    {
        DWORD size = sizeof(status);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) &&
            status >= 400)
        {
            CloseChain(req, session);
            err = L"服务器返回 HTTP " + std::to_wstring(status);
            return false;
        }
    }

    unsigned long long total = 0;
    {
        wchar_t lenBuf[64] = {};
        DWORD lenSize = sizeof(lenBuf) - sizeof(wchar_t);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH,
                                WINHTTP_HEADER_NAME_BY_INDEX, lenBuf, &lenSize,
                                WINHTTP_NO_HEADER_INDEX))
        {
            total = static_cast<unsigned long long>(_wtoi64(lenBuf));
        }
    }

    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        err = L"无法写入文件";
        CloseChain(req, session);
        return false;
    }

    unsigned long long got = 0;
    bool ok = true;
    for (;;)
    {
        DWORD chunk = 0;
        if (!WinHttpQueryDataAvailable(req, &chunk) || chunk == 0)
            break;
        std::vector<char> buf(chunk);
        DWORD read = 0;
        if (!WinHttpReadData(req, buf.data(), chunk, &read) || read == 0)
        {
            ok = false;
            break;
        }
        DWORD written = 0;
        if (!WriteFile(hFile, buf.data(), read, &written, nullptr) || written != read)
        {
            ok = false;
            err = L"写入文件失败";
            break;
        }
        got += read;
        if (progress)
            progress(ctx, got, total);
    }
    CloseHandle(hFile);
    CloseChain(req, session);

    if (!ok)
    {
        DeleteFileW(path.c_str());
        if (err.empty())
            err = L"下载中断";
        return false;
    }
    if (got == 0)
    {
        DeleteFileW(path.c_str());
        err = L"服务器返回了空文件";
        return false;
    }
    return true;
}

long NetLatency(const std::wstring &url, DWORD timeoutMs)
{
    UrlParts p;
    if (!CrackUrl(url, p))
        return -1;
    HINTERNET session = OpenSession(timeoutMs);
    if (!session)
        return -1;

    const DWORD begin = GetTickCount();
    HINTERNET conn = WinHttpConnect(session, p.host.c_str(), p.port, 0);
    HINTERNET req = WinHttpOpenRequest(
        conn,
        L"GET", p.path.c_str(), nullptr, WINHTTP_NO_REFERER, nullptr,
        p.https ? WINHTTP_FLAG_SECURE : 0);
    if (!req)
    {
        WinHttpCloseHandle(session);
        return -1;
    }
    WinHttpSetTimeouts(req, 4000, 4000, timeoutMs, timeoutMs);
    long ms = -1;
    if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr))
    {
        ms = static_cast<long>(GetTickCount() - begin);
    }
    CloseChain(req, session);
    return ms;
}

DWORD NetReadLocalFile(const std::wstring &path, std::string &out)
{
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError();
    DWORD size = 0;
    DWORD lo = GetFileSize(h, &size);
    CloseHandle(h);
    if (lo == INVALID_FILE_SIZE && GetLastError() != 0)
        return GetLastError();

    h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError();
    std::string buf;
    buf.resize(lo);
    std::size_t done = 0;
    while (done < buf.size())
    {
        DWORD want = static_cast<DWORD>(buf.size() - done);
        DWORD got = 0;
        if (!ReadFile(h, buf.data() + done, want, &got, nullptr) || got == 0)
            break;
        done += got;
    }
    CloseHandle(h);
    buf.resize(done);
    out.swap(buf);
    return 0;
}
