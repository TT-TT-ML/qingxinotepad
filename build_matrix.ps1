# 跨平台构建矩阵（Windows 多编译器 / 多版本）
#   - MSVC x64   ：默认构建，用于安装包与官方绿色包（依赖静态 CRT，Win7+ 可用）
#   - MinGW x64  ：GCC 静态构建，用于免安装镜像与 Linux(Wine) 包（依赖系统 UCRT，Win10+ 自带）
# 用法：pwsh -File build_matrix.ps1 -Version 1.4.3 [-SkipMSVC]
param(
    [string]$Version = "1.4.4",
    [string]$CMake = "D:\CMake\bin\cmake.exe",
    [string]$MinGWBin = "F:\MinGW\bin",
    [switch]$SkipMSVC,
    [switch]$SkipMinGW
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$dist = Join-Path $root "dist"

function Step([string]$msg) { Write-Host "==> $msg" -ForegroundColor Cyan }

function CountWarnings([string]$logPath) {
    $lines = Get-Content -LiteralPath $logPath -ErrorAction SilentlyContinue
    ($lines | Select-String -Pattern "warning C|warning: " | Measure-Object).Count
}

# ---------- 1. MSVC x64 ----------
if (-not $SkipMSVC) {
    Step "MSVC x64 构建"
    $log = Join-Path $env:TEMP "np_msvc_build.log"
    & $CMake --build (Join-Path $root "build") --config Release 2>&1 | Tee-Object -FilePath $log | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "MSVC 构建失败，见 $log" }
    $w = CountWarnings $log
    Step "MSVC 警告数: $w"
    $exe = Join-Path $root "build\Release\qingxinotepad.exe"
    $info = (Get-Item -LiteralPath $exe).VersionInfo
    Step "MSVC 产物: $exe  $((Get-Item -LiteralPath $exe).Length) 字节  ver=$($info.FileVersion)  product=$($info.ProductName)"
}

# ---------- 2. MinGW x64 ----------
if (-not $SkipMinGW) {
    Step "MinGW x64 构建"
    if (-not (Test-Path -LiteralPath (Join-Path $MinGWBin "g++.exe"))) { throw "找不到 $MinGWBin\g++.exe" }
    $env:PATH = "$MinGWBin;$env:PATH"
    $bdir = Join-Path $root "build_mingw"
    $cfg = Join-Path $bdir "CMakeCache.txt"
    if (-not (Test-Path -LiteralPath $cfg)) {
        & $CMake -S $root -B $bdir -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release `
            -DCMAKE_C_COMPILER="$MinGWBin/gcc.exe".Replace("\", "/") `
            -DCMAKE_CXX_COMPILER="$MinGWBin/g++.exe".Replace("\", "/") | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "MinGW 配置失败" }
    }
    $log = Join-Path $env:TEMP "np_mingw_build.log"
    & $CMake --build $bdir -j ([Environment]::ProcessorCount) 2>&1 | Tee-Object -FilePath $log | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "MinGW 构建失败，见 $log" }
    Step "MinGW 警告数: $(CountWarnings $log)"

    $exe = Join-Path $bdir "qingxinotepad.exe"
    if (-not (Test-Path -LiteralPath $exe)) { throw "MinGW 产物缺失: $exe" }

    # 绿色版目录（MinGW 版）：exe + 便携标记 + LICENSE
    $portDir = Join-Path $dist "portable-mingw-x64"
    if (Test-Path -LiteralPath $portDir) { Remove-Item -LiteralPath $portDir -Recurse -Force }
    New-Item -ItemType Directory -Path $portDir | Out-Null
    Copy-Item -LiteralPath $exe -Destination $portDir
    Copy-Item -LiteralPath (Join-Path $root "LICENSE") -Destination $portDir
    Copy-Item -LiteralPath (Join-Path $root "README.md") -Destination $portDir
    New-Item -ItemType File -Path (Join-Path $portDir "qingxinotepad.portable") | Out-Null

    $zip = Join-Path $dist ("qingxinotepad-portable-" + $Version + "-mingw-x64.zip")
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    Compress-Archive -Path (Join-Path $portDir "*") -DestinationPath $zip -CompressionLevel Optimal
    Step "MinGW 绿色包: $zip  $((Get-Item -LiteralPath $zip).Length) 字节"

    # 供 Linux(Wine) 包复用的裸 exe
    $raw = Join-Path $dist "mingw-x64\qingxinotepad.exe"
    New-Item -ItemType Directory -Path (Split-Path $raw) -Force | Out-Null
    Copy-Item -LiteralPath $exe -Destination $raw -Force
    Step "MinGW 裸 exe: $raw  $((Get-Item -LiteralPath $exe).Length) 字节"
}

Step "矩阵构建完成（版本 $Version）"
