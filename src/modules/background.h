/*
   ▄████████  ▄██████▄     ▄████████  ▄█        ▄██████▄   ▄██████▄     ▄███████▄
  ███    ███ ███    ███   ███    ███ ███       ███    ███ ███    ███   ███    ███
  ███    █▀  ███    ███   ███    ███ ███       ███    ███ ███    ███   ███    ███
 ▄███▄▄▄     ███    ███  ▄███▄▄▄▄██▀ ███       ███    ███ ███    ███   ███    ███
▀▀███▀▀▀     ███    ███ ▀▀███▀▀▀▀▀   ███       ███    ███ ███    ███ ▀█████████▀
  ███        ███    ███ ▀███████████ ███       ███    ███ ███    ███   ███
  ███        ███    ███   ███    ███ ███▌    ▄ ███    ███ ███    ███   ███
  ███         ▀██████▀    ███    ███ █████▄▄██  ▀██████▀   ▀██████▀   ▄████▀
                          ███    ███ ▀

  Background image rendering with GDI+ support and multiple positioning modes.
  Supports tile, stretch, fit, fill, and nine anchor point positions.
*/

#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include "core/types.h"

extern Gdiplus::Image *g_bgImage;
extern HBITMAP g_bgBitmap;
extern int g_bgBitmapW;
extern int g_bgBitmapH;

void LoadBackgroundImage(const std::wstring &path);
void PaintBackground(HDC hdc, const RECT &rc);
void UpdateBackgroundBitmap(HWND hwnd);
void SetBackgroundPosition(BgPosition pos);
void ViewSelectBackground();
void ViewClearBackground();
void ViewBackgroundOpacity();
