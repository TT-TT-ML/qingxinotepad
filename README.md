# 晴溪notepad · Qingxin Notepad 1.4.2

轻量、原生的 Windows 文本 / 代码编辑器，用 C++17 + Win32 API 手写，**界面全部自绘**，不依赖 RichEdit / Qt / .NET，单文件 exe 无第三方运行时。

A lightweight native Windows text & code editor written in C++17 with the Win32 API — fully self-drawn UI, no RichEdit, no Qt, no .NET, one exe and no third-party runtime.

---

## 下载

| 渠道 | 地址 |
| --- | --- |
| 官方下载中心 | <https://ttqx.cc.cd/d/> → 「晴溪notepad文本编辑器 / 1.4.2」 |
| 安装版直链 | <https://ttqx.cc.cd/d/get/%E6%99%B4%E6%BA%AAnotepad%E6%96%87%E6%9C%AC%E7%BC%96%E8%BE%91%E5%99%A8/1.4.2/qingxinotepad_install.exe> |
| 绿色版直链 | <https://ttqx.cc.cd/d/get/%E6%99%B4%E6%BA%AAnotepad%E6%96%87%E6%9C%AC%E7%BC%96%E8%BE%91%E5%99%A8/1.4.2/qingxinotepad-portable-1.4.2.zip> |
| 源码 | 本仓库（`main` 分支） |

- **安装版**：单个 `qingxinotepad_install.exe`，安装时可选要注册的文件关联扩展名。
- **绿色版**：qingxinotepad-portable-1.4.0-r2.zip 解压后直接运行 `qingxinotepad.exe`，配置写在 exe 同目录的 `qingxinotepad.ini`，不往系统里写任何东西。

## 功能

**编辑与排版**
- 自绘编辑器控件：按单词软换行、折行块行号居中 + GDI 中括号、列选/行选中、光标逐字素移动。
- 软换行分词规则：**空格之外，任何标点、运算符、符号都能断开单词**；连续字母数字串算一个词，行尾放不下整体下移；**超过 15 个字符的串不再算单词**（URL、压缩代码），行尾允许硬断。
- **中日韩按「一字即一词」处理**：汉字、假名、韩文、全角形式在行尾逐字断开，不再整段搬家。
- 中文 / 阿拉伯文等宽字符按 2 列布局，混合排版与 RTL 菜单可用。
- 按 `Down` 在折行块内直接跳到**下一逻辑行**（折行行属于同一行，不再逐视觉行爬）。
- 自动换行是可选开关，在**格式 → 自动换行**（`Alt+W`）里勾选，状态记在注册表 `Software\LegacyNotepad\WordWrap`。

**代码能力**
- 语法高亮：C/C++、Python、JavaScript、HTML/CSS 等，含括号配对标记。
- **自动括号配对**：`()` `[]` `{}` `<>` 成对补全，收尾括号可直接跳过；**单双引号同样按括号处理**——打 `"` 自动补 `"`，再打同一个引号就跳过而不是重复插入，反斜杠后不配对。
- **结构体补全**：光标行（含 Allman 风格另起一行的 `{`、带继承列表的 `class Foo : public Bar`）出现 `struct / class / enum / union` 时打 `{`，补成 `{};`，光标停在两花括号之间；回车自动拆成三行，**分号留在右大括号后**。函数体、`if/for` 块等普通花括号保持原样，不加分号，`namespace {}` 也不加分号。
- **代码检查开关**：`编译 → 代码检查`（勾选即开启），关掉后不再把未知函数 / 缺 `#include` 的标红，也不弹提示；状态记在注册表 `Software\LegacyNotepad\CodeCheck`。
- **语法的强制补全**：代码语言里打完关键字再敲空格，自动带出结构骨架并定位光标——`if` / `for` / `while` / `switch` / `catch` → `() {}` 三行骨架（光标在圆括号内），`do` / `try` / `namespace`（`using namespace` 这种 using 指令不展开）→ `{}` 骨架（光标在花括号内），`public` / `private` / `protected`（**只在类体内生效**）与 `default`（**只在 switch 内生效**）→ 补成 `xxx:` 并按包围它的花括号对齐缩进。`template<` 的尖括号也成对补全。
- 自动补全：按已输入前缀过滤符号与库函数，带类型前缀；光标前没有正在输入的词时不弹窗，`Ctrl+Space` 可手动呼出。
- 代码检查：未定义函数标红行 + 弹窗提示缺头文件；`#include "本地头文件"` 会并入符号集。
- 多语言编译：选语言直接编译，编译在独立线程跑，错误/警告进自绘报错窗口（黄底警告、红底错误、可滚动），支持忽略检查强制编译。
- 库窗口：内置库目录，可搜索并下载 C++ 库与 Python 包。

