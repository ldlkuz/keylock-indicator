#include "config.h"

#include <stdio.h>

#define RUN_KEY    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define VALUE_NAME L"KeyLockIndicator"
#define APP_KEY    L"Software\\KeyLockIndicator"

BOOL config_autostart_get(void)
{
    wchar_t buf[1024];
    DWORD size = (DWORD)sizeof(buf);

    return RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, VALUE_NAME, RRF_RT_REG_SZ,
                        NULL, buf, &size) == ERROR_SUCCESS;
}

BOOL config_autostart_set(BOOL on)
{
    HKEY key;
    BOOL ok;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, NULL, 0, KEY_SET_VALUE,
                        NULL, &key, NULL) != ERROR_SUCCESS)
        return FALSE;

    if (on) {
        wchar_t path[MAX_PATH];
        wchar_t quoted[MAX_PATH + 8];

        if (!GetModuleFileNameW(NULL, path, MAX_PATH)) {
            RegCloseKey(key);
            return FALSE;
        }
        swprintf_s(quoted, ARRAYSIZE(quoted), L"\"%s\"", path);
        ok = RegSetValueExW(key, VALUE_NAME, 0, REG_SZ, (const BYTE *)quoted,
                            (DWORD)((wcslen(quoted) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        LSTATUS st = RegDeleteValueW(key, VALUE_NAME);
        ok = (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND);
    }

    RegCloseKey(key);
    return ok;
}

DWORD config_dword_get(const wchar_t *name, DWORD fallback)
{
    DWORD value = fallback;
    DWORD size = (DWORD)sizeof(value);

    if (RegGetValueW(HKEY_CURRENT_USER, APP_KEY, name, RRF_RT_REG_DWORD, NULL,
                     &value, &size) != ERROR_SUCCESS)
        return fallback;
    return value;
}

BOOL config_dword_set(const wchar_t *name, DWORD value)
{
    HKEY key;
    BOOL ok;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, APP_KEY, 0, NULL, 0, KEY_SET_VALUE,
                        NULL, &key, NULL) != ERROR_SUCCESS)
        return FALSE;
    ok = RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value,
                        (DWORD)sizeof(value)) == ERROR_SUCCESS;
    RegCloseKey(key);
    return ok;
}
