#include "settings.h"
#include "core/globals.h"
#include "core/storage.h"
#include "core/types.h"
#include <windows.h>

#define SETTINGS_KEY L"Software\\LegacyNotepad"
#define FONT_NAME_VALUE L"FontName"
#define FONT_SIZE_VALUE L"FontSize"
#define FONT_WEIGHT_VALUE L"FontWeight"
#define FONT_ITALIC_VALUE L"FontItalic"
#define FONT_UNDERLINE_VALUE L"FontUnderline"
#define ALWAYS_ON_TOP_VALUE L"AlwaysOnTop"
#define WORD_WRAP_VALUE L"WordWrap"
#define CODE_CHECK_VALUE L"CodeCheck"
#define WINDOW_X_VALUE L"WindowX"
#define WINDOW_Y_VALUE L"WindowY"
#define WINDOW_WIDTH_VALUE L"WindowWidth"
#define WINDOW_HEIGHT_VALUE L"WindowHeight"
#define MIN_FONT_SIZE 8u
#define MAX_FONT_SIZE 72u

void LoadFontSettings()
{
    std::wstring name;
    if (StorageGetString(SETTINGS_KEY, FONT_NAME_VALUE, &name) && !name.empty())
    {
        g_state.fontName = name;
    }

    DWORD fontSize = 0;
    if (StorageGetDword(SETTINGS_KEY, FONT_SIZE_VALUE, &fontSize))
    {
        if (fontSize >= MIN_FONT_SIZE && fontSize <= MAX_FONT_SIZE)
        {
            g_state.fontSize = static_cast<int>(fontSize);
        }
    }

    DWORD weight = 0;
    if (StorageGetDword(SETTINGS_KEY, FONT_WEIGHT_VALUE, &weight))
    {
        g_state.fontWeight = static_cast<int>(weight);
    }

    DWORD flag = 0;
    if (StorageGetDword(SETTINGS_KEY, FONT_ITALIC_VALUE, &flag))
    {
        g_state.fontItalic = (flag != 0);
    }

    if (StorageGetDword(SETTINGS_KEY, FONT_UNDERLINE_VALUE, &flag))
    {
        g_state.fontUnderline = (flag != 0);
    }

    if (StorageGetDword(SETTINGS_KEY, ALWAYS_ON_TOP_VALUE, &flag))
    {
        g_state.alwaysOnTop = (flag != 0);
    }

    // 自动换行是用户可选的开关（格式 → 自动换行），记住上次的选择
    if (StorageGetDword(SETTINGS_KEY, WORD_WRAP_VALUE, &flag))
    {
        g_state.wordWrap = (flag != 0);
    }

    // 代码检查开关（编译 → 代码检查），记住上次的选择
    if (StorageGetDword(SETTINGS_KEY, CODE_CHECK_VALUE, &flag))
    {
        g_state.codeCheck = (flag != 0);
    }
}

void SaveFontSettings()
{
    StorageSetString(SETTINGS_KEY, FONT_NAME_VALUE, g_state.fontName);
    StorageSetDword(SETTINGS_KEY, FONT_SIZE_VALUE, static_cast<DWORD>(g_state.fontSize));
    StorageSetDword(SETTINGS_KEY, FONT_WEIGHT_VALUE, static_cast<DWORD>(g_state.fontWeight));
    StorageSetDword(SETTINGS_KEY, FONT_ITALIC_VALUE, g_state.fontItalic ? 1u : 0u);
    StorageSetDword(SETTINGS_KEY, FONT_UNDERLINE_VALUE, g_state.fontUnderline ? 1u : 0u);
    StorageSetDword(SETTINGS_KEY, ALWAYS_ON_TOP_VALUE, g_state.alwaysOnTop ? 1u : 0u);
    StorageSetDword(SETTINGS_KEY, WORD_WRAP_VALUE, g_state.wordWrap ? 1u : 0u);
    StorageSetDword(SETTINGS_KEY, CODE_CHECK_VALUE, g_state.codeCheck ? 1u : 0u);
}

void LoadWindowSettings()
{
    DWORD v = 0;
    if (StorageGetDword(SETTINGS_KEY, WINDOW_X_VALUE, &v))
    {
        g_state.windowX = static_cast<int>(v);
    }
    if (StorageGetDword(SETTINGS_KEY, WINDOW_Y_VALUE, &v))
    {
        g_state.windowY = static_cast<int>(v);
    }
    if (StorageGetDword(SETTINGS_KEY, WINDOW_WIDTH_VALUE, &v) && v > 0)
    {
        g_state.windowWidth = static_cast<int>(v);
    }
    if (StorageGetDword(SETTINGS_KEY, WINDOW_HEIGHT_VALUE, &v) && v > 0)
    {
        g_state.windowHeight = static_cast<int>(v);
    }

    // Roomy default: a saved size from an older session may be tiny; promote
    // anything smaller than the new comfortable baseline.
    if (g_state.windowWidth < 1000)
        g_state.windowWidth = 1200;
    if (g_state.windowHeight < 700)
        g_state.windowHeight = 760;

    RECT rc = {g_state.windowX, g_state.windowY, g_state.windowX + g_state.windowWidth, g_state.windowY + g_state.windowHeight};
    HMONITOR hMonitor = MonitorFromRect(&rc, MONITOR_DEFAULTTONULL);
    if (!hMonitor)
    {
        g_state.windowX = CW_USEDEFAULT;
        g_state.windowY = CW_USEDEFAULT;
    }
}

void SaveWindowSettings()
{
    StorageSetDword(SETTINGS_KEY, WINDOW_X_VALUE, static_cast<DWORD>(g_state.windowX));
    StorageSetDword(SETTINGS_KEY, WINDOW_Y_VALUE, static_cast<DWORD>(g_state.windowY));
    StorageSetDword(SETTINGS_KEY, WINDOW_WIDTH_VALUE, static_cast<DWORD>(g_state.windowWidth));
    StorageSetDword(SETTINGS_KEY, WINDOW_HEIGHT_VALUE, static_cast<DWORD>(g_state.windowHeight));
}
