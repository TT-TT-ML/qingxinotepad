#!/bin/sh
# Qingxi Notepad —— Linux 启动器（Wine 承载 Win32 主程序）
#
# 逻辑：
#   1. 找到同目录的 qingxinotepad.exe（安装后可能在 ../share/qingxi-notepad/）
#   2. 选 wine / wine64
#   3. WINEPREFIX 固定到 ~/.local/share/qingxi-notepad/wine，避免污染默认 prefix
#   4. 透传全部参数（文件路径、-portable 等）；支持 QINGXI_WINE=/path/to/wine
set -u

SELF="$0"
# 解析软链，拿到真实目录
while [ -h "$SELF" ]; do
    LINK="$(readlink "$SELF")"
    case "$LINK" in
        /*) SELF="$LINK" ;;
        *)  SELF="$(dirname "$SELF")/$LINK" ;;
    esac
done
DIR="$(cd "$(dirname "$SELF")" && pwd)"

EXE=""
for c in "$DIR/qingxinotepad.exe" \
         "$DIR/../share/qingxi-notepad/qingxinotepad.exe" \
         "/usr/local/share/qingxi-notepad/qingxinotepad.exe" \
         "$HOME/.local/share/qingxi-notepad/qingxinotepad.exe"; do
    if [ -f "$c" ]; then EXE="$c"; break; fi
done
if [ -z "$EXE" ]; then
    echo "qingxi-notepad: 找不到 qingxinotepad.exe" >&2
    exit 1
fi

WINE="${QINGXI_WINE:-}"
if [ -z "$WINE" ]; then
    for c in wine wine64 "/usr/bin/wine" "/usr/bin/wine64" "/usr/local/bin/wine"; do
        if command -v "$c" >/dev/null 2>&1; then WINE="$(command -v "$c")"; break; fi
    done
fi
if [ -z "$WINE" ]; then
    cat >&2 <<'EOF'
qingxi-notepad: 未找到 Wine。请先安装：
  Debian/Ubuntu: sudo apt install wine64
  Arch:          sudo pacman -S wine
  Fedora:        sudo dnf install wine
也可用环境变量指定：QINGXI_WINE=/path/to/wine qingxi-notepad
EOF
    exit 127
fi

# 免 Mono/Gecko 弹窗，日志关掉
WINEPREFIX="${WINEPREFIX:-$HOME/.local/share/qingxi-notepad/wine}"
export WINEPREFIX
export WINEDLLOVERRIDES="mscoree,mshtml="
export WINEDEBUG="-all"

# 首次运行自动初始化 prefix（静默，避免卡在对话框）
if [ ! -d "$WINEPREFIX" ]; then
    mkdir -p "$WINEPREFIX" 2>/dev/null || true
    "$WINE" wineboot --init >/dev/null 2>&1 || true
    "$WINE" wineserver -w >/dev/null 2>&1 || true
fi

exec "$WINE" "$EXE" "$@"
