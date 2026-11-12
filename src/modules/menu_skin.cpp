/*
  Owner-drawn menu skin.

  Every item is switched to MFT_OWNERDRAW and painted by us: themed background,
  full-row hover highlight, check mark, accelerator text in grey, disabled items
  dimmed, separator lines, and the sub-menu arrow on parent items. The menu
  background brush is also installed (MENUINFO) so the popup padding and frame
  behind the items follow the theme instead of staying white.
  MenuSkinApplyAll() must be called after any menu rebuild (language switch,
  Build menu rebuild, recent files, theme change).
*/

#include "menu_skin.h"
#include "theme.h"
#include "core/globals.h"
#include <windows.h>
#include <string>

namespace
{

HFONT g_menuFont = nullptr;

// Per-item payload stored in dwItemData. A plain label string is not enough:
// separators and sub-menu parents have to be told apart at draw time, and a
// sub-menu parent keeps MF_POPUP, which would otherwise confuse the text path.
struct SkinItem
{
    std::wstring label;
    bool separator;
    bool submenu;
};

HFONT MenuFont()
{
    static bool made = false;
    if (made)
        return g_menuFont;
    made = true;
    LOGFONTW lf = {};
    lf.lfHeight = -15;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    const wchar_t *face = L"Microsoft YaHei UI";
    for (int i = 0; face[i] != L'\0' && i < LF_FACESIZE - 1; ++i)
        lf.lfFaceName[i] = face[i];
    lf.lfFaceName[LF_FACESIZE - 1] = L'\0';
    g_menuFont = CreateFontIndirectW(&lf);
    return g_menuFont;
}

void Fill(HDC dc, const RECT &rc, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &rc, b);
    DeleteObject(b);
}

SkinItem *DataOf(const DRAWITEMSTRUCT *dis)
{
    return reinterpret_cast<SkinItem *>(dis->itemData);
}

// barLevel == true means this menu is the window's menu bar: its own items are
// laid out and painted by the system (WM_NCPAINT / WM_UAHDRAWMENU take care of
// the theme), so only the popups hanging under them are skinned. Skinning the
// bar items here would force our minimum item width on them and make the bar
// wrap onto several rows.
void ApplyRecursive(HMENU menu, bool barLevel)
{
    if (!menu)
        return;
    int n = GetMenuItemCount(menu);
    for (int i = 0; i < n; ++i)
    {
        wchar_t text[512] = {};
        MENUITEMINFOW mi = {};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_FTYPE | MIIM_DATA;
        mi.dwTypeData = text;
        mi.cch = 511;
        if (!GetMenuItemInfoW(menu, static_cast<UINT>(i), TRUE, &mi))
            continue;

        HMENU sub = mi.hSubMenu;
        if (sub)
            ApplyRecursive(sub, false);

        if (barLevel)
            continue;

        // Already skinned: keep the existing payload.
        if ((mi.fType & MFT_OWNERDRAW) && mi.dwItemData)
            continue;

        SkinItem *item = new SkinItem;
        item->label = text;
        item->separator = (mi.fType & MFT_SEPARATOR) != 0;
        item->submenu = (sub != nullptr);

        MENUITEMINFOW set = {};
        set.cbSize = sizeof(set);
        set.fMask = MIIM_FTYPE | MIIM_DATA;
        set.fType = (mi.fType & MF_POPUP) | MFT_OWNERDRAW; // keep the popup bit
        set.dwItemData = reinterpret_cast<ULONG_PTR>(item);
        SetMenuItemInfoW(menu, static_cast<UINT>(i), TRUE, &set);
    }
}

} // namespace

void MenuSkinApply(HMENU menu)
{
    ApplyRecursive(menu, false);
}