**命令行（控制台，`Ctrl+反引号`）**
- **独立菜单**：菜单栏单独一列「控制台 → 命令行窗口 / 设置… / 清空输出 / 关闭命令行窗口」，不再塞在「查看」菜单末尾；菜单文案跟随界面语言。
- **设置对话框**：行数（3-60）、列数（20-300）、字号（10-32 像素）、是否启用 ANSI 彩色输出，改完立即按新网格重排窗口并写入注册表；关掉彩色后新输出只用主题默认色，已有的彩色行也按单色显示。
- 自绘命令行窗口，工作目录跟随当前文件；执行 `cmd.exe /C` 并把 stdout/stderr 画进字符网格。
- **交互式标准输入**：进程运行中输入行回车，内容作为 stdin 送进进程，`readline` / `scanf` / `input()` 这类程序可以直接对话；`Ctrl+C` 杀掉整棵进程树（作业对象），`stop` 同理。
- **输出查找**：`Ctrl+F` 进入查找行，`Enter` / `F3` 跳到下一个命中、`Shift+Enter` 上一个；所有命中行淡黄底，当前命中深黄底并自动滚动到可视区。
- **字符级选中**：拖选按字符精确选择、双击选整行、`Ctrl+A` 全选；右键「复制选中 / 复制全部输出 / 导出输出…」，导出为 UTF-8。
- **右侧滚动条**：显示当前位置，可拖动滑块、点击空白翻页；滚轮与 `PgUp/PgDn` 同步，滚动后自动脱离「跟随末尾」。
- **ANSI 颜色**：16 色 + 256 色调色板（`38;5;n`）与真彩色近似（`38;2;r;g;b`）；支持 `\r` 覆盖（进度条）、`\x1b[K` 清行、`\x1b[2J` 清屏、`\x1b[1A` 回退一行。
- **内置命令**：`cls` / `pwd` / `cd` / `open`（资源管理器）/ `history [n]` / `size <行> <列>` / `font <字号>` / `stop` / `save [文件]` / `help` / `exit`。
- 输入行点击即可定位光标，`Tab` 补全路径（重复按轮换候选），`Ctrl+滚轮` 缩放字号，窗口网格尺寸与字号写入注册表；控制台文案跟随界面语言（中文界面中文，其余英文）。

**界面**
- 多标签：顶部标签栏显示文件名（超长省略）+ 未保存状态。
- 深色模式：标题栏、菜单栏、**所有菜单项（含子菜单父项、分隔线、三角箭头、弹出菜单底色）全部自绘**，不残留系统白条。
- 15 种界面语言，运行时可切换：英语、日语、简体中文、俄语、西班牙语、繁体中文、韩语、法语、德语、葡萄牙语、阿拉伯语、印地语、意大利语、荷兰语、土耳其语。
- 背景图：可设图片背景，平铺 / 拉伸 / 适应 / 填充 / 锚点 + 透明度。
- 其他：缩放、状态栏（行列 / 编码 / 行尾 / 缩放 / 语法）、窗口置顶、自定义图标（`.ico` / `.exe` / `.dll` / `.icl` / `.mun`）。

