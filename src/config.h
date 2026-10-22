#pragma once
#include <windows.h>

BOOL  config_autostart_get(void);
BOOL  config_autostart_set(BOOL on);

DWORD config_dword_get(const wchar_t *name, DWORD fallback);
BOOL  config_dword_set(const wchar_t *name, DWORD value);
