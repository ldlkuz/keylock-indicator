#include "config.h"

#include <stdio.h>

#define APP_KEY    L"Software\\KeyLockIndicator"
#define RUN_KEY    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALUE  L"KeyLockIndicator"
#define TASK_NAME  L"KeyLockIndicator"

/* Windows will not auto-start a Run-key entry whose executable demands
 * elevation, so autostart is implemented as a logon scheduled task with the
 * highest run level. The program itself is elevated, so creating the task
 * needs no extra prompt. */

static BOOL run_hidden(wchar_t *cmdline, DWORD *exit_code)
{
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    BOOL started;
    DWORD code = 1;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));

    started = CreateProcessW(NULL, cmdline, NULL, NULL, FALSE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (!started) {
        if (exit_code)
            *exit_code = GetLastError();
        return FALSE;
    }

    WaitForSingleObject(pi.hProcess, 8000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exit_code)
        *exit_code = code;
    return TRUE;
}

static BOOL task_exists(void)
{
    wchar_t cmd[256];
    DWORD code = 1;

    swprintf_s(cmd, ARRAYSIZE(cmd), L"schtasks.exe /query /tn \"%s\"", TASK_NAME);
    if (!run_hidden(cmd, &code))
        return FALSE;
    return code == 0;
}

/* Old versions wrote HKCU\...\Run; that never worked for an app that requires
 * elevation, so clean it up when we see it. */
static void remove_legacy_run_entry(void)
{
    HKEY key;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return;
    RegDeleteValueW(key, RUN_VALUE);
    RegCloseKey(key);
}

void config_autostart_refresh(void)
{
    remove_legacy_run_entry();
    config_dword_set(L"Autostart", task_exists() ? 1u : 0u);
}

BOOL config_autostart_get(void)
{
    return config_dword_get(L"Autostart", 0) != 0;
}

BOOL config_autostart_set(BOOL on)
{
    wchar_t cmd[MAX_PATH + 256];
    DWORD code = 1;

    remove_legacy_run_entry();

    if (on) {
        wchar_t exe[MAX_PATH];

        if (!GetModuleFileNameW(NULL, exe, MAX_PATH))
            return FALSE;
        swprintf_s(cmd, ARRAYSIZE(cmd),
                   L"schtasks.exe /create /tn \"%s\" /tr \"\\\"%s\\\"\" "
                   L"/sc onlogon /rl highest /f",
                   TASK_NAME, exe);
    } else {
        swprintf_s(cmd, ARRAYSIZE(cmd), L"schtasks.exe /delete /tn \"%s\" /f", TASK_NAME);
    }

    if (!run_hidden(cmd, &code))
        return FALSE;

    /* Deleting a task that does not exist is reported as failure; treat that
     * as "already off". */
    if (!on && code != 0)
        code = task_exists() ? 1u : 0u;

    if (code != 0)
        return FALSE;

    config_dword_set(L"Autostart", on ? 1u : 0u);
    return TRUE;
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
