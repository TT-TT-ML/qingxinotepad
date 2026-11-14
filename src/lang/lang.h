#pragma once
#include <windows.h>
#include <string>
enum class LangID
{
    EN,
    JA,
    ZH_CN,   //新增简体中文ID
    RU,      //新增俄语
    ES,      //新增西班牙语
    ZH_TW,   //新增繁体中文
    KO,      //新增韩语
    FR,      //新增法语
    DE,      //新增德语
    PT,      //新增葡萄牙语
    AR,      //新增阿拉伯语
    HI,      //新增印地语
    IT,      //新增意大利语
    NL,      //新增荷兰语
    TR,      //新增土耳其语
    COUNT    //必须保留在最后（顺序与注册表里已保存的语言值是绑定的，新语言只能往后追加）
};
struct LangStrings
{
    std::wstring appName;
    std::wstring untitled;
    std::wstring menuFile;
    std::wstring menuNew;
    std::wstring menuOpen;
    std::wstring menuSave;
    std::wstring menuSaveAs;
    std::wstring menuPrint;
    std::wstring menuPageSetup;
    std::wstring menuExit;
    std::wstring menuRecentFiles;
    std::wstring menuEdit;
    std::wstring menuUndo;
    std::wstring menuRedo;
    std::wstring menuCut;
    std::wstring menuCopy;
    std::wstring menuPaste;
    std::wstring menuDelete;
    std::wstring menuFind;
    std::wstring menuFindNext;
    std::wstring menuFindPrev;
    std::wstring menuReplace;
    std::wstring menuGoTo;
    std::wstring menuSelectAll;
    std::wstring menuTimeDate;
    std::wstring menuFormat;
    std::wstring menuWordWrap;
    std::wstring menuFont;
    std::wstring menuView;
    std::wstring menuZoomIn;
    std::wstring menuZoomOut;
    std::wstring menuZoomDefault;
    std::wstring menuStatusBar;
    std::wstring menuDarkMode;
    std::wstring menuBackground;
    std::wstring menuBgSelect;
    std::wstring menuBgClear;
    std::wstring menuBgOpacity;
    std::wstring menuBgPosition;
    std::wstring menuBgPosTopLeft;
    std::wstring menuBgPosTopCenter;
    std::wstring menuBgPosTopRight;
    std::wstring menuBgPosCenterLeft;
    std::wstring menuBgPosCenter;
    std::wstring menuBgPosCenterRight;
    std::wstring menuBgPosBottomLeft;
    std::wstring menuBgPosBottomCenter;
    std::wstring menuBgPosBottomRight;
    std::wstring menuBgPosTile;
    std::wstring menuBgPosStretch;
    std::wstring menuBgPosFit;
    std::wstring menuBgPosFill;
    std::wstring menuTransparency;
    std::wstring menuAlwaysOnTop;
    std::wstring menuHelp;
    std::wstring menuAbout;
    std::wstring menuCheckUpdates;
    std::wstring menuLanguage;
    std::wstring menuLangEnglish;
    std::wstring menuLangJapanese;
    std::wstring menuLangChinese;  //新增：简体中文菜单显示文字

    std::wstring dialogFind;
    std::wstring dialogFindReplace;
    std::wstring dialogGoTo;
    std::wstring dialogTransparency;
    std::wstring dialogFindLabel;
    std::wstring dialogReplaceLabel;
    std::wstring dialogFindNext;
    std::wstring dialogReplace;
    std::wstring dialogReplaceAll;
    std::wstring dialogClose;
    std::wstring dialogLineNumber;
    std::wstring dialogOK;
    std::wstring dialogCancel;
    std::wstring dialogOpacityLabel;
    std::wstring msgCannotFind;
    std::wstring msgSaveChanges;
    std::wstring msgCannotOpenFile;
    std::wstring msgCannotSaveFile;
    std::wstring msgError;
    std::wstring msgAbout;
    std::wstring statusLn;
    std::wstring statusCol;
    std::wstring encodingUTF8;
    std::wstring encodingUTF8BOM;
    std::wstring encodingUTF16LE;
    std::wstring encodingUTF16BE;
    std::wstring encodingANSI;
    std::wstring lineEndingCRLF;
    std::wstring lineEndingLF;
    std::wstring lineEndingCR;
    std::wstring menuBuild;         // 编译菜单
    std::wstring menuCompile;       // 编译当前文件
    std::wstring menuCompileSettings; // 编译设置
    std::wstring menuBuildCurrentLang; // 当前语言（Build 菜单顶部提示前缀）
    std::wstring menuForceCompile;  // 无视检查强制编译（追加在末尾，保持聚合初始化对齐）
    std::wstring menuEncoding;      // Format 菜单：编码子菜单
    std::wstring menuLineEnding;    // Format 菜单：行尾子菜单
    std::wstring dialogMatchCase;   // 查找/替换：区分大小写
    std::wstring dialogDirection;   // 查找：方向
    std::wstring dialogUp;          // 向上
    std::wstring dialogDown;        // 向下
    std::wstring msgSaveFailed;     // 保存失败提示
    std::wstring menuLangRussian;   // 语言菜单：俄语（追加在末尾，保持聚合初始化对齐）
    std::wstring menuLangSpanish;   // 语言菜单：西班牙语
    std::wstring menuIcon;          // 查看菜单：图标子菜单
    std::wstring menuIconChange;    // 更换图标
    std::wstring menuIconReset;     // 恢复默认图标
    std::wstring menuLibDownload;   // 编译菜单：开发库下载
    std::wstring menuLangKorean;    // 语言菜单：韩语
    std::wstring menuLangFrench;    // 语言菜单：法语
    std::wstring menuLangGerman;    // 语言菜单：德语
    std::wstring menuLangPortuguese;// 语言菜单：葡萄牙语
    std::wstring menuLangChineseTW; // 语言菜单：繁体中文
    std::wstring menuLangArabic;    // 语言菜单：阿拉伯语（追加在末尾，保证聚合初始化对齐）
    std::wstring menuLangHindi;     // 语言菜单：印地语
    std::wstring menuLangItalian;   // 语言菜单：意大利语
    std::wstring menuLangDutch;     // 语言菜单：荷兰语
    std::wstring menuLangTurkish;   // 语言菜单：土耳其语
    std::wstring menuConsole;       // 控制台菜单：菜单标题
    std::wstring menuCompileRun;    // 编译菜单：编译并运行
    std::wstring menuConsoleOpen;   // 控制台菜单：命令行窗口
    std::wstring menuConsoleClear;  // 控制台菜单：清空输出
    std::wstring menuConsoleClose;  // 控制台菜单：关闭命令行窗口
    std::wstring menuConsoleSettings; // 控制台菜单：设置…
    std::wstring menuCodeCheck;      // 编译菜单：代码检查开关
};
void InitLanguage();
void SetLanguage(LangID lang);
void SaveLanguageSetting();
LangID LoadLanguageSetting();
LangID GetCurrentLanguage();
const LangStrings &GetLangStrings();
const std::wstring &GetString(const std::wstring &key);