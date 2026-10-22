#include "tray.h"
#include "palette.h"

#include <strsafe.h>

/* Stable identity for the icon. With NIF_GUID the shell treats every instance
 * as the same icon, so a process that is killed (no NIM_DELETE) is replaced by
 * the next launch instead of leaving a ghost icon behind. */
static const GUID kIconGuid =
    {0x7c4e2b91, 0x3a6d, 0x4f58, {0x9e, 0x1c, 0x2b, 0x8d, 0x5a, 0x0f, 0x6e, 0x34}};

static NOTIFYICONDATAW g_nid;
static BOOL g_added;

static void init_nid(NOTIFYICONDATAW *nid, HWND hwnd)
{
    ZeroMemory(nid, sizeof(*nid));
    nid->cbSize = sizeof(*nid);
    nid->hWnd = hwnd;
    nid->uID = 0;              /* ignored when NIF_GUID is set */
    nid->guidItem = kIconGuid;
}

BOOL tray_add(HWND hwnd, HICON icon, const wchar_t *tip)
{
    init_nid(&g_nid, hwnd);
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_GUID;
    g_nid.uCallbackMessage = WM_TRAY_MSG;
    g_nid.hIcon = icon;
    StringCchCopyW(g_nid.szTip, ARRAYSIZE(g_nid.szTip), tip ? tip : L"");

    g_added = Shell_NotifyIconW(NIM_ADD, &g_nid) ? TRUE : FALSE;
    return g_added;
}

BOOL tray_update(HICON icon, const wchar_t *tip)
{
    HWND hwnd;

    if (!g_added)
        return FALSE;

    /* Rebuild the struct from scratch: reusing it leaves the previous (longer)
     * tip bytes behind the new terminator. */
    hwnd = g_nid.hWnd;
    init_nid(&g_nid, hwnd);
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_GUID;
    g_nid.hIcon = icon;
    StringCchCopyW(g_nid.szTip, ARRAYSIZE(g_nid.szTip), tip ? tip : L"");

    return Shell_NotifyIconW(NIM_MODIFY, &g_nid) ? TRUE : FALSE;
}

BOOL tray_remove(HWND hwnd)
{
    if (!g_added)
        return FALSE;
    init_nid(&g_nid, hwnd);
    g_nid.uFlags = NIF_GUID;
    g_added = FALSE;
    return Shell_NotifyIconW(NIM_DELETE, &g_nid) ? TRUE : FALSE;
}

int tray_menu(HWND hwnd, BOOL autostart, BOOL low_latency, BOOL panel_visible)
{
    HMENU menu;
    POINT pt;
    int cmd;

    menu = CreatePopupMenu();
    if (!menu)
        return 0;

    AppendMenuW(menu, MF_STRING | (panel_visible ? MF_UNCHECKED : MF_CHECKED), IDM_PANEL,
                panel_visible ? L"隐藏三灯面板" : L"显示三灯面板");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (autostart ? MF_CHECKED : MF_UNCHECKED), IDM_AUTOSTART, L"开机自启");
    AppendMenuW(menu, MF_STRING | (low_latency ? MF_CHECKED : MF_UNCHECKED), IDM_LOWLATENCY,
                L"低延迟模式（键盘钩子）");
    {
        HMENU colors = CreatePopupMenu();
        int i;

        if (colors) {
            for (i = 0; i < palette_count(); ++i)
                AppendMenuW(colors,
                            MF_STRING | (i == palette_get() ? MF_CHECKED : MF_UNCHECKED),
                            IDM_COLOR_FIRST + i, palette_name(i));
            AppendMenuW(menu, MF_POPUP, (UINT_PTR)colors, L"颜色");
        }
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_REFRESH, L"立即刷新");
    AppendMenuW(menu, MF_STRING, IDM_ABOUT, L"关于");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"退出");
    SetMenuDefaultItem(menu, IDM_PANEL, FALSE);

    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                         pt.x, pt.y, 0, hwnd, NULL);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
    return cmd;
}