// Frees every payload under this menu and puts the items back to plain
// MFT_STRING so a rebuild starts from a clean owner-draw state.
void MenuSkinFree(HMENU menu)
{
    if (!menu)
        return;
    int n = GetMenuItemCount(menu);
    for (int i = 0; i < n; ++i)
    {
        MENUITEMINFOW q = {};
        q.cbSize = sizeof(q);
        q.fMask = MIIM_SUBMENU | MIIM_DATA | MIIM_FTYPE;
        if (!GetMenuItemInfoW(menu, static_cast<UINT>(i), TRUE, &q))
            continue;
        if (q.hSubMenu)
            MenuSkinFree(q.hSubMenu);
        if (q.dwItemData && (q.fType & MFT_OWNERDRAW))
        {
            delete reinterpret_cast<SkinItem *>(q.dwItemData);
            MENUITEMINFOW c = {};
            c.cbSize = sizeof(c);
            c.fMask = MIIM_FTYPE | MIIM_DATA;
            c.fType = (q.fType & MF_POPUP) | MFT_STRING;
            c.dwItemData = 0;
            SetMenuItemInfoW(menu, static_cast<UINT>(i), TRUE, &c);
        }
    }
}

// Background brush for the popup padding / frame, so no white strip is left
// between our painted rows.
void MenuSkinApplyColors(HWND hwnd)
{
    HMENU menu = GetMenu(hwnd);
    if (!menu)
        return;
    MENUINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
    mi.hbrBack = IsDarkMode() ? g_hbrMenuDark : GetSysColorBrush(COLOR_MENU);
    SetMenuInfo(menu, &mi);
}

void MenuSkinApplyAll(HWND hwnd)
{
    HMENU menu = GetMenu(hwnd);
    if (!menu)
        return;
    ApplyRecursive(menu, true);   // 菜单栏本身交给系统排版，弹出菜单逐项自绘
    MenuSkinApplyColors(hwnd);
    DrawMenuBar(hwnd);
}

// Splits "Save(&S)\tCtrl+S" into the visible label (ampersands removed,
// mnemonic index remembered) and the accelerator text.
void SplitLabel(const std::wstring &label, std::wstring &mainText, std::wstring &accel, int &mnemonic)
{
    mainText.clear();
    accel.clear();
    mnemonic = -1;
    for (size_t i = 0; i < label.size(); ++i)
    {
        if (label[i] == L'\t')
        {
            for (size_t k = i + 1; k < label.size(); ++k)
                accel += label[k];
            break;
        }
        if (label[i] == L'&')
        {
            if (mnemonic < 0)
                mnemonic = static_cast<int>(mainText.size());
            continue;
        }
        mainText += label[i];
    }
}

bool MenuSkinMeasure(MEASUREITEMSTRUCT *mis)
{
    if (!mis || mis->CtlType != ODT_MENU)
        return false;

    SkinItem *item = reinterpret_cast<SkinItem *>(mis->itemData);
    if (item && item->separator)
    {
        mis->itemHeight = 9;
        mis->itemWidth = 8;
        return true;
    }

    mis->itemHeight = 28;

    // Width must be provided by us: the label lives in dwItemData, and the
    // default width would clip it.
    std::wstring label = item ? item->label : L"";
    std::wstring mainText;
    std::wstring accel;
    int mnemonic = -1;
    SplitLabel(label, mainText, accel, mnemonic);

    HDC dc = GetDC(nullptr);
    HFONT old = nullptr;
    if (dc)
    {
        HFONT useFont = MenuFont() ? MenuFont() : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        old = reinterpret_cast<HFONT>(SelectObject(dc, useFont));
    }
    SIZE msz = {0, 0};
    SIZE asz = {0, 0};
    if (dc)
    {
        GetTextExtentPoint32W(dc, mainText.c_str(), static_cast<int>(mainText.size()), &msz);
        if (!accel.empty())
            GetTextExtentPoint32W(dc, accel.c_str(), static_cast<int>(accel.size()), &asz);
        if (old)
            SelectObject(dc, old);
        ReleaseDC(nullptr, dc);
    }

    int width = 26 + msz.cx + 34;
    if (!accel.empty())
        width += asz.cx + 64;
    if (item && item->submenu)
        width += 22; // room for our own arrow
    if (width < 190)
        width = 190;
    mis->itemWidth = static_cast<UINT>(width);
    return true;
}

