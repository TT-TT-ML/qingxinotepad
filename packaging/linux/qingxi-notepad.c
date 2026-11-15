/*
 * Qingxi Notepad —— Linux 启动器（Wine 承载）
 *
 * 说明：主程序是纯 Win32 自绘应用，Linux 上通过 Wine 运行。本启动器负责：
 *   1. 定位同目录下的 qingxinotepad.exe（或 ../share/qingxi-notepad/）
 *   2. 选择 wine / wine64 可执行文件（QINGXI_WINE 或 --wine= 可覆盖）
 *   3. 建立 WINEPREFIX（默认 ~/.local/share/qingxi-notepad/wine），首次运行 wine --init
 *   4. 透传全部命令行参数（文件路径、-portable 等）
 *
 * 交叉编译（Windows 上用 zig，无需 Linux sysroot）：
 *   zig cc --target=x86_64-linux-musl  -O2 -static -s qingxi-notepad.c -o qingxi-notepad
 *   zig cc --target=aarch64-linux-musl -O2 -static -s qingxi-notepad.c -o qingxi-notepad
 *   zig cc --target=x86-linux-musl     -O2 -static -s qingxi-notepad.c -o qingxi-notepad
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

static int dir_exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int has_prefix(const char *s, const char *pre) {
    return strncmp(s, pre, strlen(pre)) == 0;
}

/* mkdir -p，失败返回 -1 */
static int mkdir_p(const char *path) {
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len == 0) return -1;
    for (size_t i = 1; i < len; ++i) {
        if (tmp[i] != '/') continue;
        tmp[i] = '\0';
        if (!dir_exists(tmp) && mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
        tmp[i] = '/';
    }
    if (!dir_exists(tmp) && mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

/* 执行 cmd [arg]，等它结束；用于 wineboot 初始化 */
static int run_wait(const char *cmd, const char *arg) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (arg) execlp(cmd, cmd, arg, (char *)NULL);
        else execlp(cmd, cmd, (char *)NULL);
        _exit(127);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static const char *wine_candidates[] = {
    "/usr/bin/wine", "/usr/local/bin/wine", "/usr/bin/wine64",
    "/usr/local/bin/wine64", "/opt/wine-stable/bin/wine", NULL
};

int main(int argc, char **argv) {
    char self[4096];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n <= 0) {
        if (!argv[0] || !argv[0][0]) {
            fprintf(stderr, "qingxi-notepad: 无法定位自身路径\n");
            return 1;
        }
        snprintf(self, sizeof(self), "%s", argv[0]);
        n = (ssize_t)strlen(self);
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
            fprintf(stderr, "qingxi-notepad: 找不到 qingxinotepad.exe\n查过：%s\n      %s\n", exe, alt);
            return 1;
        }
        snprintf(exe, sizeof(exe), "%s", alt);
    }

    /* Wine：QINGXI_WINE > --wine= > 常见路径 > PATH */
    const char *wine = NULL;
    const char *env_wine = getenv("QINGXI_WINE");
    if (env_wine && env_wine[0] && file_exists(env_wine)) wine = env_wine;
    for (int i = 1; i < argc && !wine; ++i) {
        if (has_prefix(argv[i], "--wine=") && file_exists(argv[i] + 7)) wine = argv[i] + 7;
    }
    if (!wine) {
        for (int i = 0; wine_candidates[i]; ++i) {
            if (file_exists(wine_candidates[i])) { wine = wine_candidates[i]; break; }
        }
    }
    if (!wine) wine = "wine"; /* 交给 PATH 解析；失败时给出安装提示 */

    /* WINEPREFIX：环境变量 > XDG_DATA_HOME > ~/.local/share > ~/.qingxi-notepad
     * 逐级尝试创建，某级不可写（例如 ~/.local/share 属 root）时自动降级 */
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    char prefix[4096];
    const char *env_prefix = getenv("WINEPREFIX");
    int own_prefix = 0;
    if (env_prefix && env_prefix[0]) {
        snprintf(prefix, sizeof(prefix), "%s", env_prefix);
        own_prefix = 1;
    } else {
        const char *xdg = getenv("XDG_DATA_HOME");
        char c1[4096] = "", c2[4096], c3[4096];
        snprintf(c2, sizeof(c2), "%s/.local/share/qingxi-notepad/wine", home);
        snprintf(c3, sizeof(c3), "%s/.qingxi-notepad/wine", home);
        int got = 0;
        if (xdg && xdg[0]) {
            snprintf(c1, sizeof(c1), "%s/qingxi-notepad/wine", xdg);
            if (mkdir_p(c1) == 0) { snprintf(prefix, sizeof(prefix), "%s", c1); got = 1; }
        }
        if (!got && mkdir_p(c2) == 0) { snprintf(prefix, sizeof(prefix), "%s", c2); got = 1; }
        if (!got && mkdir_p(c3) == 0) { snprintf(prefix, sizeof(prefix), "%s", c3); got = 1; }
        if (!got) {
            fprintf(stderr,
                    "qingxi-notepad: 无法创建 WINEPREFIX（已尝试 %s%s%s 与 %s）\n"
                    "请用 WINEPREFIX=/可写路径 指定。\n",
                    c1[0] ? c1 : "", c1[0] ? "、" : "", c2, c3);
            return 1;
        }
        own_prefix = 1;
    }
    setenv("WINEPREFIX", prefix, 1);
    setenv("WINEDLLOVERRIDES", "mscoree,mshtml=", 1); /* 免装 Mono/Gecko 弹窗 */
    setenv("WINEDEBUG", "-all", 1);

    if (own_prefix) {
        if (mkdir_p(prefix) != 0) {
            fprintf(stderr, "qingxi-notepad: 无法创建 WINEPREFIX 目录：%s（errno=%d）\n", prefix, errno);
            return 1;
        }
        char reg[4096];
        snprintf(reg, sizeof(reg), "%s/system.reg", prefix);
        if (!file_exists(reg)) {
            fprintf(stderr, "qingxi-notepad: 首次运行，正在初始化 Wine 环境（约 30 秒）...\n");
            run_wait(wine, "--init"); /* 等价于 wineboot --init */
        }
    }

    /* wine <exe> [用户参数...] */
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

    execv(wine, args);   /* 绝对路径直接执行 */
    execvp(wine, args);  /* 依赖 PATH 时兜底 */

    int err = errno;
    fprintf(stderr,
            "qingxi-notepad: 未找到 Wine（%s）。\n"
            "请先安装：\n"
            "  Debian/Ubuntu: sudo apt install wine64\n"
            "  Arch:          sudo pacman -S wine\n"
            "  Fedora:        sudo dnf install wine\n"
            "也可用环境变量指定：QINGXI_WINE=/path/to/wine qingxi-notepad\n"
            "(errno=%d)\n",
            wine, err);
    return 127;
}
