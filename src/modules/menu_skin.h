/*
  Owner-drawn menu skin.

  Every item — command, sub-menu parent, separator — is switched to
  MFT_OWNERDRAW and painted by us: themed background, full-row hover highlight,
  check mark, accelerator text in grey, disabled items dimmed, separator lines,
  and the sub-menu arrow. MENUINFO background is installed as well, so the
  padding behind the rows follows the theme. MenuSkinApplyAll() must be called
  after any menu rebuild (language switch, Build menu rebuild, recent files,
  theme change).
*/

#pragma once

#include <windows.h>

void MenuSkinApplyAll(HWND hwnd);
void MenuSkinApply(HMENU menu);
void MenuSkinFree(HMENU menu);
void MenuSkinApplyColors(HWND hwnd);
bool MenuSkinMeasure(MEASUREITEMSTRUCT *mis);
bool MenuSkinDraw(HWND hwnd, const DRAWITEMSTRUCT *dis);
