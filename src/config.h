#pragma once
#include <windows.h>

BOOL  config_autostart_get(void);
void  config_autostart_refresh(void);
BOOL  config_autostart_set(BOOL on);

DWORD config_dword_get(const wchar_t *name, DWORD fallback);
BOOL  config_dword_set(const wchar_t *name, DWORD value);