**文件**
- 编码：UTF-8、UTF-8 带 BOM、UTF-16 LE/BE、ANSI；行尾 CRLF / LF / CR。
- 查找 / 替换 / 跳转，区分大小写与方向；最近文件列表；打印与页面设置。

## 构建

依赖：Windows 10/11 x64、CMake ≥ 3.16、C++17 编译器（MSVC 2022/2026 或 MinGW-w64）。

```powershell
# 配置（MSVC）
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
# 或 MinGW
cmake -S . -B build -G "MinGW Makefiles"

# 编译 Release
cmake --build build --config Release
```

产物：`build/Release/qingxinotepad.exe`。MSVC 下按 `/W4 /WX` 编译，当前为 0 warning。

> 编译前请先关掉正在运行的 `qingxinotepad.exe`，否则链接器无法覆盖同名文件。

## 打包（安装包 + 绿色包）

```powershell
pwsh -File build_and_pack.ps1              # 默认版本 1.4.0
pwsh -File build_and_pack.ps1 -Version 1.4.0
pwsh -File build_and_pack.ps1 -SkipBuild   # 只重新打包，不重编译
```

输出到 `dist/`：

- `qingxinotepad_install.exe` —— 单文件安装包（安装器 stub + 内嵌 zip），由安装包构建器 CLI 生成，全程无 GUI 点击。
- `qingxinotepad-portable-<version>.zip` —— 绿色包，配置跟着 exe 走。

### 便携模式

在 exe 同目录放一个名为 `qingxinotepad.portable`（或 `portable.txt`）的标记文件即可：

| 状态 | 配置 | 下载的库 |
| --- | --- | --- |
| 安装版 | `HKCU\Software\LegacyNotepad` | `%LOCALAPPDATA%\LegacyNotepad` |
| 便携版 | exe 同目录 `qingxinotepad.ini` | exe 同目录 `data\` |

同一个 exe 两种模式都支持；便携模式下不往自身目录以外写任何文件。

## 目录结构

```
src/
  core/        # 类型、全局句柄、设置存储（storage）
  lang/        # 15 种界面语言表 + 语言运行时（lang.cpp）
  modules/
    codeeditor  # 自绘编辑器控件：布局、软换行、绘制、命中测试、补全
    syntax      # 语法定义、扩展名检测、分词与高亮着色
    symbols     # 符号表与代码检查（未定义函数、头文件提示）
    compiler    # 多语言编译调度（独立线程）
    compilewindow  # 自绘报错/警告窗口
    tabs        # 标签栏
    menu / menu_skin  # 菜单构建与全项自绘（含子菜单、分隔线）
    theme       # 浅/深色主题与菜单刷新
    background  # GDI+ 背景图/透明度/位置
    dialog      # 查找、替换、字体、透明度等对话框
    consolewindow  # 自绘命令行窗口（终端：实时输出、交互输入、查找、滚动条）
    settings / file / ui / commands / editor
    libwindow / libcatalog / libsource / netlib  # 库窗口与下载
    presets     # 预置库函数/关键字表
  main.cpp      # 窗口类、消息循环、模块装配
  notepad.rc    # 菜单、加速键、图标
CMakeLists.txt
build_and_pack.ps1
```

## 版本历史

### 1.4.2
- **新增代码检查开关**：`编译 → 代码检查` 可一键关 / 开未知函数与缺 `#include` 的红线标记和弹窗提示；关掉时立刻清掉已有红线与待弹提示，重新打开会立刻重扫；选择写入注册表 `CodeCheck`，下次启动保持。

### 1.4.1
- **修复 `using namespace` 被展开**：`using namespace std;` 里的 `namespace` 是 using 指令，打完空格不再补出 `{}` 三行骨架，一条指令可以正常写完；`namespace 名字 { }` 的展开不受影响。

