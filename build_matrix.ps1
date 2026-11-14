# 跨平台构建矩阵（Windows 多编译器 / 多架构）
#   msvc-x64   ：官方构建，静态 CRT，Win7+，用于安装包与官方绿色包
#   msvc-x86   ：32 位，Win7+ 老机器
#   mingw-x64  ：MinGW-w64 GCC 静态构建，不依赖 VC++ 运行库（系统自带 UCRT）
#   arm64      ：llvm-mingw(clang 22) 交叉编译，Windows on ARM
# 用法：pwsh -File build_matrix.ps1 -Version 1.4.5 [-Only msvc-x64]
param(
    [string]$Version = "1.4.5",
    [string]$CMake    = "D:\CMake\bin\cmake.exe",
    [string]$MinGWBin = "F:\MinGW\bin",
    [string]$LLVMMinGW = "F:\Qt\Tools\llvm-mingw2217_64\bin",
    [string]$Ninja    = "F:\Qt\Tools\Ninja\ninja.exe",
    [string[]]$Only   = @()
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$dist = Join-Path $root "dist"

function Want([string]$tag) { return ($Only.Count -eq 0 -or ($Only -contains $tag)) }
function Step([string]$m) { Write-Host "==> $m" -ForegroundColor Cyan }

function CountWarn([string]$log, [string]$pat) {
    if (-not (Test-Path -LiteralPath $log)) { return 0 }
    return (Get-Content -LiteralPath $log | Select-String -Pattern $pat | Measure-Object).Count
}

function MakePortable([string]$exe, [string]$tag) {
    $dir = Join-Path $dist "portable-$tag"
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory -Path $dir | Out-Null
    Copy-Item -LiteralPath $exe -Destination $dir
    Copy-Item -LiteralPath (Join-Path $root "LICENSE") -Destination $dir
    Copy-Item -LiteralPath (Join-Path $root "README.md") -Destination $dir
    New-Item -ItemType File -Path (Join-Path $dir "qingxinotepad.portable") | Out-Null
    $zip = Join-Path $dist ("qingxinotepad-portable-" + $Version + "-" + $tag + ".zip")
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    Compress-Archive -Path (Join-Path $dir "*") -DestinationPath $zip -CompressionLevel Optimal
    Step "$tag 绿色包: $zip  $((Get-Item -LiteralPath $zip).Length) 字节"
}

# ---------- msvc-x64 / msvc-x86 ----------
foreach ($t in @(
        @{ Tag = "msvc-x64"; Dir = "build"; Arch = $null },
        @{ Tag = "msvc-x86"; Dir = "build_win32"; Arch = "Win32" })) {
    if (-not (Want $t.Tag)) { continue }
    Step "$($t.Tag) 构建"
    $bdir = Join-Path $root $t.Dir
    if (-not (Test-Path -LiteralPath (Join-Path $bdir "CMakeCache.txt"))) {
        if ($t.Arch) {
            & $CMake -S $root -B $bdir -G "Visual Studio 18 2026" -A $t.Arch | Out-Null
        } else {
            & $CMake -S $root -B $bdir -G "Visual Studio 18 2026" | Out-Null
        }
        if ($LASTEXITCODE -ne 0) { throw "$($t.Tag) 配置失败" }
    }
    $log = Join-Path $env:TEMP "np_$($t.Tag).log"
    & $CMake --build $bdir --config Release 2>&1 | Tee-Object -FilePath $log | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "$($t.Tag) 构建失败，见 $log" }
    $exe = Join-Path $bdir "Release\qingxinotepad.exe"
    $info = (Get-Item -LiteralPath $exe).VersionInfo
    Step "$($t.Tag): $((Get-Item -LiteralPath $exe).Length) 字节  警告=$(CountWarn $log 'warning C')  ver=$($info.FileVersion)"
    if ($t.Tag -eq "msvc-x86") { MakePortable $exe "win32-x86" }
}

# ---------- mingw-x64 ----------
if (Want "mingw-x64") {
    Step "mingw-x64 构建"
    $env:PATH = "$MinGWBin;$env:PATH"
    $bdir = Join-Path $root "build_mingw"
    if (-not (Test-Path -LiteralPath (Join-Path $bdir "CMakeCache.txt"))) {
        & $CMake -S $root -B $bdir -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release `
            "-DCMAKE_C_COMPILER=$($MinGWBin.Replace('\','/'))/gcc.exe" `
            "-DCMAKE_CXX_COMPILER=$($MinGWBin.Replace('\','/'))/g++.exe" | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "mingw-x64 配置失败" }
    }
    $log = Join-Path $env:TEMP "np_mingw-x64.log"
    & $CMake --build $bdir -j ([Environment]::ProcessorCount) 2>&1 | Tee-Object -FilePath $log | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "mingw-x64 构建失败，见 $log" }
    Step "mingw-x64 警告=$(CountWarn $log 'warning:')"
    MakePortable (Join-Path $bdir "qingxinotepad.exe") "mingw-x64"
}

# ---------- arm64（llvm-mingw 交叉） ----------
if (Want "arm64") {
    Step "arm64 构建（llvm-mingw clang 交叉）"
    if (-not (Test-Path -LiteralPath (Join-Path $LLVMMinGW "aarch64-w64-mingw32-g++.exe"))) { throw "找不到 $LLVMMinGW\aarch64-w64-mingw32-g++.exe" }
    $env:PATH = "$LLVMMinGW;$(Split-Path $Ninja);$env:PATH"
    $bdir = Join-Path $root "build_aarch64"
    $lg = $LLVMMinGW.Replace('\', '/')
    if (-not (Test-Path -LiteralPath (Join-Path $bdir "CMakeCache.txt"))) {
        & $CMake -S $root -B $bdir -G Ninja "-DCMAKE_MAKE_PROGRAM=$($Ninja.Replace('\','/'))" -DCMAKE_BUILD_TYPE=Release `
            "-DCMAKE_CXX_COMPILER=$lg/aarch64-w64-mingw32-g++.exe" `
            "-DCMAKE_C_COMPILER=$lg/aarch64-w64-mingw32-gcc.exe" `
            "-DCMAKE_RC_COMPILER=$lg/aarch64-w64-mingw32-windres.exe" | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "arm64 配置失败" }
    }
    $log = Join-Path $env:TEMP "np_arm64.log"
    & $CMake --build $bdir 2>&1 | Tee-Object -FilePath $log | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "arm64 构建失败，见 $log" }
    Step "arm64 警告=$(CountWarn $log 'warning:')"
    MakePortable (Join-Path $bdir "qingxinotepad.exe") "arm64"
}

Step "矩阵构建完成（版本 $Version）"
