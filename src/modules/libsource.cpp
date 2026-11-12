#include "libsource.h"
#include "netlib.h"

#include <windows.h>
#include <atomic>
#include <thread>
#include <vector>

namespace
{

const LibSource kCppSources[] = {
    {LibLang::Cpp, L"GitHub 官方", L"https://raw.githubusercontent.com/microsoft/vcpkg/master/",
     L"https://raw.githubusercontent.com/microsoft/vcpkg/master/versions/baseline.json"},
    {LibLang::Cpp, L"jsDelivr CDN", L"https://cdn.jsdelivr.net/gh/microsoft/vcpkg@master/",
     L"https://cdn.jsdelivr.net/gh/microsoft/vcpkg@master/versions/baseline.json"},
    {LibLang::Cpp, L"gh-proxy 代理", L"https://gh-proxy.com/https://raw.githubusercontent.com/microsoft/vcpkg/master/",
     L"https://gh-proxy.com/https://raw.githubusercontent.com/microsoft/vcpkg/master/versions/baseline.json"},
    {LibLang::Cpp, L"ghfast 代理", L"https://ghfast.top/https://raw.githubusercontent.com/microsoft/vcpkg/master/",
     L"https://ghfast.top/https://raw.githubusercontent.com/microsoft/vcpkg/master/versions/baseline.json"},
    {LibLang::Cpp, L"Gitee 镜像", L"https://gitee.com/mirrors/vcpkg/raw/master/",
     L"https://gitee.com/mirrors/vcpkg/raw/master/versions/baseline.json"},
};

const LibSource kPySources[] = {
    {LibLang::Python, L"PyPI 官方", L"https://pypi.org/", L"https://pypi.org/simple/pip/"},
    {LibLang::Python, L"清华 TUNA", L"https://pypi.tuna.tsinghua.edu.cn/",
     L"https://pypi.tuna.tsinghua.edu.cn/simple/pip/"},
    {LibLang::Python, L"阿里云", L"https://mirrors.aliyun.com/pypi/",
     L"https://mirrors.aliyun.com/pypi/simple/pip/"},
    {LibLang::Python, L"中科大", L"https://mirrors.ustc.edu.cn/pypi/",
     L"https://mirrors.ustc.edu.cn/pypi/simple/pip/"},
    {LibLang::Python, L"华为云", L"https://repo.huaweicloud.com/repository/pypi/",
     L"https://repo.huaweicloud.com/repository/pypi/simple/pip/"},
    {LibLang::Python, L"腾讯云", L"https://mirrors.cloud.tencent.com/pypi/",
     L"https://mirrors.cloud.tencent.com/pypi/simple/pip/"},
    {LibLang::Python, L"北外开源", L"https://mirrors.bfsu.edu.cn/pypi/web/",
     L"https://mirrors.bfsu.edu.cn/pypi/web/simple/pip/"},
};

int g_selected[2] = {-1, -1};

} // namespace

const std::vector<LibSource> &AllSources(LibLang lang)
{
    static const std::vector<LibSource> cpp(std::begin(kCppSources), std::end(kCppSources));
    static const std::vector<LibSource> py(std::begin(kPySources), std::end(kPySources));
    return (lang == LibLang::Cpp) ? cpp : py;
}

int LibChooseFastest(LibLang lang, std::vector<long> &latencies)
{
    const std::vector<LibSource> &srcs = AllSources(lang);
    latencies.assign(srcs.size(), -1);

    std::vector<std::thread> workers;
    workers.reserve(srcs.size());
    for (size_t i = 0; i < srcs.size(); ++i)
    {
        const wchar_t *probe = srcs[i].probe;
        long *slot = &latencies[i];
        workers.emplace_back([probe, slot]() { *slot = NetLatency(std::wstring(probe)); });
    }
    for (std::thread &t : workers)
        t.join();

    int best = -1;
    long bestMs = 0;
    for (size_t i = 0; i < latencies.size(); ++i)
    {
        if (latencies[i] < 0)
            continue;
        if (best < 0 || latencies[i] < bestMs)
        {
            best = static_cast<int>(i);
            bestMs = latencies[i];
        }
    }
    if (best >= 0)
        g_selected[static_cast<int>(lang)] = best;
    return best;
}

int LibSelectedSource(LibLang lang)
{
    return g_selected[static_cast<int>(lang)];
}

void LibSetSelectedSource(LibLang lang, int index)
{
    g_selected[static_cast<int>(lang)] = index;
}
