# Linux 包构建（在 Windows 上完成，产物可直接在 Linux 使用）
#   主程序：复用对应架构的 Windows 构建（Wine 承载，Win32 内核零改动）
#   启动器：POSIX shell（默认）；如提供 zig 则改为静态 ELF 启动器（x86_64/arm64 均可）
#   产物：dist\qingxinotepad-linux-<arch>-<ver>.tar.gz（含 install.sh 与 .desktop）
#
# 用法：
#   pwsh -File packaging\linux\pack_linux.ps1 -Version 1.4.5                 # 全部架构
#   pwsh -File packaging\linux\pack_linux.ps1 -Version 1.4.5 -Arch arm64     # 只出一个
#   pwsh -File packaging\linux\pack_linux.ps1 -Version 1.4.5 -Zig F:\zig\zig.exe
param(
    [string]$Version = "1.4.6",
    [string]$Arch    = "all",
    [string]$Zig     = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot) -Parent
$dist = Join-Path $root "dist"
$utf8 = New-Object Text.UTF8Encoding $false

# Linux 架构 -> 使用的 Windows 构建（Wine 加载）
$map = @(
    @{ Arch = "x86_64"; WinExe = "dist\mingw-x64\qingxinotepad.exe";           ZigArch = "x86_64-linux-musl"; Label = "Linux x86_64（Wine + MinGW-x64 exe）" },
    @{ Arch = "arm64";  WinExe = "build_aarch64\qingxinotepad.exe";            ZigArch = "aarch64-linux-musl"; Label = "Linux arm64（Wine + 原生 ARM64 exe）" },
    @{ Arch = "i686";   WinExe = "dist\portable-win32-x86\qingxinotepad.exe";  ZigArch = "x86-linux-musl"; Label = "Linux i686（Wine + MSVC x86 exe）" }
)

function WriteLf([string]$src, [string]$dst) {
    $t = [IO.File]::ReadAllText($src).Replace("`r`n", "`n")
    [IO.File]::WriteAllText($dst, $t, $utf8)
}

foreach ($m in $map) {
    if ($Arch -ne "all" -and $Arch -ne $m.Arch) { continue }
    $exeSrc = Join-Path $root $m.WinExe
    if (-not (Test-Path -LiteralPath $exeSrc)) { Write-Host "==> 跳过 $($m.Arch)：缺少 $exeSrc" -ForegroundColor Yellow; continue }

    $pkg = Join-Path $dist ("linux-" + $m.Arch)
    if (Test-Path -LiteralPath $pkg) { Remove-Item -LiteralPath $pkg -Recurse -Force }
    New-Item -ItemType Directory -Path $pkg | Out-Null

    # 1. 启动器
    $launcher = Join-Path $pkg "qingxi-notepad"
    WriteLf (Join-Path $PSScriptRoot "qingxi-notepad.sh") $launcher
    $mode = "shell"
    if ($Zig -and (Test-Path -LiteralPath $Zig)) {
        $elf = Join-Path $pkg "qingxi-notepad.bin"
        & $Zig cc "--target=$($m.ZigArch)" -O2 -static -s (Join-Path $PSScriptRoot "qingxi-notepad.c") -o $elf 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $elf)) {
            Remove-Item -LiteralPath $launcher -Force
            Rename-Item -LiteralPath $elf -NewName "qingxi-notepad"
            $mode = "elf($($m.ZigArch))"
        }
    }

    # 2. 组装
    Copy-Item -LiteralPath $exeSrc -Destination $pkg
    Copy-Item -LiteralPath (Join-Path $root "LICENSE") -Destination $pkg
    Copy-Item -LiteralPath (Join-Path $root "README.md") -Destination $pkg
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "qingxi-notepad.desktop") -Destination $pkg
    WriteLf (Join-Path $PSScriptRoot "install.sh") (Join-Path $pkg "install.sh")

    # 3. 打包（python tarfile 写入正确权限位）
    $tarball = Join-Path $dist ("qingxinotepad-linux-" + $m.Arch + "-" + $Version + ".tar.gz")
    $folder = "qingxinotepad-linux-$($m.Arch)-$Version"
    if (Test-Path -LiteralPath $tarball) { Remove-Item -LiteralPath $tarball -Force }
    $py = (Get-Command python -ErrorAction Stop).Source
    & $py (Join-Path $PSScriptRoot "make_tar.py") $pkg $folder $tarball | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "$($m.Arch) 打包失败" }

    $size = (Get-Item -LiteralPath $tarball).Length
    Write-Host "==> $($m.Arch): 启动器=$mode  包=$tarball  $size 字节" -ForegroundColor Cyan
}

Write-Host "==> Linux 包构建完成（版本 $Version）" -ForegroundColor Green
