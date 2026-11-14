#include "compiler.h"
#include "consolewindow.h"
#include "codeeditor.h"
#include "compilewindow.h"
#include "syntax.h"
#include "core/globals.h"
#include "core/storage.h"
#include "core/types.h"
#include "commands.h"
#include "resource.h"
#include <windows.h>
#include <string>
#include <map>
#include <vector>

#define COMP_KEY L"Software\\LegacyNotepad"

static const wchar_t *kCompDlgClass = L"LegacyCompilerDlg";
#define IDC_COMP_COMBO 5001
#define IDC_COMP_EDIT 5002
#define IDC_COMP_DEFAULT 5003
#define IDOK 1
#define IDCANCEL 2

static HWND g_compDlg = nullptr;
static HWND g_compCombo = nullptr;
static HWND g_compEdit = nullptr;
static std::wstring g_compCurrentId;
static std::map<std::wstring, std::wstring> g_compEdited;
static std::vector<std::wstring> g_compIds;
// File that the running compile is building — shown in the result window so
// it is obvious which source was compiled.
static std::wstring g_compileTarget;
// “编译并运行”：本次编译成功后要送进命令行窗口执行的产物路径
static std::wstring g_compileRunPath;
static bool g_runAfterCompile = false;

/* ------------------------------------------------------------------ */
/*  Default command templates per language                             */
/* ------------------------------------------------------------------ */

static std::wstring DefaultCommandFor(const std::wstring &id)
{
    static const std::map<std::wstring, std::wstring> defs = {
        {L"cpp", L"g++ -std=c++17 -Wall -O2 \"{file}\" -o \"{out}\""},
        {L"c", L"gcc -std=c11 -Wall -O2 \"{file}\" -o \"{out}\""},
        {L"cs", L"csc \"{file}\" /out:\"{out}\""},
        {L"java", L"javac -encoding UTF-8 \"{file}\""},
        {L"js", L"node \"{file}\""},
        {L"ts", L"tsc \"{file}\""},
        {L"py", L"python \"{file}\""},
        {L"php", L"php \"{file}\""},
        {L"go", L"go build -o \"{out}\" \"{file}\""},
        {L"rust", L"rustc \"{file}\" -o \"{out}\""},
        {L"lua", L"lua \"{file}\""},
        {L"sh", L"bash \"{file}\""},
        {L"ruby", L"ruby \"{file}\""},
        {L"swift", L"swiftc \"{file}\" -o \"{out}\""},
        {L"kotlin", L"kotlinc \"{file}\" -include-runtime -d \"{out}.jar\""},
        {L"dart", L"dart run \"{file}\""},
        {L"ps", L"powershell -NoProfile -File \"{file}\""},
        {L"perl", L"perl \"{file}\""},
        {L"r", L"Rscript \"{file}\""},
        {L"haskell", L"ghc \"{file}\" -o \"{out}\""},
        {L"scala", L"scala \"{file}\""},
        {L"groovy", L"groovy \"{file}\""},
        {L"objc", L"clang -fobjc-arc \"{file}\" -o \"{out}\""},
        {L"vb", L"vbc \"{file}\" /out:\"{out}\""},
        {L"cmake", L"cmake -S \"{dir}\" -B \"{dir}build\" && cmake --build \"{dir}build\" --config Release"},
        {L"qmake", L"qmake \"{file}\" -o Makefile && nmake"},
        {L"rc", L"rc /nologo \"{file}\" /fo \"{out}.res\""},
        {L"make", L"make -f \"{file}\""},
        {L"docker", L"docker build -f \"{file}\" ."},
        {L"proto", L"protoc --cpp_out=\"{dir}\" \"{file}\""},
    };
    auto it = defs.find(id);
    return (it != defs.end()) ? it->second : std::wstring();
}

/* ------------------------------------------------------------------ */
/*  Registry persistence                                               */
/* ------------------------------------------------------------------ */

std::wstring CompilerCommandFor(const std::wstring &id)
{
    auto it = g_state.compilerCmds.find(id);
    if (it != g_state.compilerCmds.end() && !it->second.empty())
        return it->second;
    return DefaultCommandFor(id);
}

