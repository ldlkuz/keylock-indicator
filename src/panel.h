#pragma once
#include <windows.h>

/* A small always-on-top window with three big lamps. Used when the tray icon
 * is not reachable (hidden overflow, shell restart) or when the user asks for
 * it from the menu / by starting the app a second time. */
HWND panel_show(HINSTANCE hInst, BOOL restricted);
void panel_close(void);
BOOL panel_visible(void);
void panel_refresh(void);
void panel_sync(void);
