/* KeyLockIndicator - taskbar Caps/Num/Scroll indicator.
 * Win32 + C11, single file exe, no runtime dependencies. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "icon.h"
#include "keystate.h"
#include "palette.h"
#include "panel.h"
#include "resource.h"
#include "tray.h"

#define TIMER_POLL          1
#define TIMER_TRAY_RETRY    2
#define APP_TITLE           L"键盘指示灯"

static const wchar_t kClassName[] = L"KeyLockIndicatorHiddenWnd";
static const wchar_t kMutexName[]  = L"Local\\KeyLockIndicator.SingleInstance";

static HWND       g_hwnd;
static HINSTANCE  g_inst;
static UINT       g_msg_taskbar_created;
static UINT       g_msg_show_panel;
static IconSet    g_icons;
static LightState g_state;
static BOOL       g_tray_added;
static BOOL       g_force_panel;
static int        g_tray_retries;
static BOOL       g_dark;
static int        g_icon_size;
static int        g_icon_dpi;
static HWND       g_tray_wnd;

static BOOL taskbar_is_dark(void);
static void build_tooltip(LightState s, wchar_t *buf, size_t cch);
static void rebuild_icons(void);
static void update_tray(void);
static int  run_diag(const wchar_t *dir);
static BOOL process_is_low_integrity(void);
static int  run_selftest(const wchar_t *dir);
static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

/* ------------------------------------------------------------------ */
/* theme / text helpers                                                */

static BOOL taskbar_is_dark(void)
{
    DWORD light = 1;
    DWORD size = (DWORD)sizeof(light);

    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, NULL,
                     &light, &size) != ERROR_SUCCESS)
        light = 1;
    return light ? FALSE : TRUE;
}

static void build_tooltip(LightState s, wchar_t *buf, size_t cch)
{
    swprintf_s(buf, cch, L"Caps Lock: %s\nNum Lock: %s\nScroll Lock: %s",
               s.caps ? L"开" : L"关",
               s.num ? L"开" : L"关",
               s.scroll ? L"开" : L"关");
}