### 1.4.0
- **控制台（命令行窗口）全面升级**：
  - **独立一级菜单**——「控制台」单独成列：命令行窗口（`Ctrl+反引号`）/ 设置… / 清空输出 / 关闭命令行窗口。
  - **命令行设置**——行数、列数、字号、是否彩色四项可在对话框里调，立即生效并记忆。
  - **交互式标准输入**——运行中的进程可以直接在输入行回车送数据，`readline` / `scanf` / `input()` 类程序能真正对话；`Ctrl+C` / `stop` 用作业对象杀整棵进程树。
  - **输出查找**——`Ctrl+F` 输入关键字，`Enter` / `F3` 逐个跳转、`Shift+Enter` 反向；命中行高亮，当前命中加深并自动滚动定位。
  - **字符级选中与复制**——拖选按字符精确选取、双击选整行、`Ctrl+A` 全选；复制时自动去掉行尾补齐空格。
  - **滚动条**——右侧画出轨道与滑块，可拖动、点击翻页，和滚轮 / `PgUp` / `PgDn` 状态同步。
  - **ANSI 增强**——16 色 + 256 色（`38;5;n`）、真彩色近似（`38;2;r;g;b`）、`\x1b[K` 清行、`\x1b[2J` 清屏、`\x1b[1A` 回退一行。
  - **内置命令补齐**——`pwd`、`open`（资源管理器）、`history [n]`、点击输入行定位光标、`Esc` 关闭；`help` 重写。
  - 控制台文案接入界面语言：中文界面中文，其余语言英文。

### 1.3.1
- **折行细分词规则**：只有空格（和制表符）算天然分隔，其余标点、运算符、符号一律能断开单词；**长度超过 15 个字符的连续串不再视作单词**，行尾直接硬断。
- **自动换行开关**：收进「格式 → 自动换行」，勾选状态随注册表保存。
- **单双引号当括号**：成对补全，收尾引号跳过，反斜杠后不配对。
- **结构体补全**：`struct / class / enum / union` 的花括号补成 `{};`，回车后分号留在 `}` 后面；普通语句块、函数体、`namespace` 不加分号；大括号另起一行（Allman）与带继承列表的 `class` 也认。
- **语法级强制补全**：`if / for / while / switch / catch` + 空格带出 `() {}` 骨架，`do / try / namespace` + 空格带出 `{}` 骨架；类体内 `public / private / protected` + 空格补成 `public:` 并对齐缩进，`switch` 体内 `default` + 空格补成 `default:`；`template<` 的 `<>` 成对。

### 1.3.0
- **深色模式菜单**：所有菜单项（含「背景 / 图标 / 最近文件 / 语言 / 编译 / 编码 / 行尾」等子菜单父项）与分隔线全部改为自绘，子菜单箭头用 GDI 绘制，弹出菜单内边距与边框一并染深色，深色下不再出现白色条块。
- **自动补全**：光标前没有正在输入的词（刚敲空格/括号、自动配对后停在括号中）不再弹窗；候选只是把已输入词重列一遍时也不弹窗。
- **中文语言**：修正「关于」对话框与状态栏文案错位（原先关于框显示「行 」、状态栏显示整段关于文案），统一连字符写法。
- **自动换行**：按 `Down` 从折行块直接跳到下一逻辑行；**中日韩文字改为一字一词，行尾逐字断开**。
- 版本号统一 1.3.0，界面语言 15 种。

### 1.2.0 及更早
- 软换行分词规则升级（符号分隔 + 15 字符上限）、多文字宽度适配、界面语言扩到 15 种。
- 编译报错窗口、代码检查、自动补全、多标签、自绘菜单、背景图等逐版加入。

## 已知问题

- Windows Defender 可能对未签名 exe 报启发式误报，见 `.github/DEFENDER_FALSE_POSITIVE.md`。
- 背景图各模式在极端 DPI / 缩放比下可能有细微差异。

## 许可

MIT License，见 [LICENSE](LICENSE)。

## 反馈

命令面板 <https://ttqx.cc.cd/p/> 或仓库 Issues。

## Queries

[x.com/forloopcodes](https://x.com/forloopcodes)
