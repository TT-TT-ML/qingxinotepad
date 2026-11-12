/*
  Multi-document tabs (buffer-swap model).

  The editor control itself stays single; each tab owns its own text,
  path, encoding and modified flag. Switching tabs swaps the buffer into
  the editor. A custom-drawn tab strip at the top of the main window is
  the navigation bar: every tab shows the first five characters of its
  file name plus "...", and a status dot (green = saved, orange = unsaved).
*/

#pragma once

#include <windows.h>
#include <string>
#include "core/types.h"

void TabsInit();
HWND TabsCreateBar(HWND parent);
void TabsMoveBar(int x, int y, int w, int h);
int TabsBarHeight();

int TabsActiveIndex();
int TabsCount();

void TabsSyncActive();                 // pull editor text/state into the active tab
void TabsSwitchTo(int index);
void TabsAddDocument(const std::wstring &path, const std::wstring &text,
                     Encoding enc, LineEnding le, bool modified, bool activate);
bool TabsFindPath(const std::wstring &path, int &index);
void TabsClose(int index);
void TabsMarkActiveModified();
void TabsNotifySaved();                // after a successful save: refresh path/name + dot
void TabsRefresh();                    // repaint the strip

bool TabsConfirmExit();                // asks once if any tab is dirty
