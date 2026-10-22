#include "panel.h"
#include "config.h"
#include "keystate.h"
#include "palette.h"

#include <math.h>
#include <windowsx.h>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

#define PANEL_CLASS         L"KeyLockIndicatorPanelWnd"
#define PANEL_TIMER         1
#define PANEL_W             280
#define PANEL_H             132
#define PANEL_H_RESTRICTED  154
#define LAMP_R              17
#define SS                  4

static HWND       g_panel;
static BOOL       g_restricted;
static LightState g_shown;
static float      g_scale = 1.0f;   /* dpi / 96 */

/* Auto-hide: once the pointer has visited the panel, hide it 3 s after it
 * leaves. If it is never visited (opened with a click on the tray icon, so the
 * pointer starts elsewhere) give it 10 s to be read first. */
#define HIDE_AFTER_LEAVE_MS  3000
#define HIDE_AFTER_OPEN_MS   10000

static DWORD      g_last_inside;
static BOOL       g_ever_inside;
static BOOL       g_dragging;

/* Cached 32bpp top-down DIB: every repaint is composed here first so the
 * lamps can be anti-aliased in software (GDI's Ellipse has hard edges). */
static HBITMAP    g_dib;
static HDC        g_dib_dc;
static void      *g_dib_bits;
static int        g_dib_w, g_dib_h;

static int S(int v)
{
    return (int)((float)v * g_scale + 0.5f);
}

static int panel_width(void)
{
    return S(PANEL_W);
}

static int panel_height(void)
{
    return S(g_restricted ? PANEL_H_RESTRICTED : PANEL_H);
}

/* The panel belongs next to the taskbar, so it should follow whichever monitor
 * the taskbar is on, not the primary one. */
static BOOL taskbar_work_area(RECT *work)
{
    HWND tray = FindWindowW(L"Shell_TrayWnd", NULL);
    HMONITOR mon;
    MONITORINFO mi;

    if (tray) {
        mon = MonitorFromWindow(tray, MONITOR_DEFAULTTONEAREST);
        mi.cbSize = sizeof(mi);
        if (mon && GetMonitorInfoW(mon, &mi)) {
            *work = mi.rcWork;
            return TRUE;
        }
    }
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, work, 0))
        return TRUE;

    work->left = 0;
    work->top = 0;
    work->right = GetSystemMetrics(SM_CXSCREEN);
    work->bottom = GetSystemMetrics(SM_CYSCREEN);
    return FALSE;
}

static float taskbar_dpi_scale(void)
{
    HWND tray = FindWindowW(L"Shell_TrayWnd", NULL);

    if (tray) {
        UINT dpi = GetDpiForWindow(tray);

        if (dpi >= 96 && dpi <= 480)
            return (float)dpi / 96.0f;
    }
    return 1.0f;
}

static void free_buffer(void)
{
    if (g_dib_dc) {
        DeleteDC(g_dib_dc);
        g_dib_dc = NULL;
    }
    if (g_dib) {
        DeleteObject(g_dib);
        g_dib = NULL;
    }
    g_dib_bits = NULL;
    g_dib_w = g_dib_h = 0;
}

static BOOL ensure_buffer(int w, int h)
{
    BITMAPINFOHEADER bih;
    HDC screen;

    if (g_dib && g_dib_w == w && g_dib_h == h)
        return TRUE;

    free_buffer();

    ZeroMemory(&bih, sizeof(bih));
    bih.biSize = sizeof(bih);
    bih.biWidth = w;
    bih.biHeight = -h; /* top-down */
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;

    screen = GetDC(NULL);
    if (!screen)
        return FALSE;
    g_dib = CreateDIBSection(screen, (const BITMAPINFO *)&bih, DIB_RGB_COLORS,
                             &g_dib_bits, NULL, 0);
    ReleaseDC(NULL, screen);
    if (!g_dib || !g_dib_bits) {
        free_buffer();
        return FALSE;
    }

    g_dib_dc = CreateCompatibleDC(NULL);
    if (!g_dib_dc) {
        free_buffer();
        return FALSE;
    }
    SelectObject(g_dib_dc, g_dib);
    g_dib_w = w;
    g_dib_h = h;
    return TRUE;
}

