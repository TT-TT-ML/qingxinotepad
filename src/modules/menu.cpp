#include "menu.h"
#include "core/globals.h"
#include "resource.h"
#include "lang/lang.h"
#include "editor.h"
#include "file.h"
#include "menu_skin.h"
#include "syntax.h"

// 语言菜单的唯一真相来源：加语言只改这张表 + 对应 .h + RC 菜单项
struct LangMenuItem
{
    UINT id;
    LangID lang;
    std::wstring LangStrings::*text;
};
static const LangMenuItem kLangMenuItems[] = {
    {IDM_VIEW_LANG_EN, LangID::EN, &LangStrings::menuLangEnglish},
    {IDM_VIEW_LANG_ZH_CN, LangID::ZH_CN, &LangStrings::menuLangChinese},
    {IDM_VIEW_LANG_ZH_TW, LangID::ZH_TW, &LangStrings::menuLangChineseTW},
    {IDM_VIEW_LANG_JA, LangID::JA, &LangStrings::menuLangJapanese},
    {IDM_VIEW_LANG_KO, LangID::KO, &LangStrings::menuLangKorean},
    {IDM_VIEW_LANG_RU, LangID::RU, &LangStrings::menuLangRussian},
    {IDM_VIEW_LANG_ES, LangID::ES, &LangStrings::menuLangSpanish},
    {IDM_VIEW_LANG_FR, LangID::FR, &LangStrings::menuLangFrench},
    {IDM_VIEW_LANG_DE, LangID::DE, &LangStrings::menuLangGerman},
    {IDM_VIEW_LANG_PT, LangID::PT, &LangStrings::menuLangPortuguese},
    {IDM_VIEW_LANG_AR, LangID::AR, &LangStrings::menuLangArabic},
    {IDM_VIEW_LANG_HI, LangID::HI, &LangStrings::menuLangHindi},
    {IDM_VIEW_LANG_IT, LangID::IT, &LangStrings::menuLangItalian},
    {IDM_VIEW_LANG_NL, LangID::NL, &LangStrings::menuLangDutch},
    {IDM_VIEW_LANG_TR, LangID::TR, &LangStrings::menuLangTurkish},
};

