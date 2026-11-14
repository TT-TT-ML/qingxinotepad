# Linux 包构建（在 Windows 上完成，产物可直接在 Linux 使用）
#   主程序：复用 MinGW-x64 的 qingxinotepad.exe（Wine 承载，纯 Win32 代码零改动）
#   启动器：POSIX shell（无交叉编译依赖；如提供 zig 则改用静态 ELF 启动器）
#   产物：dist\qingxinotepad-linux-x86_64-<ver>.tar.gz（含 install.sh 与 .desktop）
# 用法：pwsh -File packaging\linux\pack_linux.ps1 -Version 1.4.5
#       pwsh -File packaging\linux\pack_linux.ps1 -Version 1.4.5 -Zig F:\zig\zig.exe   # 可选：ELF 启动器
param(
    [string]$Version = "1.4.5",
    [string]$Zig     = "",
    [string]$ExeWin  = "C:\work\qingxinotepad\dist\mingw-x64\qingxinotepad.exe"
)
$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot) -Parent
$dist = Join-Path $root "dist"
$pkg  = Join-Path $dist "linux-x86_64"
$utf8 = New-Object Text.UTF8Encoding $false

function WriteLf([string]$src, [string]$dst) {
    $t = [IO.File]::ReadAllText($src).Replace("`r`n", "`n")
    [IO.File]::WriteAllText($dst, $t, $utf8)
}

if (-not (Test-Path -LiteralPath $ExeWin)) {
    $fallback = Join-Path $root "build_mingw\qingxinotepad.exe"
    if (Test-Path -LiteralPath $fallback) { $ExeWin = $fallback } else { throw "找不到 qingxinotepad.exe: $ExeWin" }
}

if (Test-Path -LiteralPath $pkg) { Remove-Item -LiteralPath $pkg -Recurse -Force }
New-Item -ItemType Directory -Path $pkg | Out-Null

# 1. 启动器：默认 shell；给了 zig 就编静态 ELF
$launcherShell = Join-Path $pkg "qingxi-notepad"
WriteLf (Join-Path $PSScriptRoot "qingxi-notepad.sh") $launcherShell
$mode = "shell"
if ($Zig -and (Test-Path -LiteralPath $Zig)) {
    $elf = Join-Path $pkg "qingxi-notepad.bin"
    & $Zig cc --target=x86_64-linux-musl -O2 -static -s (Join-Path $PSScriptRoot "qingxi-notepad.c") -o $elf 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $elf)) {
        Remove-Item -LiteralPath $launcherShell -Force
        Rename-Item -LiteralPath $elf -NewName "qingxi-notepad"
        $mode = "elf"
    }
}

# 2. 组装文件
Copy-Item -LiteralPath $ExeWin -Destination $pkg
Copy-Item -LiteralPath (Join-Path $root "LICENSE") -Destination $pkg
Copy-Item -LiteralPath (Join-Path $root "README.md") -Destination $pkg
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "qingxi-notepad.desktop") -Destination $pkg
WriteLf (Join-Path $PSScriptRoot "install.sh") (Join-Path $pkg "install.sh")

# 3. 打包（tar --mode 保证 Unix 权限：启动器与 install.sh 755，其余 644）
$tarball = Join-Path $dist ("qingxinotepad-linux-x86_64-" + $Version + ".tar.gz")
$folder = "qingxinotepad-linux-x86_64-$Version"
if (Test-Path -LiteralPath $tarball) { Remove-Item -LiteralPath $tarball -Force }

# 用 python 打包：Windows 侧无法保证 Unix 权限位，由 tarfile 显式写入
$py = (Get-Command python -ErrorAction Stop).Source
& $py (Join-Path $PSScriptRoot "make_tar.py") $pkg $folder $tarball
if ($LASTEXITCODE -ne 0) { throw "python 打包失败" }

$size = (Get-Item -LiteralPath $tarball).Length
Write-Host "==> 启动器类型: $mode" -ForegroundColor Cyan
Write-Host "==> Linux 包: $tarball  $size 字节" -ForegroundColor Cyan