static void blend_px(unsigned char *bits, int w, int h, int x, int y,
                     float r, float g, float b, float a)
{
    unsigned char *p;
    float inv;

    if (a <= 0.002f || x < 0 || y < 0 || x >= w || y >= h)
        return;
    if (a > 1.0f)
        a = 1.0f;

    p = bits + ((size_t)y * (size_t)w + (size_t)x) * 4;
    inv = 1.0f - a;
    p[0] = (unsigned char)((b * a + (p[0] / 255.0f) * inv) * 255.0f + 0.5f);
    p[1] = (unsigned char)((g * a + (p[1] / 255.0f) * inv) * 255.0f + 0.5f);
    p[2] = (unsigned char)((r * a + (p[2] / 255.0f) * inv) * 255.0f + 0.5f);
    p[3] = 255;
}

static void fill_background(unsigned char *bits, int w, int h)
{
    int y, x;

    for (y = 0; y < h; ++y) {
        float t = (float)y / (float)(h > 1 ? h - 1 : 1);
        float r = 0.145f + (0.075f - 0.145f) * t;
        float g = 0.145f + (0.078f - 0.145f) * t;
        float b = 0.160f + (0.090f - 0.160f) * t;
        unsigned char *row = bits + (size_t)y * (size_t)w * 4;

        for (x = 0; x < w; ++x) {
            row[x * 4 + 0] = (unsigned char)(b * 255.0f + 0.5f);
            row[x * 4 + 1] = (unsigned char)(g * 255.0f + 0.5f);
            row[x * 4 + 2] = (unsigned char)(r * 255.0f + 0.5f);
            row[x * 4 + 3] = 255;
        }
    }
}

static void draw_lamp(unsigned char *bits, int w, int h, int cx, int cy, int radius, BOOL on)
{
    float core_r, core_g, core_b, edge_r, edge_g, edge_b;

    if (on) {
        float core[3], edge[3];

        palette_panel(palette_get(), core, edge);
        core_r = core[0]; core_g = core[1]; core_b = core[2];
        edge_r = edge[0]; edge_g = edge[1]; edge_b = edge[2];
    } else {
        core_r = 0.27f; core_g = 0.27f; core_b = 0.31f;
        edge_r = 0.19f; edge_g = 0.19f; edge_b = 0.23f;
    }
    int glow = on ? radius + 10 : radius + 2;
    int x, y, sx, sy;

    /* soft halo, only for lit lamps */
    if (on) {
        for (y = cy - glow; y <= cy + glow; ++y) {
            for (x = cx - glow; x <= cx + glow; ++x) {
                float dx = (float)x + 0.5f - (float)cx;
                float dy = (float)y + 0.5f - (float)cy;
                float d = sqrtf(dx * dx + dy * dy);
                float t, a;

                if (d > (float)glow || d < (float)radius - 1.0f)
                    continue;
                t = d / (float)glow;
                a = 0.34f * (1.0f - t) * (1.0f - t);
                blend_px(bits, w, h, x, y, core_r, core_g, core_b, a);
            }
        }
    }

    /* body: 4x4 supersampled disc with a radial gradient */
    for (y = cy - radius - 1; y <= cy + radius + 1; ++y) {
        for (x = cx - radius - 1; x <= cx + radius + 1; ++x) {
            int hits = 0;
            float sr = 0.0f, sg = 0.0f, sb = 0.0f;

            for (sy = 0; sy < SS; ++sy) {
                for (sx = 0; sx < SS; ++sx) {
                    float px = (float)x + ((float)sx + 0.5f) / SS;
                    float py = (float)y + ((float)sy + 0.5f) / SS;
                    float dx = px - (float)cx;
                    float dy = py - (float)cy;
                    float d2 = dx * dx + dy * dy;
                    float t;

                    if (d2 > (float)(radius * radius))
                        continue;
                    t = sqrtf(d2) / (float)radius;
                    sr += core_r + (edge_r - core_r) * t;
                    sg += core_g + (edge_g - core_g) * t;
                    sb += core_b + (edge_b - core_b) * t;
                    ++hits;
                }
            }
            if (hits == 0)
                continue;
            blend_px(bits, w, h, x, y, sr / hits, sg / hits, sb / hits,
                     (float)hits / (float)(SS * SS));
        }
    }

    /* thin rim so an unlit lamp still reads as a lamp, not a smudge */
    for (y = cy - radius - 1; y <= cy + radius + 1; ++y) {
        for (x = cx - radius - 1; x <= cx + radius + 1; ++x) {
            int hits = 0;

            for (sy = 0; sy < SS; ++sy) {
                for (sx = 0; sx < SS; ++sx) {
                    float px = (float)x + ((float)sx + 0.5f) / SS;
                    float py = (float)y + ((float)sy + 0.5f) / SS;
                    float dx = px - (float)cx;
                    float dy = py - (float)cy;
                    float d = sqrtf(dx * dx + dy * dy);

                    if (d <= (float)radius && d >= (float)radius - 1.25f)
                        ++hits;
                }
            }
            if (hits) {
                blend_px(bits, w, h, x, y,
                         on ? core_r * 0.80f : 0.36f,
                         on ? core_g * 0.80f : 0.36f,
                         on ? core_b * 0.80f : 0.40f,
                         0.55f * (float)hits / (float)(SS * SS));
            }
        }
    }
}