void UpdateMenuStrings()
{
    HMENU hMenu = GetMenu(g_hwndMain);
    if (!hMenu)
        return;

    const auto& lang = GetLangStrings();

    // 先释放上一轮的自绘数据并还原成普通字符串项：ModifyMenuW 会重建菜单项、
    // 丢掉 OWNERDRAW 标记，不先释放的话 dwItemData 就泄漏了。
    MenuSkinFree(hMenu);

    ModifyMenuW(hMenu, 0, MF_BYPOSITION | MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(GetSubMenu(hMenu, 0)), lang.menuFile.c_str());
    ModifyMenuW(hMenu, 1, MF_BYPOSITION | MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(GetSubMenu(hMenu, 1)), lang.menuEdit.c_str());
    ModifyMenuW(hMenu, 2, MF_BYPOSITION | MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(GetSubMenu(hMenu, 2)), lang.menuFormat.c_str());
    ModifyMenuW(hMenu, 3, MF_BYPOSITION | MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(GetSubMenu(hMenu, 3)), lang.menuView.c_str());
    ModifyMenuW(hMenu, 4, MF_BYPOSITION | MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(GetSubMenu(hMenu, 4)), lang.menuLanguage.c_str());
    ModifyMenuW(hMenu, 5, MF_BYPOSITION | MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(GetSubMenu(hMenu, 5)), lang.menuHelp.c_str());

    HMENU hFileMenu = GetSubMenu(hMenu, 0);
    if (hFileMenu)
    {
        // 按 ID 定位，不按位置：最近文件子菜单是运行时插入到位置 5 的 POPUP，
        // 旧实现按位置改 5/6/8，会把这个子菜单覆盖成字符串项（最近文件整段消失），
        // 并让 Page Setup / Exit 留下英文残留。
        ModifyMenuW(hFileMenu, IDM_FILE_NEW, MF_BYCOMMAND | MF_STRING, IDM_FILE_NEW, lang.menuNew.c_str());
        ModifyMenuW(hFileMenu, IDM_FILE_OPEN, MF_BYCOMMAND | MF_STRING, IDM_FILE_OPEN, lang.menuOpen.c_str());
        ModifyMenuW(hFileMenu, IDM_FILE_SAVE, MF_BYCOMMAND | MF_STRING, IDM_FILE_SAVE, lang.menuSave.c_str());
        ModifyMenuW(hFileMenu, IDM_FILE_SAVEAS, MF_BYCOMMAND | MF_STRING, IDM_FILE_SAVEAS, lang.menuSaveAs.c_str());
        ModifyMenuW(hFileMenu, IDM_FILE_PRINT, MF_BYCOMMAND | MF_STRING, IDM_FILE_PRINT, lang.menuPrint.c_str());
        ModifyMenuW(hFileMenu, IDM_FILE_PAGESETUP, MF_BYCOMMAND | MF_STRING, IDM_FILE_PAGESETUP, lang.menuPageSetup.c_str());
        ModifyMenuW(hFileMenu, IDM_FILE_EXIT, MF_BYCOMMAND | MF_STRING, IDM_FILE_EXIT, lang.menuExit.c_str());
    }

    HMENU hEditMenu = GetSubMenu(hMenu, 1);
    if (hEditMenu)
    {
        ModifyMenuW(hEditMenu, 0, MF_BYPOSITION | MF_STRING, IDM_EDIT_UNDO, lang.menuUndo.c_str());
        ModifyMenuW(hEditMenu, 1, MF_BYPOSITION | MF_STRING, IDM_EDIT_REDO, lang.menuRedo.c_str());
        ModifyMenuW(hEditMenu, 3, MF_BYPOSITION | MF_STRING, IDM_EDIT_CUT, lang.menuCut.c_str());
        ModifyMenuW(hEditMenu, 4, MF_BYPOSITION | MF_STRING, IDM_EDIT_COPY, lang.menuCopy.c_str());
        ModifyMenuW(hEditMenu, 5, MF_BYPOSITION | MF_STRING, IDM_EDIT_PASTE, lang.menuPaste.c_str());
        ModifyMenuW(hEditMenu, 6, MF_BYPOSITION | MF_STRING, IDM_EDIT_DELETE, lang.menuDelete.c_str());
        ModifyMenuW(hEditMenu, 8, MF_BYPOSITION | MF_STRING, IDM_EDIT_FIND, lang.menuFind.c_str());
        ModifyMenuW(hEditMenu, 9, MF_BYPOSITION | MF_STRING, IDM_EDIT_FINDNEXT, lang.menuFindNext.c_str());
        ModifyMenuW(hEditMenu, 10, MF_BYPOSITION | MF_STRING, IDM_EDIT_FINDPREV, lang.menuFindPrev.c_str());
        ModifyMenuW(hEditMenu, 11, MF_BYPOSITION | MF_STRING, IDM_EDIT_REPLACE, lang.menuReplace.c_str());
        ModifyMenuW(hEditMenu, 12, MF_BYPOSITION | MF_STRING, IDM_EDIT_GOTO, lang.menuGoTo.c_str());
        ModifyMenuW(hEditMenu, 14, MF_BYPOSITION | MF_STRING, IDM_EDIT_SELECTALL, lang.menuSelectAll.c_str());
        ModifyMenuW(hEditMenu, 15, MF_BYPOSITION | MF_STRING, IDM_EDIT_TIMEDATE, lang.menuTimeDate.c_str());
    }

    HMENU hFormatMenu = GetSubMenu(hMenu, 2);
    if (hFormatMenu)
    {
        ModifyMenuW(hFormatMenu, 0, MF_BYPOSITION | MF_STRING, IDM_FORMAT_WORDWRAP, lang.menuWordWrap.c_str());
        // ModifyMenuW 会重建菜单项、丢掉原来的勾选状态，这里补回来
        CheckMenuItem(hFormatMenu, IDM_FORMAT_WORDWRAP, g_state.wordWrap ? MF_CHECKED : MF_UNCHECKED);
        ModifyMenuW(hFormatMenu, 1, MF_BYPOSITION | MF_STRING, IDM_FORMAT_FONT, lang.menuFont.c_str());

        HMENU hEncMenu = GetSubMenu(hFormatMenu, 3);
        if (hEncMenu)
        {
            ModifyMenuW(hFormatMenu, 3, MF_BYPOSITION | MF_STRING | MF_POPUP,
                        reinterpret_cast<UINT_PTR>(hEncMenu), lang.menuEncoding.c_str());
            ModifyMenuW(hEncMenu, IDM_FORMAT_ENC_BASE + 0, MF_STRING, IDM_FORMAT_ENC_BASE + 0, lang.encodingUTF8.c_str());
            ModifyMenuW(hEncMenu, IDM_FORMAT_ENC_BASE + 1, MF_STRING, IDM_FORMAT_ENC_BASE + 1, lang.encodingUTF8BOM.c_str());
            ModifyMenuW(hEncMenu, IDM_FORMAT_ENC_BASE + 2, MF_STRING, IDM_FORMAT_ENC_BASE + 2, lang.encodingUTF16LE.c_str());
            ModifyMenuW(hEncMenu, IDM_FORMAT_ENC_BASE + 3, MF_STRING, IDM_FORMAT_ENC_BASE + 3, lang.encodingUTF16BE.c_str());
            ModifyMenuW(hEncMenu, IDM_FORMAT_ENC_BASE + 4, MF_STRING, IDM_FORMAT_ENC_BASE + 4, lang.encodingANSI.c_str());
        }

        HMENU hEolMenu = GetSubMenu(hFormatMenu, 4);
        if (hEolMenu)
        {
            ModifyMenuW(hFormatMenu, 4, MF_BYPOSITION | MF_STRING | MF_POPUP,
                        reinterpret_cast<UINT_PTR>(hEolMenu), lang.menuLineEnding.c_str());
            ModifyMenuW(hEolMenu, IDM_FORMAT_EOL_BASE + 0, MF_STRING, IDM_FORMAT_EOL_BASE + 0, lang.lineEndingCRLF.c_str());
            ModifyMenuW(hEolMenu, IDM_FORMAT_EOL_BASE + 1, MF_STRING, IDM_FORMAT_EOL_BASE + 1, lang.lineEndingLF.c_str());
            ModifyMenuW(hEolMenu, IDM_FORMAT_EOL_BASE + 2, MF_STRING, IDM_FORMAT_EOL_BASE + 2, lang.lineEndingCR.c_str());
        }
        UpdateEncodingMenuChecks();
    }

    HMENU hViewMenu = GetSubMenu(hMenu, 3);
    if (hViewMenu)
    {
        ModifyMenuW(hViewMenu, 0, MF_BYPOSITION | MF_STRING, IDM_VIEW_ZOOMIN, lang.menuZoomIn.c_str());
        ModifyMenuW(hViewMenu, 1, MF_BYPOSITION | MF_STRING, IDM_VIEW_ZOOMOUT, lang.menuZoomOut.c_str());
        ModifyMenuW(hViewMenu, 2, MF_BYPOSITION | MF_STRING, IDM_VIEW_ZOOMDEFAULT, lang.menuZoomDefault.c_str());
        ModifyMenuW(hViewMenu, 4, MF_BYPOSITION | MF_STRING, IDM_VIEW_STATUSBAR, lang.menuStatusBar.c_str());
        ModifyMenuW(hViewMenu, 5, MF_BYPOSITION | MF_STRING, IDM_VIEW_DARKMODE, lang.menuDarkMode.c_str());

        HMENU hBgMenu = GetSubMenu(hViewMenu, 7);
        if (hBgMenu)
        {
            ModifyMenuW(hViewMenu, 7, MF_BYPOSITION | MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(hBgMenu), lang.menuBackground.c_str());
            ModifyMenuW(hBgMenu, 0, MF_BYPOSITION | MF_STRING, IDM_VIEW_BG_SELECT, lang.menuBgSelect.c_str());
            ModifyMenuW(hBgMenu, 1, MF_BYPOSITION | MF_STRING, IDM_VIEW_BG_CLEAR, lang.menuBgClear.c_str());
            ModifyMenuW(hBgMenu, 2, MF_BYPOSITION | MF_STRING, IDM_VIEW_BG_OPACITY, lang.menuBgOpacity.c_str());

            HMENU hPosMenu = GetSubMenu(hBgMenu, 4);
            if (hPosMenu)
            {
                ModifyMenuW(hBgMenu, 4, MF_BYPOSITION | MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(hPosMenu), lang.menuBgPosition.c_str());
                ModifyMenuW(hPosMenu, 0, MF_BYPOSITION | MF_STRING, IDM_VIEW_BG_POS_TOPLEFT, lang.menuBgPosTopLeft.c_str());
                ModifyMenuW(hPosMenu, 1, MF_BYPOSITION | MF_STRING, IDM_VIEW_BG_POS_TOPCENTER, lang.menuBgPosTopCenter.c_str());
                ModifyMenuW(hPosMenu, 2, MF_BYPOSITION | MF_STRING, IDM_VIEW_BG_POS_TOPRIGHT, lang.menuBgPosTopRight.c_str());
                ModifyMenuW(hPosMenu, 4, MF_BYPOSITION | MF_STRING, IDM_VIEW_BG_POS_CENTERLEFT, lang.menuBgPosCenterLeft.c_str());
                ModifyMenuW(hPosMenu, 5, MF_BYPOSITION | MF_STRING, IDM_VIEW_BG_POS_CENTER, lang.menuBgPosCenter.c_str());
                ModifyMenuW(hPosMenu, 6, MF_BYPOSITION | MF_STRING, IDM_VIEW_BG_POS_CENTERRIGHT, lang.menuBgPosCenterRight.c_str());
                ModifyMenuW(hPosMenu, 8, MF_BYPOSITION | MF_STRING, IDM_VIEW_BG_POS_BOTTOMLEFT, lang.menuBgPosBottomLeft.c_str());
                ModifyMenuW(hPosMenu, 9, MF_BYPOSITION | MF_STRING, IDM_VIEW_BG_POS_BOTTOMCENTER, lang.menuBgPosBottomCenter.c_str());
                ModifyMenuW(hPosMenu, 10, MF_BYPOSITION | MF_STRING, IDM_VIEW_BG_POS_BOTTOMRIGHT, lang.menuBgPosBottomRight.c_str());
                ModifyMenuW(hPosMenu, 12, MF_BYPOSITION | MF_STRING, IDM_VIEW_BG_POS_TILE, lang.menuBgPosTile.c_str());
                ModifyMenuW(hPosMenu, 13, MF_BYPOSITION | MF_STRING, IDM_VIEW_BG_POS_STRETCH, lang.menuBgPosStretch.c_str());
                ModifyMenuW(hPosMenu, 14, MF_BYPOSITION | MF_STRING, IDM_VIEW_BG_POS_FIT, lang.menuBgPosFit.c_str());
                ModifyMenuW(hPosMenu, 15, MF_BYPOSITION | MF_STRING, IDM_VIEW_BG_POS_FILL, lang.menuBgPosFill.c_str());
            }
        }

        ModifyMenuW(hViewMenu, 9, MF_BYPOSITION | MF_STRING, IDM_VIEW_TRANSPARENCY, lang.menuTransparency.c_str());
        ModifyMenuW(hViewMenu, 10, MF_BYPOSITION | MF_STRING, IDM_VIEW_ALWAYSONTOP, lang.menuAlwaysOnTop.c_str());

        // 图标子菜单：按「哪个 popup 里含 IDM_VIEW_ICON_CHANGE」定位，不硬编码位置
        for (int i = 0; i < GetMenuItemCount(hViewMenu); ++i)
        {
            HMENU hSub = GetSubMenu(hViewMenu, i);
            if (!hSub)
                continue;
            if (GetMenuState(hSub, IDM_VIEW_ICON_CHANGE, MF_BYCOMMAND) == (UINT)-1)
                continue;
            ModifyMenuW(hViewMenu, i, MF_BYPOSITION | MF_STRING | MF_POPUP,
                        reinterpret_cast<UINT_PTR>(hSub), lang.menuIcon.c_str());
            ModifyMenuW(hSub, IDM_VIEW_ICON_CHANGE, MF_BYCOMMAND | MF_STRING, IDM_VIEW_ICON_CHANGE, lang.menuIconChange.c_str());
            ModifyMenuW(hSub, IDM_VIEW_ICON_RESET, MF_BYCOMMAND | MF_STRING, IDM_VIEW_ICON_RESET, lang.menuIconReset.c_str());
            break;
        }
    }

    HMENU hLangMenu = GetSubMenu(hMenu, 4);
    if (hLangMenu)
    {
        for (const auto &item : kLangMenuItems)
            ModifyMenuW(hLangMenu, item.id, MF_BYCOMMAND | MF_STRING, item.id,
                        (lang.*item.text).c_str());
    }

    HMENU hHelpMenu = GetSubMenu(hMenu, 5);
    if (hHelpMenu)
    {
        ModifyMenuW(hHelpMenu, 0, MF_BYPOSITION | MF_STRING, IDM_HELP_CHECKUPDATES, lang.menuCheckUpdates.c_str());
        ModifyMenuW(hHelpMenu, 1, MF_BYPOSITION | MF_STRING, IDM_HELP_ABOUT, lang.menuAbout.c_str());
    }

    HMENU hBuildMenu = GetSubMenu(hMenu, 6);
    if (hBuildMenu)
        ModifyMenuW(hMenu, 6, MF_BYPOSITION | MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(hBuildMenu), lang.menuBuild.c_str());
    RebuildBuildMenu();

    MenuSkinApplyAll(g_hwndMain);
    DrawMenuBar(g_hwndMain);
}

