/*
 * Qingxi Notepad —— Linux 启动器（Wine 承载）
 *
 * 说明：主程序是纯 Win32 自绘应用，Linux 上通过 Wine 运行。本启动器负责：
 *   1. 定位同目录下的 qingxinotepad.exe（或 ../share/qingxi-notepad/）
 *   2. 选择 wine / wine64 可执行文件
 *   3. 把 WINEPREFIX 固定到 ~/.local/share/qingxi-notepad/wine
 *   4. 透传全部命令行参数（文件路径、-portable 等）
 *
 * 交叉编译：zig cc --target=x86_64-linux-musl -O2 -static qingxi-notepad.c -o qingxi-notepad
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <libgen.h>
#include <errno.h>

static int file_exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static int has_prefix(const char *s, const char *pre) {
    return strncmp(s, pre, strlen(pre)) == 0;
}

int main(int argc, char **argv, char **envp) {
    char self[4096];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n < 0) {
        fprintf(stderr, "qingxi-notepad: 无法定位自身路径\n");
        return 1;
    }
    self[n] = '\0';

    char dirbuf[4096];
    snprintf(dirbuf, sizeof(dirbuf), "%s", self);
    char *dir = dirname(dirbuf);

    char exe[4096], alt[4096];
    snprintf(exe, sizeof(exe), "%s/qingxinotepad.exe", dir);
    snprintf(alt, sizeof(alt), "%s/../share/qingxi-notepad/qingxinotepad.exe", dir);
    if (!file_exists(exe)) {
        if (!file_exists(alt)) {
            fprintf(stderr, "qingxi-notepad: 找不到 qingxinotepad.exe（查过 %s 与 %s）\n", exe, alt);
            return 1;
        }
        snprintf(exe, sizeof(exe), "%s", alt);
    }

    const char *wine = NULL;
    for (int i = 1; i < argc; ++i) {
        if (has_prefix(argv[i], "--wine=")) { wine = argv[i] + 7; break; }
    }
    if (!wine || !file_exists(wine)) {
        if (file_exists("/usr/bin/wine")) wine = "/usr/bin/wine";
        else if (file_exists("/usr/local/bin/wine")) wine = "/usr/local/bin/wine";
        else if (file_exists("/usr/bin/wine64")) wine = "/usr/bin/wine64";
        else wine = "wine"; /* 交给 PATH 解析 */
    }

    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    char prefix[4096];
    const char *envp_prefix = getenv("WINEPREFIX");
    if (envp_prefix && envp_prefix[0]) {
        snprintf(prefix, sizeof(prefix), "%s", envp_prefix);
    } else {
        snprintf(prefix, sizeof(prefix), "%s/.local/share/qingxi-notepad/wine", home);
    }
    setenv("WINEPREFIX", prefix, 1);
    setenv("WINEDLLOVERRIDES", "mscoree,mshtml=", 1); /* 免装 Mono/Gecko 弹窗 */
    setenv("WINEDEBUG", "-all", 1);

    /* 组装参数：wine <exe> [用户参数...] */
    char **args = calloc((size_t)argc + 3, sizeof(char *));
    if (!args) return 1;
    int k = 0;
    args[k++] = (char *)wine;
    args[k++] = exe;
    for (int i = 1; i < argc; ++i) {
        if (has_prefix(argv[i], "--wine=")) continue;
        args[k++] = argv[i];
    }
    args[k] = NULL;

    execvp(wine, args);

    /* 只有 exec 失败才会走到这里 */
    int err = errno;
    fprintf(stderr,
            "qingxi-notepad: 无法启动 Wine（%s）。\n"
            "请先安装 Wine：\n"
            "  Debian/Ubuntu: sudo apt install wine64\n"
            "  Arch:          sudo pacman -S wine\n"
            "  Fedora:        sudo dnf install wine\n"
            "或使用 --wine=/path/to/wine 指定路径。\n"
            "(errno=%d)\n",
            wine, err);
    return 127;
}