static void report_tray_failure(DWORD err)
{
    wchar_t text[1024];

    swprintf_s(text, ARRAYSIZE(text),
               L"图标没能放进任务栏（错误码 %lu）。\n\n"
               L"常见原因：\n"
               L"  1. 资源管理器没在运行，或正在重启\n"
               L"  2. 程序以受限权限运行，比如被别的程序带起来\n"
               L"  3. 安全软件拦了托盘注册\n\n"
               L"程序会继续重试。",
               (unsigned long)err);
    MessageBoxW(NULL, text, APP_TITLE, MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
}

/* ------------------------------------------------------------------ */
/* icons / tray                                                        */

/* The tray lives on the taskbar, which may sit on any monitor with any DPI.
 * Size the icon for that monitor instead of the primary one. */
static int taskbar_dpi(void)
{
    if (!g_tray_wnd || !IsWindow(g_tray_wnd))
        g_tray_wnd = FindWindowW(L"Shell_TrayWnd", NULL);

    if (g_tray_wnd) {
        UINT dpi = GetDpiForWindow(g_tray_wnd);
        if (dpi)
            return (int)dpi;
    }
    return (int)GetDpiForSystem();
}

static void rebuild_icons(void)
{
    int dpi, size;

    icons_destroy(&g_icons);
    g_dark = taskbar_is_dark();

    dpi = taskbar_dpi();
    size = (int)GetSystemMetricsForDpi(SM_CXSMICON, (UINT)dpi);
    if (size < 12)
        size = 16;
    if (size > 64)
        size = 64;

    g_icon_dpi = dpi;
    g_icon_size = size;

    if (!icons_build(&g_icons, size, g_dark))
        icons_build(&g_icons, 16, FALSE);
}

static void update_tray(void)
{
    wchar_t tip[160];

    if (!g_icons.hicon[0])
        return;

    build_tooltip(g_state, tip, ARRAYSIZE(tip));
    if (g_tray_added) {
        tray_update(g_icons.hicon[lights_index(g_state)], tip);
        return;
    }

    /* A sandboxed process (low integrity) can never talk to the shell tray;
     * retrying is pointless and only spams the log. */
    if (g_tray_retries == 0 && process_is_low_integrity()) {
        panel_show(g_inst, TRUE);
        return;
    }

    if (g_tray_retries > 30)
        return;

    g_tray_added = tray_add(g_hwnd, g_icons.hicon[lights_index(g_state)], tip);
    if (g_tray_added) {
        g_tray_retries = 0;
        KillTimer(g_hwnd, TIMER_TRAY_RETRY);
    } else {
        DWORD err = GetLastError();

        ++g_tray_retries;
        if (g_tray_retries == 3) {
            /* Keep the app usable without the tray: show the lamp panel. */
            panel_show(g_inst, FALSE);
        }
        if (g_tray_retries == 30)
            report_tray_failure(err);
        if (g_tray_retries <= 30) {
            /* The shell may still be starting: retry for about a minute. */
            SetTimer(g_hwnd, TIMER_TRAY_RETRY, 2000, NULL);
        } else {
            KillTimer(g_hwnd, TIMER_TRAY_RETRY);
        }
    }
}

/* ------------------------------------------------------------------ */
/* window procedure                                                    */

static void toggle_panel(void)
{
    if (panel_visible())
        panel_close();
    else
        panel_show(g_inst, process_is_low_integrity());
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_msg_taskbar_created && msg == g_msg_taskbar_created) {
        /* Explorer restarted: the icon has to be added again. */
        g_tray_added = FALSE;
        g_tray_retries = 0;
        update_tray();
        return 0;
    }

    if (g_msg_show_panel && msg == g_msg_show_panel) {
        /* A second instance was started: give the user something visible. */
        panel_show(g_inst, process_is_low_integrity());
        return 0;
    }

    switch (msg) {
    case WM_TIMER:
        if (wp == TIMER_POLL) {
            LightState now = keystate_sample();

            /* The taskbar can move to a monitor with a different DPI. */
            if (taskbar_dpi() != g_icon_dpi) {
                rebuild_icons();
                update_tray();
            }

            if (!keystate_equal(now, g_state)) {
                g_state = now;
                update_tray();
            }
        } else if (wp == TIMER_TRAY_RETRY) {
            if (!g_tray_added)
                update_tray();
        }
        return 0;

    case WM_KEYSTATE_CHANGED: {
        LightState now = keystate_sample();

        if (!keystate_equal(now, g_state)) {
            g_state = now;
            update_tray();
        }
        panel_sync();
        return 0;
    }

    case WM_TRAY_MSG:
        switch (LOWORD(lp)) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: {
            int cmd = tray_menu(hwnd, config_autostart_get(), keystate_hook_active(),
                                panel_visible());

            switch (cmd) {
            case IDM_AUTOSTART: {
                BOOL want = config_autostart_get() ? FALSE : TRUE;
                config_autostart_set(want);
            } break;
            case IDM_LOWLATENCY: {
                BOOL want = keystate_hook_active() ? FALSE : TRUE;

                if (want && !keystate_hook_install())
                    break;
                if (!want)
                    keystate_hook_remove();
                config_dword_set(L"LowLatency", want ? 1 : 0);
                /* The hook reacts instantly, so the fallback poll can be slow;
                 * without it the poll is the only source, keep it tight. */
                SetTimer(hwnd, TIMER_POLL, want ? 1000 : 200, NULL);
            } break;
            case IDM_PANEL:
                toggle_panel();
                break;
            case IDM_REFRESH:
                g_state = keystate_sample();
                update_tray();
                break;
            case IDM_ABOUT:
                MessageBoxW(hwnd,
                            L"键盘指示灯 v0.1\n\n"
                            L"托盘图标是三根横条，从上到下依次对应：\n"
                            L"Num Lock / Caps Lock / Scroll Lock\n\n"
                            L"绿色 = 开启，灰色 = 关闭。\n"
                            L"左键单击图标可打开/收起三灯面板。",
                            APP_TITLE, MB_OK | MB_ICONINFORMATION);
                break;
            case IDM_EXIT:
                DestroyWindow(hwnd);
                break;
            default:
                if (cmd >= IDM_COLOR_FIRST && cmd < IDM_COLOR_FIRST + palette_count()) {
                    palette_set(cmd - IDM_COLOR_FIRST);
                    config_dword_set(L"Color", (DWORD)palette_get());
                    rebuild_icons();
                    update_tray();
                    panel_refresh();
                }
                break;
            }
        } break;

        case WM_LBUTTONUP:
            g_state = keystate_sample();
            update_tray();
            toggle_panel();
            break;
        default:
            break;
        }
        return 0;

    case WM_SETTINGCHANGE: {
        BOOL dark = taskbar_is_dark();
        int size = GetSystemMetrics(SM_CXSMICON);
        if (dark != g_dark || size != g_icon_size) {
            rebuild_icons();
            update_tray();
        }
        return 0;
    }

    case WM_THEMECHANGED:
    case WM_DISPLAYCHANGE:
        rebuild_icons();
        update_tray();
        return 0;

    case WM_ENDSESSION:
        if (wp)
            tray_remove(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_POLL);
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/* selftest                                                            */

/* Tray APIs are rejected (ERROR_ACCESS_DENIED) when the process runs below
 * the shell's integrity level, e.g. inside a restricted agent sandbox. */
static BOOL process_is_low_integrity(void)
{
    HANDLE token = NULL;
    BOOL low = FALSE;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return FALSE;

    {
        DWORD len = 0;
        GetTokenInformation(token, TokenIntegrityLevel, NULL, 0, &len);
        if (len) {
            TOKEN_MANDATORY_LABEL *tml = (TOKEN_MANDATORY_LABEL *)malloc(len);
            if (tml && GetTokenInformation(token, TokenIntegrityLevel, tml, len, &len)) {
                DWORD count = *GetSidSubAuthorityCount(tml->Label.Sid);
                if (count > 0) {
                    DWORD rid = *GetSidSubAuthority(tml->Label.Sid, count - 1);
                    low = rid < SECURITY_MANDATORY_MEDIUM_RID;
                }
            }
            free(tml);
        }
    }
    CloseHandle(token);
    return low;
}

static int run_selftest(const wchar_t *dir)
{
    static const int sizes[6] = {16, 20, 24, 32, 64, 128};
    BOOL dark = taskbar_is_dark();
    wchar_t path[MAX_PATH];
    FILE *f = NULL;
    BOOL pass = TRUE;
    int si, i;

    CreateDirectoryW(dir, NULL);
    if (swprintf_s(path, ARRAYSIZE(path), L"%s\\selftest.txt", dir) < 0)
        return 2;
    if (_wfopen_s(&f, path, L"wb") != 0 || !f)
        return 2;

    fprintf(f, "KeyLockIndicator selftest\n");
    fprintf(f, "sm_cxsmicon=%d dpi=%u dark=%d autostart=%d low_integrity=%d\n",
            GetSystemMetrics(SM_CXSMICON), GetDpiForSystem(),
            (int)taskbar_is_dark(), (int)config_autostart_get(),
            (int)process_is_low_integrity());

    {
        LightState st = keystate_read();
        fprintf(f, "state caps=%d num=%d scroll=%d\n", st.caps, st.num, st.scroll);
    }

    {
        BOOL hooked = keystate_hook_install();
        fprintf(f, "hook install=%d active=%d\n", (int)hooked, (int)keystate_hook_active());
        keystate_hook_remove();
        fprintf(f, "hook removed active=%d\n", (int)keystate_hook_active());
        if (!hooked) {
            DWORD err = GetLastError();
            if (err == ERROR_ACCESS_DENIED && process_is_low_integrity())
                fprintf(f, "hook SKIPPED (low integrity context)\n");
            else {
                fprintf(f, "hook FAILED err=%lu\n", err);
                pass = FALSE;
            }
        }
    }
    fflush(f);

    for (si = 0; si < 6; ++si) {
        IconSet set;

        if (!icons_build(&set, sizes[si], dark)) {
            fprintf(f, "size %d: build FAILED\n", sizes[si]);
            pass = FALSE;
            continue;
        }
        for (i = 0; i < 8; ++i) {
            if (!set.hicon[i]) {
                fprintf(f, "size %d: icon %d missing\n", sizes[si], i);
                pass = FALSE;
                continue;
            }
            if (swprintf_s(path, ARRAYSIZE(path), L"%s\\icon_s%d_%d.rgba",
                           dir, sizes[si], i) > 0) {
                if (!icons_dump_bgra(&set, i, path)) {
                    fprintf(f, "size %d: dump %d FAILED\n", sizes[si], i);
                    pass = FALSE;
                }
            }
        }
        fprintf(f, "size %d: 8/8 icons ok\n", sizes[si]);
        icons_destroy(&set);
    }

    {
        WNDCLASSEXW wc;
        HWND hwnd;
        IconSet set;
        BOOL added = FALSE, modified = FALSE, removed = FALSE;
        DWORD err = 0;

        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"KeyLockIndicatorSelftest";
        RegisterClassExW(&wc);

        hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0,
                               NULL, NULL, wc.hInstance, NULL);
        if (hwnd && icons_build(&set, 16, FALSE)) {
            added = tray_add(hwnd, set.hicon[0], L"selftest");
            if (added) {
                modified = tray_update(set.hicon[7], L"selftest 2");
                removed = tray_remove(hwnd);
            } else {
                err = GetLastError();
            }
            icons_destroy(&set);
        }
        if (added) {
            fprintf(f, "tray add=1 modify=%d remove=%d\n", modified, removed);
            if (!modified || !removed)
                pass = FALSE;
        } else if (err == ERROR_ACCESS_DENIED && process_is_low_integrity()) {
            fprintf(f, "tray add=0 err=%lu SKIPPED (low integrity context)\n", err);
        } else {
            fprintf(f, "tray add=0 err=%lu\n", err);
            pass = FALSE;
        }
        if (hwnd)
            DestroyWindow(hwnd);
    }

    fprintf(f, "RESULT: %s\n", pass ? "PASS" : "FAIL");
    fclose(f);
    return pass ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* diagnostics                                                         */

static BOOL is_elevated(void)
{
    HANDLE token;
    TOKEN_ELEVATION te;
    DWORD len = 0;
    BOOL result = FALSE;

    ZeroMemory(&te, sizeof(te));
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return FALSE;
    if (GetTokenInformation(token, TokenElevation, &te, sizeof(te), &len))
        result = te.TokenIsElevated ? TRUE : FALSE;
    CloseHandle(token);
    return result;
}

static void diag_os(wchar_t *buf, size_t cch)
{
    HKEY key;
    wchar_t product[128] = L"?";
    wchar_t build[64] = L"?";
    DWORD ubr = 0;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        DWORD size = (DWORD)sizeof(product);
        RegQueryValueExW(key, L"ProductName", NULL, NULL, (LPBYTE)product, &size);
        size = (DWORD)sizeof(build);
        RegQueryValueExW(key, L"CurrentBuildNumber", NULL, NULL, (LPBYTE)build, &size);
        size = (DWORD)sizeof(ubr);
        RegQueryValueExW(key, L"UBR", NULL, NULL, (LPBYTE)&ubr, &size);
        RegCloseKey(key);
    }
    swprintf_s(buf, cch, L"%s (build %s.%lu)", product, build, (unsigned long)ubr);
}

