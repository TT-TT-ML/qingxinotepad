# 一键：编译 Release → 生成单文件安装包 → 更新绿色版 zip
# 用法：
#   pwsh -File build_and_pack.ps1                 # 默认 1.3.1
#   pwsh -File build_and_pack.ps1 -Version 1.2.1  # 出新版本
#   pwsh -File build_and_pack.ps1 -SkipBuild      # 不重新编译，只重打包
param(
    [string]$Version = "1.4.4",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$root      = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir  = Join-Path $root "build"
$appExe    = Join-Path $buildDir "Release\qingxinotepad.exe"
$dist      = Join-Path $root "dist"
$srcDir    = Join-Path $dist "install_src"
$portDir   = Join-Path $dist "portable"
$logDir    = Join-Path $dist "_build"
$cmake     = "D:\CMake\bin\cmake.exe"

# 新版 Builder 会把中间产物放在 %TEMP%，旧版（Program Files 里那个）会往
# Program Files 写，非管理员必失败，所以优先用源码目录里的新版。
$builder = @(
    "E:\codes\installer\dist\Builder\Builder.exe",
    "C:\Program Files\安装包构建器\Builder.exe"
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1

if (-not $builder) { throw "找不到 Builder.exe" }
New-Item -ItemType Directory -Force -Path $dist, $logDir | Out-Null

function Step($text) { Write-Host ("==> " + $text) }

# 1. 编译
if (-not $SkipBuild) {
    Step "停止正在运行的 qingxinotepad"
    Get-Process -Name qingxinotepad -ErrorAction SilentlyContinue | Stop-Process -Force

    if (-not (Test-Path -LiteralPath (Join-Path $buildDir "CMakeCache.txt"))) {
        Step "首次配置 CMake"
        & $cmake -S $root -B $buildDir -G "Visual Studio 18 2026" -A x64 2>&1 |
            Out-File -FilePath (Join-Path $logDir "configure.log") -Encoding utf8
        if ($LASTEXITCODE -ne 0) { throw "CMake 配置失败，见 dist\_build\configure.log" }
    }

    Step "编译 Release"
    (& $cmake --build $buildDir --config Release) 2>&1 |
        Out-File -FilePath (Join-Path $logDir "build.log") -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw "编译失败，见 dist\_build\build.log" }

    $warn = (Select-String -LiteralPath (Join-Path $logDir "build.log") -Pattern "warning C" |
        Measure-Object).Count
    Write-Host ("    编译完成，warning C 计数: " + $warn)
}

if (-not (Test-Path -LiteralPath $appExe)) { throw "找不到 $appExe" }
Step ("使用程序: " + $appExe + "  (" + (Get-Item -LiteralPath $appExe).Length + " 字节)")

# 2. 组装安装源目录
Step "组装安装源目录 dist\install_src"
Remove-Item -LiteralPath $srcDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $srcDir | Out-Null
Copy-Item -LiteralPath $appExe -Destination $srcDir -Force
Copy-Item -LiteralPath (Join-Path $root "LICENSE") -Destination (Join-Path $srcDir "LICENSE.txt") -Force
Copy-Item -LiteralPath (Join-Path $root "README.md") -Destination $srcDir -Force
Copy-Item -LiteralPath (Join-Path $root "src\icon.ico") -Destination $srcDir -Force

# 3. 调用构建器命令行打包（不弹窗、不点鼠标）
Step "调用安装包构建器（CLI 模式）"
# 可选的文件关联扩展名：本项目支持的全部语言扩展名（与 src/modules/syntax.cpp
# 的 LanguageFromPath 一一对应）。安装页会把这些列出来由用户勾选，只有勾选的
# 才会在安装时写进当前用户的默认打开方式。
$assocExts = ".cpp;.h;.c;.cc;.cxx;.hpp;.hxx;.hh;.ipp;.inc;.cu;.cuh;.cs;.java;" +
             ".js;.mjs;.cjs;.jsx;.ts;.tsx;.py;.pyw;.html;.htm;.xhtml;.vue;" +
             ".css;.scss;.sass;.less;.php;.phtml;.go;.rs;.lua;.sql;.sh;.bash;.zsh;" +
             ".bat;.cmd;.json;.xml;.xaml;.svg;.csproj;.vcxproj;.slnx;.manifest;" +
             ".resx;.xsd;.xsl;.md;.markdown;.yml;.yaml;.ini;.conf;.cfg;.properties;" +
             ".env;.gitconfig;.toml;.rb;.erb;.rake;.swift;.kt;.kts;.gradle;.dart;" +
             ".ps1;.psm1;.psd1;.pl;.pm;.r;.hs;.lhs;.scala;.sc;.groovy;.gvy;.m;.mm;" +
             ".vb;.vbs;.cmake;.pro;.pri;.qrc;.qss;.rc;.mk;.mak;.dockerfile;" +
             ".containerfile;.proto"
# 简介用字面 \n（构建器 CLI 会把它转成换行），避免命令行里出现真换行
$desc = 'Qingxi Notepad —— 轻量快速的文本与代码编辑器。\n支持 C++/Python/JavaScript 语法高亮、自动补全、库下载与一键编译。'
$cliLog = Join-Path $logDir "builder_cli.log"
Remove-Item -LiteralPath $cliLog -Force -ErrorAction SilentlyContinue
# Builder 是 GUI 子系统程序：直接 & 调用会立刻返回，必须用 -Wait 等它写完。
# 参数带空格必须自己加引号，Start-Process 不会替你转义。
$argLine = '-cli -dir "{0}" -main "qingxinotepad.exe" -name "qingxinotepad" -ver "{1}" -desc "{2}" -icon "icon.ico" -assoc "{3}" -assocprog "qingxinotepad.exe"' -f $srcDir, $Version, $desc, $assocExts
$proc = Start-Process -FilePath $builder -WorkingDirectory $logDir -Wait -PassThru -NoNewWindow -ArgumentList $argLine
$code = $proc.ExitCode
for ($i = 0; $i -lt 20 -and -not (Test-Path -LiteralPath $cliLog); $i++) { Start-Sleep -Milliseconds 200 }

if (-not (Test-Path -LiteralPath $cliLog)) { throw "构建器没有写日志，退出码 $code" }
$msg = [IO.File]::ReadAllText($cliLog, [Text.Encoding]::UTF8)
if ($msg -notmatch "^OK") { throw ("打包失败: " + $msg.Trim()) }

$deskExe = Join-Path $env:USERPROFILE ("Desktop\qingxinotepad_install.exe")
if (-not (Test-Path -LiteralPath $deskExe)) { throw "桌面没有生成的安装包" }
Copy-Item -LiteralPath $deskExe -Destination (Join-Path $dist "qingxinotepad_install.exe") -Force

# 4. 绿色版（配置跟着 exe 走）
Step "刷新绿色便携版 dist\portable"
Remove-Item -LiteralPath $portDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $portDir | Out-Null
Copy-Item -LiteralPath $appExe -Destination $portDir -Force
Copy-Item -LiteralPath (Join-Path $root "LICENSE") -Destination (Join-Path $portDir "LICENSE.txt") -Force
Copy-Item -LiteralPath (Join-Path $root "README.md") -Destination $portDir -Force
Copy-Item -LiteralPath (Join-Path $root "src\icon.ico") -Destination $portDir -Force
New-Item -ItemType File -Force -Path (Join-Path $portDir "qingxinotepad.portable") | Out-Null

$zip = Join-Path $dist ("qingxinotepad-portable-" + $Version + ".zip")
Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $portDir "*") -DestinationPath $zip -CompressionLevel Optimal

# 5. 结果
$inst = Get-Item -LiteralPath (Join-Path $dist "qingxinotepad_install.exe")
Write-Host ""
Write-Host "完成"
Write-Host ("  安装包 : " + $inst.FullName + "  " + $inst.Length + " 字节")
Write-Host ("  绿色版 : " + (Join-Path $portDir "") + "   → " + $zip)
Write-Host ("  版本号 : " + $Version)