void RebuildBuildMenu()
{
    HMENU hMenu = GetMenu(g_hwndMain);
    if (!hMenu)
        return;
    HMENU hBuild = GetSubMenu(hMenu, 6);
    if (!hBuild)
        return;
    const auto &lang = GetLangStrings();

    // Rebuild the whole Build submenu from scratch.
    MenuSkinFree(hBuild);   // 释放自绘数据并把每一项还原成 MFT_STRING
    while (GetMenuItemCount(hBuild) > 0)
        DeleteMenu(hBuild, 0, MF_BYPOSITION);

    // 1) Current language info line (top, grayed).
    std::wstring info = lang.menuBuildCurrentLang;
    const wchar_t *dn = EditorLanguageName();
    if (dn)
        info += dn;
    AppendMenuW(hBuild, MF_STRING | MF_GRAYED, 0, info.c_str());

    // 2) Compile current file (F7).
    AppendMenuW(hBuild, MF_STRING, IDM_BUILD_COMPILE, lang.menuCompile.c_str());

    // 2b) Ignore checks, force compile.
    AppendMenuW(hBuild, MF_STRING, IDM_BUILD_FORCE, lang.menuForceCompile.c_str());

    AppendMenuW(hBuild, MF_SEPARATOR, 0, nullptr);

    // 3) Compile settings...
    AppendMenuW(hBuild, MF_STRING, IDM_BUILD_SETTINGS, lang.menuCompileSettings.c_str());

    // 3b) Library browser (C++ / Python packages).
    AppendMenuW(hBuild, MF_STRING, IDM_BUILD_LIBS, lang.menuLibDownload.c_str());

    AppendMenuW(hBuild, MF_SEPARATOR, 0, nullptr);

    // 4) One button per known language; current one is checked.
    const wchar_t *curId = EditorLanguageId();
    const int count = LanguageCount();
    for (int i = 0; i < count; ++i)
    {
        const LanguageDef *L = LanguageByIndex(i);
        if (!L || !L->display)
            continue;
        UINT flags = MF_STRING;
        if (curId && L->id && wcscmp(L->id, curId) == 0)
            flags |= MF_CHECKED;
        AppendMenuW(hBuild, flags, IDM_BUILD_LANG_BASE + i, L->display);
    }

    MenuSkinApply(hBuild);
    DrawMenuBar(g_hwndMain);
}

void UpdateLanguageMenu()
{
    HMENU hMenu = GetMenu(g_hwndMain);
    if (!hMenu)
        return;

    HMENU hLangMenu = GetSubMenu(hMenu, 4);
    if (!hLangMenu)
        return;

    LangID currentLang = GetCurrentLanguage();
    for (const auto &item : kLangMenuItems)
        CheckMenuItem(hLangMenu, item.id, (currentLang == item.lang) ? MF_CHECKED : MF_UNCHECKED);
}