/* Used only by the on-demand --diag report; the app itself writes no files. */
static void write_utf8(FILE *f, const wchar_t *text)
{
    char utf8[4096];
    int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, (int)sizeof(utf8), NULL, NULL);

    if (n > 1)
        fwrite(utf8, 1, (size_t)(n - 1), f);
}

static int run_diag(const wchar_t *dir)
{
    wchar_t path[MAX_PATH];
    wchar_t exe[MAX_PATH] = L"?";
    wchar_t osver[256] = L"?";
    wchar_t desktop[256] = L"?";
    wchar_t line[1200];
    FILE *f = NULL;
    HWND hwnd = NULL, tray = NULL, notify = NULL;
    WNDCLASSEXW wc;
    HANDLE mutex;
    IconSet set;
    HDESK desk;
    DWORD need = 0, mutex_err, err, session = 0;
    BOOL added = FALSE;
    SYSTEMTIME st;
    int i, rc = 1;

    CreateDirectoryW(dir, NULL);
    if (swprintf_s(path, ARRAYSIZE(path), L"%s\\diag.txt", dir) < 0)
        return 2;
    if (_wfopen_s(&f, path, L"wb") != 0 || !f)
        return 2;

#define DIAG(...) do { \
        swprintf_s(line, ARRAYSIZE(line), __VA_ARGS__); \
        write_utf8(f, line); \
        fflush(f); \
    } while (0)

    GetLocalTime(&st);
    GetModuleFileNameW(NULL, exe, ARRAYSIZE(exe));
    diag_os(osver, ARRAYSIZE(osver));
    ProcessIdToSessionId(GetCurrentProcessId(), &session);

    desk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (desk) {
        GetUserObjectInformationW(desk, UOI_NAME, desktop, (DWORD)sizeof(desktop), &need);
        CloseDesktop(desk);
    }

    DIAG(L"KeyLockIndicator diagnostic\n");
    DIAG(L"time=%04u-%02u-%02u %02u:%02u:%02u\n", st.wYear, st.wMonth, st.wDay,
         st.wHour, st.wMinute, st.wSecond);
    DIAG(L"exe=%s\n", exe);
    DIAG(L"pid=%lu session=%lu desktop=%s\n",
         (unsigned long)GetCurrentProcessId(), (unsigned long)session, desktop);
    DIAG(L"os=%s\n", osver);
    DIAG(L"integrity=%s elevated=%d\n",
         process_is_low_integrity() ? L"LOW" : L"normal", (int)is_elevated());

    tray = FindWindowW(L"Shell_TrayWnd", NULL);
    notify = tray ? FindWindowExW(tray, NULL, L"TrayNotifyWnd", NULL) : NULL;
    DIAG(L"shell: Shell_TrayWnd=%p TrayNotifyWnd=%p\n", (void *)tray, (void *)notify);

    SetLastError(0);
    mutex = CreateMutexW(NULL, TRUE, kMutexName);
    mutex_err = GetLastError();
    DIAG(L"mutex=%p err=%lu already_running=%d\n", (void *)mutex,
         (unsigned long)mutex_err, (int)(mutex && mutex_err == ERROR_ALREADY_EXISTS));

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"KeyLockIndicatorDiagWnd";
    RegisterClassExW(&wc);
    hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"diag", WS_POPUP,
                           0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);
    DIAG(L"window=%p\n", (void *)hwnd);

    g_dark = taskbar_is_dark();
    g_icon_size = GetSystemMetrics(SM_CXSMICON);
    DIAG(L"sm_cxsmicon=%d dark=%d\n", g_icon_size, (int)g_dark);

    if (icons_build(&set, g_icon_size, g_dark))
        DIAG(L"icons built=1\n");
    else
        DIAG(L"icons built=0\n");

    if (hwnd && set.hicon[0]) {
        SetLastError(0);
        added = tray_add(hwnd, set.hicon[0], L"diag");
        err = GetLastError();
        DIAG(L"tray add=%d err=%lu\n", (int)added, (unsigned long)err);
        for (i = 1; i <= 5 && !added; ++i) {
            MSG msg;

            Sleep(1000);
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            SetLastError(0);
            added = tray_add(hwnd, set.hicon[0], L"diag");
            err = GetLastError();
            DIAG(L"retry %d: add=%d err=%lu\n", i, (int)added, (unsigned long)err);
        }

        {
            DWORD start = GetTickCount();
            while (GetTickCount() - start < 5000) {
                MSG msg;

                MsgWaitForMultipleObjects(0, NULL, FALSE, 100, QS_ALLINPUT);
                while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
        }
        DIAG(L"survived 5s message loop=1\n");
        if (added) {
            DIAG(L"tray remove=%d\n", (int)tray_remove(hwnd));
            rc = 0;
        }
        icons_destroy(&set);
    }

    DIAG(L"RESULT: %s\n", added ? L"PASS" : L"FAIL");
    if (hwnd)
        DestroyWindow(hwnd);
    if (mutex)
        CloseHandle(mutex);
    fclose(f);
#undef DIAG
    return rc;
}

