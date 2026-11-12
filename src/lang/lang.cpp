#include "lang.h"
#include "en.h"
#include "ja.h"
#include "zh.h"
#include "ru.h"
#include "es.h"
#include "zh_tw.h"
#include "ko.h"
#include "fr.h"
#include "de.h"
#include "pt.h"
#include "ar.h"
#include "hi.h"
#include "it.h"
#include "nl.h"
#include "tr.h"
#include "core/storage.h"
#include <windows.h>

static LangID g_currentLang = LangID::EN;
static const LangStrings* g_currentStrings = &g_langEN;
static const wchar_t *kLangKey = L"Software\\LegacyNotepad";

LangID LoadLanguageSetting()
{
    DWORD value = 0;
    if (StorageGetDword(kLangKey, L"Language", &value))
    {
        // 旧版本存过越界值（或注册表被手工改成别的数）时回落到英语
        if (value < static_cast<DWORD>(LangID::COUNT))
            return static_cast<LangID>(value);
    }
    return LangID::EN;
}

void SaveLanguageSetting()
{
    StorageSetDword(kLangKey, L"Language", static_cast<DWORD>(g_currentLang));
}

void InitLanguage()
{
    LangID savedLang = LoadLanguageSetting();
    SetLanguage(savedLang);
}

void SetLanguage(LangID lang)
{
    g_currentLang = lang;

    switch (lang)
    {
    case LangID::JA:
        g_currentStrings = &g_langJA;
        break;
    case LangID::ZH_CN:
        g_currentStrings = &g_langZH;
        break;
    case LangID::RU:
        g_currentStrings = &g_langRU;
        break;
    case LangID::ES:
        g_currentStrings = &g_langES;
        break;
    case LangID::ZH_TW:
        g_currentStrings = &g_langZH_TW;
        break;
    case LangID::KO:
        g_currentStrings = &g_langKO;
        break;
    case LangID::FR:
        g_currentStrings = &g_langFR;
        break;
    case LangID::DE:
        g_currentStrings = &g_langDE;
        break;
    case LangID::PT:
        g_currentStrings = &g_langPT;
        break;
    case LangID::AR:
        g_currentStrings = &g_langAR;
        break;
    case LangID::HI:
        g_currentStrings = &g_langHI;
        break;
    case LangID::IT:
        g_currentStrings = &g_langIT;
        break;
    case LangID::NL:
        g_currentStrings = &g_langNL;
        break;
    case LangID::TR:
        g_currentStrings = &g_langTR;
        break;
    case LangID::EN:
    default:
        g_currentStrings = &g_langEN;
        break;
    }

    SaveLanguageSetting();
}

LangID GetCurrentLanguage()
{
    return g_currentLang;
}

const LangStrings& GetLangStrings()
{
    return *g_currentStrings;
}

const std::wstring& GetString([[maybe_unused]] const std::wstring& key)
{
    static std::wstring empty;
    return empty;
}
