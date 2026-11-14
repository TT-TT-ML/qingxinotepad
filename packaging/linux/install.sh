#!/bin/sh
# Qingxi Notepad —— Linux 安装脚本（Wine 承载）
# 用法：./install.sh            装到系统（/usr/local，需要 sudo）
#       ./install.sh --user     只装给当前用户（~/.local，不需要 root）
set -e

if [ "$1" = "--user" ]; then
    PREFIX="${HOME}/.local"
else
    PREFIX="/usr/local"
    if [ "$(id -u)" -eq 0 ]; then :; else
        if command -v sudo >/dev/null 2>&1; then SUDO="sudo"; else
            echo "需要 root 权限，或改用 ./install.sh --user"; exit 1
        fi
    fi
fi

DIR="$(cd "$(dirname "$0")" && pwd)"
RUN() { if [ -n "$SUDO" ]; then $SUDO "$@"; else "$@"; fi; }

RUN mkdir -p "${PREFIX}/bin" "${PREFIX}/share/qingxi-notepad" "${PREFIX}/share/applications"
RUN install -m 755 "${DIR}/qingxi-notepad" "${PREFIX}/bin/qingxi-notepad"
RUN install -m 644 "${DIR}/qingxinotepad.exe" "${PREFIX}/share/qingxi-notepad/qingxinotepad.exe"
RUN install -m 644 "${DIR}/LICENSE" "${PREFIX}/share/qingxi-notepad/LICENSE"

# 桌面项里指向 bin/qingxi-notepad
sed "s|^Exec=qingxi-notepad|Exec=${PREFIX}/bin/qingxi-notepad|" \
    "${DIR}/qingxi-notepad.desktop" > /tmp/qingxi-notepad.desktop
RUN install -m 644 /tmp/qingxi-notepad.desktop "${PREFIX}/share/applications/qingxi-notepad.desktop"
rm -f /tmp/qingxi-notepad.desktop

if ! command -v wine >/dev/null 2>&1 && ! command -v wine64 >/dev/null 2>&1; then
    echo ""
    echo "提示：未检测到 Wine。程序通过 Wine 运行，请先安装："
    echo "  Debian/Ubuntu: sudo apt install wine64"
    echo "  Arch:          sudo pacman -S wine"
    echo "  Fedora:        sudo dnf install wine"
fi

echo "安装完成：运行 qingxi-notepad 即可（或从应用菜单启动）。"
echo "卸载：删除 ${PREFIX}/bin/qingxi-notepad 与 ${PREFIX}/share/qingxi-notepad"