/* ------------------------------------------------------------------ */
/* entry point                                                         */

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR cmdline, int show)
{
    HANDLE mutex;
    WNDCLASSEXW wc;
    MSG msg;
    DWORD session = 0;
    int argc = 0;
    wchar_t **argv;

    UNREFERENCED_PARAMETER(hPrev);
    UNREFERENCED_PARAMETER(cmdline);
    UNREFERENCED_PARAMETER(show);

    g_inst = hInst;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);

    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc >= 3 && !wcscmp(argv[1], L"--selftest")) {
            int rc = run_selftest(argv[2]);
            LocalFree(argv);
            return rc;
        }
        if (argc >= 3 && !wcscmp(argv[1], L"--diag")) {
            int rc = run_diag(argv[2]);
            LocalFree(argv);
            return rc;
        }
        if (argc >= 2 && !wcscmp(argv[1], L"--panel"))
            g_force_panel = TRUE;
        LocalFree(argv);
    }

    g_msg_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    g_msg_show_panel = RegisterWindowMessageW(L"KeyLockIndicatorShowPanel");

    /* Scope the single instance name by integrity level: a copy running in a
     * low integrity sandbox can never own the tray, and must not block the
     * normal launch from Explorer. */
    {
        wchar_t name[128];

        swprintf_s(name, ARRAYSIZE(name), L"Local\\KeyLockIndicator.SingleInstance%s",
                   process_is_low_integrity() ? L".sandbox" : L"");
        mutex = CreateMutexW(NULL, TRUE, name);
    }
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        /* Broadcast reaches the hidden window even when FindWindow cannot see
         * it across integrity levels. */
        PostMessageW(HWND_BROADCAST, g_msg_show_panel, 0, 0);
        CloseHandle(mutex);
        return 0;
    }


    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = kClassName;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                              GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    if (!RegisterClassExW(&wc)) {
        if (mutex)
            CloseHandle(mutex);
        return 1;
    }

    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, APP_TITLE, WS_POPUP,
                             0, 0, 0, 0, NULL, NULL, hInst, NULL);
    if (!g_hwnd) {
        if (mutex)
            CloseHandle(mutex);
        return 1;
    }

    palette_set((int)config_dword_get(L"Color", 0));
    rebuild_icons();
    keystate_set_target(g_hwnd, WM_KEYSTATE_CHANGED);
    keystate_seed();
    if (config_dword_get(L"LowLatency", 1))
        keystate_hook_install();
    g_state = keystate_sample();
    update_tray();
    if (g_force_panel)
        panel_show(g_inst, process_is_low_integrity());
    SetTimer(g_hwnd, TIMER_POLL, keystate_hook_active() ? 1000 : 200, NULL);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    keystate_hook_remove();
    if (g_tray_added)
        tray_remove(g_hwnd);
    icons_destroy(&g_icons);

    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return 0;
}