bool MenuSkinDraw(HWND hwnd, const DRAWITEMSTRUCT *dis)
{
    if (!dis || dis->CtlType != ODT_MENU)
        return false;

    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;

    const bool dark = IsDarkMode();
    const bool sel = (dis->itemState & ODS_SELECTED) != 0;
    const bool grayed = (dis->itemState & (ODS_GRAYED | ODS_DISABLED)) != 0;
    const bool checked = (dis->itemState & ODS_CHECKED) != 0;

    COLORREF menuBg = dark ? RGB(45, 45, 45) : RGB(250, 250, 250);
    COLORREF bg = sel ? RGB(0, 120, 215) : menuBg;
    COLORREF fg;
    if (grayed)
        fg = dark ? RGB(120, 120, 120) : RGB(150, 150, 150);
    else
        fg = sel ? RGB(255, 255, 255) : (dark ? RGB(235, 235, 235) : RGB(25, 25, 25));

    SkinItem *item = DataOf(dis);

    if (item && item->separator)
    {
        Fill(dc, rc, menuBg);
        int y = rc.top + (rc.bottom - rc.top) / 2;
        RECT line = {rc.left + 12, y, rc.right - 12, y + 1};
        Fill(dc, line, dark ? RGB(80, 80, 80) : RGB(205, 205, 205));
        return true;
    }

    Fill(dc, rc, bg);

    std::wstring label;
    if (item)
        label = item->label;
    else
    {
        wchar_t buf[512] = {};
        MENUITEMINFOW mi = {};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_STRING;
        mi.dwTypeData = buf;
        mi.cch = 511;
        if (GetMenuItemInfoW(GetMenu(hwnd), dis->itemID, FALSE, &mi))
            label = buf;
    }

    std::wstring mainText;
    std::wstring accel;
    int mnemonic = -1;
    SplitLabel(label, mainText, accel, mnemonic);

    HFONT useFont = MenuFont() ? MenuFont() : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HFONT old = reinterpret_cast<HFONT>(SelectObject(dc, useFont));
    SetBkMode(dc, TRANSPARENT);

    if (checked)
    {
        COLORREF tickCol = sel ? RGB(255, 255, 255) : RGB(0, 120, 215);
        SetTextColor(dc, tickCol);
        RECT cr = {rc.left + 6, rc.top, rc.left + 24, rc.bottom};
        DrawTextW(dc, L"\u2713", -1, &cr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
    }

    SetTextColor(dc, fg);
    RECT tr = {rc.left + 26, rc.top, rc.right - 12, rc.bottom};
    DrawTextW(dc, mainText.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);

    // Underline the mnemonic character, the way a native menu does.
    if (mnemonic >= 0 && mnemonic < static_cast<int>(mainText.size()))
    {
        SIZE pre = {0, 0};
        SIZE ch = {0, 0};
        std::wstring prefix = mainText.substr(0, static_cast<size_t>(mnemonic));
        GetTextExtentPoint32W(dc, prefix.c_str(), static_cast<int>(prefix.size()), &pre);
        GetTextExtentPoint32W(dc, mainText.c_str() + mnemonic, 1, &ch);
        int x = rc.left + 26 + pre.cx;
        int y = rc.bottom - 7;
        HPEN pen = CreatePen(PS_SOLID, 1, fg);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, x, y, nullptr);
        LineTo(dc, x + ch.cx, y);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }

    if (!accel.empty())
    {
        SetTextColor(dc, sel ? RGB(225, 235, 245) : (dark ? RGB(140, 140, 140) : RGB(120, 120, 120)));
        RECT ar = {rc.left + 26, rc.top, rc.right - 22, rc.bottom};
        DrawTextW(dc, accel.c_str(), -1, &ar, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
    }

    // Sub-menu arrow: drawn by us, because these parent rows are owner-drawn
    // now (previously they were native and stayed white in dark mode).
    if (item && item->submenu)
    {
        int cy = (rc.top + rc.bottom) / 2;
        int ax = rc.right - 16;
        POINT tri[3] = {{ax, cy - 4}, {ax, cy + 4}, {ax + 6, cy}};
        HBRUSH br = CreateSolidBrush(fg);
        HGDIOBJ oldBr = SelectObject(dc, br);
        Polygon(dc, tri, 3);
        SelectObject(dc, oldBr);
        DeleteObject(br);
    }

    SelectObject(dc, old);
    return true;
}