void SetCompilerCommand(const std::wstring &id, const std::wstring &cmd)
{
    g_state.compilerCmds[id] = cmd;
    std::wstring name = L"Compiler_" + id;
    StorageSetString(COMP_KEY, name.c_str(), cmd);
}

void LoadCompilerCommands()
{
    for (int i = 0; i < LanguageCount(); ++i)
    {
        const LanguageDef *L = LanguageByIndex(i);
        if (!L || !L->id)
            continue;
        std::wstring name = L"Compiler_" + std::wstring(L->id);
        std::wstring value;
        if (StorageGetString(COMP_KEY, name.c_str(), &value) && !value.empty())
        {
            g_state.compilerCmds[L->id] = value;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static void SplitPath(const std::wstring &full, std::wstring &dir, std::wstring &name)
{
    size_t slash = full.find_last_of(L"\\/");
    std::wstring leaf = (slash == std::wstring::npos) ? full : full.substr(slash + 1);
    dir = (slash == std::wstring::npos) ? L"" : full.substr(0, slash + 1);
    size_t dot = leaf.find_last_of(L'.');
    name = (dot == std::wstring::npos) ? leaf : leaf.substr(0, dot);
}

static std::wstring ReplaceAll(std::wstring s, const std::wstring &from, const std::wstring &to)
{
    if (from.empty())
        return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::wstring::npos)
    {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

static std::wstring BytesToWide(const std::vector<BYTE> &bytes)
{
    if (bytes.empty())
        return L"";
    // Try strict UTF-8 first.
    int wc = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                 reinterpret_cast<const char *>(bytes.data()),
                                 static_cast<int>(bytes.size()), nullptr, 0);
    if (wc > 0)
    {
        std::wstring out(wc, L'\0');
        MultiByteToWideChar(CP_UTF8, 0,
                            reinterpret_cast<const char *>(bytes.data()),
                            static_cast<int>(bytes.size()), &out[0], wc);
        return out;
    }
    // Fall back to the ANSI code page.
    int wc2 = MultiByteToWideChar(CP_ACP, 0,
                                  reinterpret_cast<const char *>(bytes.data()),
                                  static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring out(wc2, L'\0');
    MultiByteToWideChar(CP_ACP, 0,
                        reinterpret_cast<const char *>(bytes.data()),
                        static_cast<int>(bytes.size()), &out[0], wc2);
    return out;
}

/* ------------------------------------------------------------------ */
/*  Compile runner (background thread so the UI never blocks)          */
/* ------------------------------------------------------------------ */

static volatile LONG g_compileBusy = 0; // re-entry guard
bool g_ignoreCheck = false;            // "force compile": suppress code-check popup

struct CompileJob
{
    HWND hwndMain;        // window to post the completion message to
    std::wstring cmd;     // fully substituted command line
    std::wstring workDir; // working directory for the child process
    std::wstring outFile; // temp file that captures stdout+stderr
    HANDLE hOut;          // handle of the output file (inherited by child)
};

static DWORD WINAPI CompileWorker(LPVOID param)
{
    CompileJob *job = static_cast<CompileJob *>(param);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdError = si.hStdOutput = job->hOut; // file handle, valid on this thread
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> buf(job->cmd.begin(), job->cmd.end());
    buf.push_back(L'\0');
    const wchar_t *workDir = job->workDir.empty() ? nullptr : job->workDir.c_str();

    CompileResult *res = new CompileResult();
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, workDir, &si, &pi);
    if (!ok)
    {
        DWORD err = GetLastError();
        wchar_t msg[512] = {0};
        wsprintfW(msg, L"无法启动编译器命令。\n错误码: %lu\n\n命令:\n%ls", err, job->cmd.c_str());
        res->output = msg;
        res->success = false;
        CloseHandle(job->hOut);
        DeleteFileW(job->outFile.c_str());
        PostMessageW(job->hwndMain, WM_APP_COMPILE_DONE, reinterpret_cast<WPARAM>(res), 0);
        delete job;
        return 0;
    }

    DWORD waitRes = WaitForSingleObject(pi.hProcess, 60000);
    res->timedOut = (waitRes == WAIT_TIMEOUT);
    if (res->timedOut)
        TerminateProcess(pi.hProcess, 1);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(job->hOut);

    HANDLE hIn = CreateFileW(job->outFile.c_str(), GENERIC_READ, 0, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::vector<BYTE> data;
    if (hIn != INVALID_HANDLE_VALUE)
    {
        DWORD sz = GetFileSize(hIn, nullptr);
        data.resize(sz);
        DWORD rd = 0;
        ReadFile(hIn, data.data(), sz, &rd, nullptr);
        CloseHandle(hIn);
    }
    DeleteFileW(job->outFile.c_str());

    std::wstring output = BytesToWide(data);
    if (res->timedOut)
        output = (output.empty() ? L"" : output + L"\n\n") + L"编译超时（60 秒），已终止进程。";
    res->success = (!res->timedOut) && (exitCode == 0);
    if (res->success && output.empty())
        output = L"编译成功。";
    res->output = output;

    PostMessageW(job->hwndMain, WM_APP_COMPILE_DONE, reinterpret_cast<WPARAM>(res), 0);
    delete job;
    return 0;
}

void BuildCompileLang(const std::wstring &id)
{
    // Re-entry guard: a compile is already running on its own thread.
    if (InterlockedCompareExchange(&g_compileBusy, 1, 0) != 0)
        return;

    // 1. Ensure we have a file on disk to compile (UI-thread work only).
    if (g_state.filePath.empty())
    {
        FileSaveAs();
        if (g_state.filePath.empty())
        {
            g_ignoreCheck = false;
            InterlockedExchange(&g_compileBusy, 0);
            return;
        }
    }
    if (g_state.modified)
    {
        FileSave();
        if (g_state.modified)
        {
            g_ignoreCheck = false;
            InterlockedExchange(&g_compileBusy, 0);
            return; // save failed / cancelled
        }
    }

    std::wstring cmd = CompilerCommandFor(id);
    if (cmd.empty())
    {
        MessageBoxW(g_hwndMain, L"该语言尚未配置编译器命令。\n请通过菜单 编译 → 编译设置... 进行设置。",
                    L"编译", MB_ICONINFORMATION | MB_OK);
        g_ignoreCheck = false;
        InterlockedExchange(&g_compileBusy, 0);
        return;
    }

    // 2. Substitute placeholders.
    g_compileTarget = g_state.filePath;
    std::wstring dir, name;
    SplitPath(g_state.filePath, dir, name);
    std::wstring out = dir + name + L".exe";
    g_compileRunPath = out;
    cmd = ReplaceAll(cmd, L"{file}", g_state.filePath);
    cmd = ReplaceAll(cmd, L"{dir}", dir);
    cmd = ReplaceAll(cmd, L"{name}", name);
    cmd = ReplaceAll(cmd, L"{out}", out);

    // 3. Redirect output to a temp file (created on the UI thread, written
    //    and closed by the worker).
    wchar_t tmpPath[MAX_PATH] = {0};
    GetTempPathW(MAX_PATH, tmpPath);
    std::wstring outFile = std::wstring(tmpPath) + L"qingxinotepad_build.tmp";
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;
    HANDLE hOut = CreateFileW(outFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hOut == INVALID_HANDLE_VALUE)
    {
        ShowCompileResult(g_hwndMain, L"无法创建临时输出文件。", false);
        g_ignoreCheck = false;
        InterlockedExchange(&g_compileBusy, 0);
        return;
    }

    // 4. Spawn the worker; the UI thread returns immediately.
    CompileJob *job = new CompileJob();
    job->hwndMain = g_hwndMain;
    job->cmd = cmd;
    job->workDir = dir;
    job->outFile = outFile;
    job->hOut = hOut;

    HANDLE hThread = CreateThread(nullptr, 0, CompileWorker, job, 0, nullptr);
    if (!hThread)
    {
        DWORD err = GetLastError();
        CloseHandle(hOut);
        DeleteFileW(outFile.c_str());
        delete job;
        wchar_t msg[512] = {0};
        wsprintfW(msg, L"无法创建编译线程。\n错误码: %lu", err);
        ShowCompileResult(g_hwndMain, msg, false);
        g_ignoreCheck = false;
        InterlockedExchange(&g_compileBusy, 0);
        return;
    }
    CloseHandle(hThread);

    // Disable the compile commands while a build runs.
    HMENU hMenu = GetMenu(g_hwndMain);
    if (hMenu)
    {
        EnableMenuItem(hMenu, IDM_BUILD_COMPILE, MF_BYCOMMAND | MF_GRAYED);
        for (int i = 0; i < LanguageCount(); ++i)
        {
            const LanguageDef *L = LanguageByIndex(i);
            if (L && L->id && id == L->id)
                EnableMenuItem(hMenu, IDM_BUILD_LANG_BASE + i, MF_BYCOMMAND | MF_GRAYED);
        }
    }
}

void BuildCompile()
{
    const LanguageDef *L = LanguageFromPath(g_state.filePath);
    std::wstring id = (L && L->id) ? std::wstring(L->id) : L"text";
    BuildCompileLang(id);
}

// 编译并运行：正常编译一次，成功后在命令行窗口里执行编译产物。
void BuildCompileRun()
{
    // 已有编译在跑就不排队，避免两个产物抢同一个输出文件
    if (InterlockedCompareExchange(&g_compileBusy, 0, 0) != 0)
        return;
    g_runAfterCompile = true;
    BuildCompile();
    // BuildCompileLang 的所有提前返回都会把 busy 清回 0，据此判断没真正开始
    if (InterlockedCompareExchange(&g_compileBusy, 0, 0) == 0)
        g_runAfterCompile = false;
}

// "Ignore checks, force compile": build the current file while suppressing
// the debounced code-check popup for the duration of the build.
void BuildCompileForce()
{
    g_ignoreCheck = true;                // suppress check popups for the build
    CE_SuppressPendingCheck(g_hwndEditor); // cancel the already-pending check popup
    BuildCompile();
    // g_ignoreCheck is cleared again when the build finishes (CompileNotifyDone)
    // or when a compile fails to start (every early return in BuildCompileLang).
}

// Called from the main window's WndProc on WM_APP_COMPILE_DONE.
void CompileNotifyDone(HWND hwnd, WPARAM wParam)
{
    g_ignoreCheck = false; // force-compile window over
    CompileResult *res = reinterpret_cast<CompileResult *>(wParam);
    if (res)
    {
        const bool ok = res->success != FALSE;
        std::wstring shown;
        if (!g_compileTarget.empty())
            shown = L"编译目标：" + g_compileTarget + L"\r\n\r\n";
        shown += res->output;
        ShowCompileResult(hwnd, shown, ok);
        const bool runIt = g_runAfterCompile && ok;
        delete res;

        // 编译并运行：编译成功后把刚产出的可执行文件交给命令行窗口，
        // 工作目录就是源文件所在目录（与控制台窗口的启动目录一致）。
        if (runIt)
        {
            g_runAfterCompile = false;
            if (!g_compileRunPath.empty())
                ConsoleRunBuiltExe(g_compileRunPath);
        }
    }
    g_runAfterCompile = false;
    InterlockedExchange(&g_compileBusy, 0);
    HMENU hMenu = GetMenu(hwnd);
    if (hMenu)
    {
        EnableMenuItem(hMenu, IDM_BUILD_COMPILE, MF_BYCOMMAND | MF_ENABLED);
        for (int i = 0; i < LanguageCount(); ++i)
            EnableMenuItem(hMenu, IDM_BUILD_LANG_BASE + i, MF_BYCOMMAND | MF_ENABLED);
    }
}

/* ------------------------------------------------------------------ */
/*  Compiler settings dialog                                           */
/* ------------------------------------------------------------------ */

static std::wstring EditText(HWND hEdit)
{
    int len = GetWindowTextLengthW(hEdit);
    std::wstring s(len, L'\0');
    GetWindowTextW(hEdit, &s[0], len + 1);
    return s;
}

static std::wstring EffectiveCommand(const std::wstring &id)
{
    auto it = g_compEdited.find(id);
    if (it != g_compEdited.end())
        return it->second;
    return CompilerCommandFor(id);
}

static void CompDlgLoadCurrent(HWND hwnd)
{
    (void)hwnd;
    SetWindowTextW(g_compEdit, EffectiveCommand(g_compCurrentId).c_str());
}

static void CompDlgCommitCurrent()
{
    if (!g_compCurrentId.empty())
        g_compEdited[g_compCurrentId] = EditText(g_compEdit);
}

static void CompDlgSelect(int idx)
{
    if (idx < 0 || idx >= static_cast<int>(g_compIds.size()))
        return;
    CompDlgCommitCurrent();
    g_compCurrentId = g_compIds[idx];
    CompDlgLoadCurrent(nullptr);
}

LRESULT CALLBACK CompilerDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_compCombo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                      14, 18, 380, 300, hwnd, reinterpret_cast<HMENU>(IDC_COMP_COMBO), nullptr, nullptr);
        g_compEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                     14, 52, 380, 22, hwnd, reinterpret_cast<HMENU>(IDC_COMP_EDIT), nullptr, nullptr);
        CreateWindowExW(0, L"STATIC", L"占位符: {file}=文件完整路径   {dir}=所在目录   {name}=文件名(不含扩展名)   {out}=输出程序路径",
                        WS_CHILD | WS_VISIBLE, 14, 82, 520, 16, hwnd, nullptr, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"恢复默认", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        14, 112, 90, 26, hwnd, reinterpret_cast<HMENU>(IDC_COMP_DEFAULT), nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                        460, 112, 70, 26, hwnd, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        540, 112, 70, 26, hwnd, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);

        // Fill the language list and select the current file's language.
        g_compIds.clear();
        for (int i = 0; i < LanguageCount(); ++i)
        {
            const LanguageDef *L = LanguageByIndex(i);
            if (!L || !L->id || !L->display)
                continue;
            g_compIds.push_back(L->id);
            SendMessageW(g_compCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L->display));
        }
        g_compEdited.clear();
        g_compCurrentId.clear();

        int sel = 0;
        std::wstring curId;
        if (!g_state.filePath.empty())
        {
            const LanguageDef *cur = LanguageFromPath(g_state.filePath);
            if (cur && cur->id)
                curId = cur->id;
        }
        for (size_t i = 0; i < g_compIds.size(); ++i)
            if (g_compIds[i] == curId)
            {
                sel = static_cast<int>(i);
                break;
            }
        SendMessageW(g_compCombo, CB_SETCURSEL, sel, 0);
        CompDlgSelect(sel);
        SetFocus(g_compEdit);
        return 0;
    }
    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);
        if (id == IDC_COMP_COMBO && code == CBN_SELCHANGE)
        {
            int idx = static_cast<int>(SendMessageW(g_compCombo, CB_GETCURSEL, 0, 0));
            CompDlgSelect(idx);
            return 0;
        }
        if (id == IDC_COMP_EDIT && code == EN_CHANGE)
        {
            if (!g_compCurrentId.empty())
                g_compEdited[g_compCurrentId] = EditText(g_compEdit);
            return 0;
        }
        if (id == IDC_COMP_DEFAULT)
        {
            g_compEdited[g_compCurrentId] = DefaultCommandFor(g_compCurrentId);
            SetWindowTextW(g_compEdit, g_compEdited[g_compCurrentId].c_str());
            return 0;
        }
        if (id == IDOK)
        {
            CompDlgCommitCurrent();
            for (const auto &kv : g_compEdited)
                SetCompilerCommand(kv.first, kv.second);
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == IDCANCEL)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_compDlg = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void BuildShowSettings()
{
    if (g_compDlg)
    {
        SetForegroundWindow(g_compDlg);
        return;
    }
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = CompilerDlgProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kCompDlgClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    HWND owner = g_hwndMain;
    EnableWindow(owner, FALSE);
    g_compDlg = CreateWindowExW(0, kCompDlgClass, L"编译器命令设置",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, 640, 175, owner, nullptr, wc.hInstance, nullptr);
    if (!g_compDlg)
    {
        EnableWindow(owner, TRUE);
        return;
    }
    // Center over the owner.
    RECT rcDlg, rcOwner;
    GetWindowRect(g_compDlg, &rcDlg);
    GetWindowRect(owner, &rcOwner);
    int cx = rcOwner.left + (rcOwner.right - rcOwner.left - (rcDlg.right - rcDlg.left)) / 2;
    int cy = rcOwner.top + (rcOwner.bottom - rcOwner.top - (rcDlg.bottom - rcDlg.top)) / 2;
    SetWindowPos(g_compDlg, nullptr, cx, cy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);

    MSG msg;
    while (IsWindow(g_compDlg) && GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (!IsDialogMessageW(g_compDlg, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}
