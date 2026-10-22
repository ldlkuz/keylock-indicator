#pragma once
#include <windows.h>

#define WM_TRAY_MSG (WM_APP + 1)

enum {
    IDM_AUTOSTART   = 1001,
    IDM_LOWLATENCY  = 1002,
    IDM_REFRESH     = 1003,
    IDM_ABOUT       = 1004,
    IDM_EXIT        = 1005,
    IDM_PANEL       = 1006,
    IDM_COLOR_FIRST = 1100
};

BOOL tray_add(HWND hwnd, HICON icon, const wchar_t *tip);
BOOL tray_update(HICON icon, const wchar_t *tip);
BOOL tray_remove(HWND hwnd);
int  tray_menu(HWND hwnd, BOOL autostart, BOOL low_latency, BOOL panel_visible);
