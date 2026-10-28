/* Test harness for config.c autostart (not part of the shipped app).
 * usage: autostart_probe <on|off|status> <result-file>
 * Runs elevated so the scheduled-task calls behave like they do in the app. */
#include <windows.h>
#include <stdio.h>

#include "../src/config.h"

static void wlog(const wchar_t *path, const wchar_t *fmt, ...)
{
    wchar_t line[512];
    va_list ap;
    FILE *f = NULL;

    va_start(ap, fmt);
    _vsnwprintf_s(line, ARRAYSIZE(line), _TRUNCATE, fmt, ap);
    va_end(ap);

    if (_wfopen_s(&f, path, L"a, ccs=UTF-16LE") != 0 || !f)
        return;
    fwprintf(f, L"%s\n", line);
    fclose(f);
}

int wmain(int argc, wchar_t **argv)
{
    wchar_t exe[MAX_PATH] = L"";

    if (argc < 3)
        return 2;

    GetModuleFileNameW(NULL, exe, MAX_PATH);
    config_autostart_refresh();
    wlog(argv[2], L"before=%d exe=%s", (int)config_autostart_get(), exe);

    if (!wcscmp(argv[1], L"on")) {
        BOOL ok = config_autostart_set(TRUE);
        wlog(argv[2], L"set(on)=%d now=%d", (int)ok, (int)config_autostart_get());
        return ok ? 0 : 1;
    }
    if (!wcscmp(argv[1], L"off")) {
        BOOL ok = config_autostart_set(FALSE);
        wlog(argv[2], L"set(off)=%d now=%d", (int)ok, (int)config_autostart_get());
        return ok ? 0 : 1;
    }
    if (!wcscmp(argv[1], L"status")) {
        wlog(argv[2], L"status=%d", (int)config_autostart_get());
        return 0;
    }
    return 2;
}