static void draw_text(HDC dc, int w, int h)
{
    static const wchar_t *labels[3] = {L"Num", L"Caps", L"Scroll"};
    HFONT font, old;
    int i;

    SetBkMode(dc, TRANSPARENT);
    font = CreateFontW(-S(14), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH, L"Microsoft YaHei UI");
    if (!font)
        font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    old = (HFONT)SelectObject(dc, font);

    for (i = 0; i < 3; ++i) {
        int cx = w * (2 * i + 1) / 6;
        BOOL on = i == 0 ? g_shown.num : (i == 1 ? g_shown.caps : g_shown.scroll);
        RECT lr = {cx - S(46), S(84), cx + S(46), S(108)};

        if (on) {
            float core[3], edge[3];

            palette_panel(palette_get(), core, edge);
            SetTextColor(dc, RGB((int)(core[0] * 235.0f), (int)(core[1] * 235.0f),
                                 (int)(core[2] * 235.0f)));
        } else {
            SetTextColor(dc, RGB(140, 140, 150));
        }
        DrawTextW(dc, labels[i], -1, &lr, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    if (g_restricted) {
        RECT hr = {0, h - S(24), w, h - S(6)};

        SetTextColor(dc, RGB(226, 168, 74));
        DrawTextW(dc, L"受限权限：请用桌面快捷方式启动", -1, &hr,
                  DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    /* close glyph */
    {
        RECT cr = {w - S(28), S(6), w - S(8), S(26)};
        HFONT sym = CreateFontW(-S(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH, L"Segoe UI Symbol");

        if (sym)
            SelectObject(dc, sym);
        SetTextColor(dc, RGB(108, 108, 118));
        DrawTextW(dc, L"\x2715", -1, &cr, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (sym) {
            SelectObject(dc, old);
            DeleteObject(sym);
        }
    }

    SelectObject(dc, old);
    if (font != (HFONT)GetStockObject(DEFAULT_GUI_FONT))
        DeleteObject(font);
}

static void paint(HWND hwnd, HDC target)
{
    RECT rc;

    GetClientRect(hwnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0)
        return;
    if (!ensure_buffer(rc.right, rc.bottom))
        return;

    fill_background((unsigned char *)g_dib_bits, g_dib_w, g_dib_h);

    {
        int i;
        int cy = S(52);

        for (i = 0; i < 3; ++i) {
            int cx = g_dib_w * (2 * i + 1) / 6;
            BOOL on = i == 0 ? g_shown.num : (i == 1 ? g_shown.caps : g_shown.scroll);

            draw_lamp((unsigned char *)g_dib_bits, g_dib_w, g_dib_h, cx, cy, S(LAMP_R), on);
        }
    }

    draw_text(g_dib_dc, g_dib_w, g_dib_h);
    BitBlt(target, 0, 0, g_dib_w, g_dib_h, g_dib_dc, 0, 0, SRCCOPY);
}

static void save_position(HWND hwnd)
{
    RECT r;

    if (!GetWindowRect(hwnd, &r))
        return;
    config_dword_set(L"PanelX", (DWORD)r.left);
    config_dword_set(L"PanelY", (DWORD)r.top);
}

static BOOL restore_position(HWND hwnd)
{
    DWORD x = config_dword_get(L"PanelX", 0xFFFFFFFFu);
    DWORD y = config_dword_get(L"PanelY", 0xFFFFFFFFu);
    int vx, vy, vw, vh, w, h;

    if (x == 0xFFFFFFFFu || y == 0xFFFFFFFFu)
        return FALSE;

    w = panel_width();
    h = panel_height();
    vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if ((int)x < vx - w + 40 || (int)x > vx + vw - 40 ||
        (int)y < vy - 10 || (int)y > vy + vh - 40)
        return FALSE;

    SetWindowPos(hwnd, HWND_TOPMOST, (int)x, (int)y, w, h, SWP_SHOWWINDOW);
    return TRUE;
}

static void place_bottom_right(HWND hwnd)
{
    RECT work;
    int x, y, w, h;

    taskbar_work_area(&work);
    w = panel_width();
    h = panel_height();
    x = work.right - w - S(24);
    y = work.bottom - h - S(24);
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
}

static LRESULT CALLBACK panel_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        paint(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_PRINTCLIENT:
        paint(hwnd, (HDC)wp);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_TIMER:
        if (wp == PANEL_TIMER) {
            LightState now = keystate_sample();
            DWORD tick = GetTickCount();

            if (!keystate_equal(now, g_shown)) {
                g_shown = now;
                InvalidateRect(hwnd, NULL, FALSE);
            }

            if (!g_dragging) {
                POINT pt;
                RECT wr;

                GetCursorPos(&pt);
                GetWindowRect(hwnd, &wr);
                if (PtInRect(&wr, pt)) {
                    g_ever_inside = TRUE;
                    g_last_inside = tick;
                } else if (tick - g_last_inside >
                           (DWORD)(g_ever_inside ? HIDE_AFTER_LEAVE_MS : HIDE_AFTER_OPEN_MS)) {
                    save_position(hwnd);
                    DestroyWindow(hwnd);
                }
            }
        }
        return 0;

    case WM_NCHITTEST: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};

        ScreenToClient(hwnd, &pt);
        if (pt.x >= g_dib_w - S(30) && pt.y <= S(30))
            return HTCLIENT;   /* close button */
        return HTCAPTION;      /* drag anywhere else */
    }

    case WM_NCLBUTTONDBLCLK:
        return 0;              /* never maximise */

    case WM_DPICHANGED: {
        RECT *suggested = (RECT *)lp;

        g_scale = (float)HIWORD(wp) / 96.0f;
        free_buffer();
        SetWindowPos(hwnd, NULL, suggested->left, suggested->top,
                     panel_width(), panel_height(),
                     SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    case WM_LBUTTONUP: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};

        if (pt.x >= g_dib_w - S(30) && pt.y <= S(30)) {
            save_position(hwnd);
            DestroyWindow(hwnd);
        }
        return 0;
    }

    case WM_ENTERSIZEMOVE:
        g_dragging = TRUE;
        return 0;

    case WM_EXITSIZEMOVE:
        g_dragging = FALSE;
        g_last_inside = GetTickCount();
        save_position(hwnd);
        return 0;

    case WM_CLOSE:
        save_position(hwnd);
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, PANEL_TIMER);
        g_panel = NULL;
        free_buffer();
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND panel_show(HINSTANCE hInst, BOOL restricted)
{
    WNDCLASSEXW wc;
    DWORD corner = 2; /* DWMWCP_ROUND */

    if (g_panel && IsWindow(g_panel)) {
        if (g_restricted != restricted) {
            g_restricted = restricted;
            SetWindowPos(g_panel, NULL, 0, 0, panel_width(), panel_height(),
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        g_last_inside = GetTickCount();
        g_ever_inside = FALSE;
        g_dragging = FALSE;
        ShowWindow(g_panel, SW_SHOWNOACTIVATE);
        InvalidateRect(g_panel, NULL, FALSE);
        return g_panel;
    }

    g_restricted = restricted;
    g_scale = taskbar_dpi_scale();

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = panel_proc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = PANEL_CLASS;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return NULL;

    g_panel = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                              PANEL_CLASS, L"键盘指示灯", WS_POPUP,
                              CW_USEDEFAULT, CW_USEDEFAULT, panel_width(), panel_height(),
                              NULL, NULL, hInst, NULL);
    if (!g_panel)
        return NULL;

    DwmSetWindowAttribute(g_panel, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */,
                          &corner, sizeof(corner));

    g_shown = keystate_sample();
    g_last_inside = GetTickCount();
    g_ever_inside = FALSE;
    g_dragging = FALSE;
    if (!restore_position(g_panel))
        place_bottom_right(g_panel);
    UpdateWindow(g_panel);
    SetTimer(g_panel, PANEL_TIMER, 100, NULL);
    return g_panel;
}

void panel_close(void)
{
    if (g_panel && IsWindow(g_panel))
        DestroyWindow(g_panel);
    g_panel = NULL;
}

BOOL panel_visible(void)
{
    return g_panel && IsWindow(g_panel) && IsWindowVisible(g_panel);
}

void panel_refresh(void)
{
    if (g_panel && IsWindow(g_panel))
        InvalidateRect(g_panel, NULL, FALSE);
}

/* Called from the keyboard-hook notification so the lamps react immediately
 * instead of waiting for the 100 ms panel timer. */
void panel_sync(void)
{
    LightState now;

    if (!g_panel || !IsWindow(g_panel))
        return;
    now = keystate_sample();
    if (!keystate_equal(now, g_shown)) {
        g_shown = now;
        InvalidateRect(g_panel, NULL, FALSE);
    }
}
