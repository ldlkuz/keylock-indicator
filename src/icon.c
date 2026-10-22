/* icon.c - render the three-light tray icon at runtime.
 *
 * The icon is composed analytically (4x4 supersampled discs) into a float
 * premultiplied buffer, then converted to a 32bpp top-down DIB with straight
 * alpha.  No .ico resource is needed, so every DPI gets a crisp icon. */
#include "icon.h"
#include "palette.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SS 4 /* 4x4 supersampling */

typedef struct { float r, g, b, a; } FColor;

static float clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

/* Composite one premultiplied source pixel over the float buffer. */
static void blend(float *buf, int size, int x, int y, FColor c)
{
    float *d;
    float ia;

    if (x < 0 || y < 0 || x >= size || y >= size)
        return;

    d = buf + ((size_t)y * (size_t)size + (size_t)x) * 4;
    ia = 1.0f - c.a;
    d[0] = c.r * c.a + d[0] * ia;
    d[1] = c.g * c.a + d[1] * ia;
    d[2] = c.b * c.a + d[2] * ia;
    d[3] = c.a + d[3] * ia;
}

/* Rounded horizontal bar ("capsule"): outer rect [x0,x1] x [cy-h, cy+h]. */
static void draw_bar(float *buf, int size, float x0, float x1, float cy, float h, FColor color)
{
    int ix0 = (int)floorf(x0) - 1;
    int ix1 = (int)ceilf(x1) + 1;
    int iy0 = (int)floorf(cy - h) - 1;
    int iy1 = (int)ceilf(cy + h) + 1;
    float ax = x0 + h;
    float bx = x1 - h;
    int x, y, sy, sx;

    if (bx < ax)
        bx = ax;

    for (y = iy0; y <= iy1; ++y) {
        for (x = ix0; x <= ix1; ++x) {
            int hits = 0;
            FColor c;

            for (sy = 0; sy < SS; ++sy) {
                for (sx = 0; sx < SS; ++sx) {
                    float px = (float)x + ((float)sx + 0.5f) / SS;
                    float py = (float)y + ((float)sy + 0.5f) / SS;
                    float dx = bx - ax;
                    float t = 0.0f;
                    float qx, qy, ddx, ddy;

                    if (dx > 0.0001f)
                        t = (px - ax) / dx;
                    if (t < 0.0f)
                        t = 0.0f;
                    if (t > 1.0f)
                        t = 1.0f;
                    qx = ax + t * dx;
                    qy = cy;
                    ddx = px - qx;
                    ddy = py - qy;
                    if (ddx * ddx + ddy * ddy <= h * h)
                        ++hits;
                }
            }
            if (hits == 0)
                continue;

            c = color;
            c.a = color.a * (float)hits / (float)(SS * SS);
            blend(buf, size, x, y, c);
        }
    }
}

/* Three horizontal bars stacked vertically, like keyboard indicator lamps:
 * Caps on top, Num in the middle, Scroll at the bottom. */
static void layout_bars(int size, float ys[3], float *x0, float *x1, float *h)
{
    float s = (float)size;

    *x0 = s * 0.155f;
    *x1 = s * 0.845f;
    *h = s * 0.095f;         /* half height -> 19% of the icon per bar */
    ys[0] = s * 0.200f;      /* num  */
    ys[1] = s * 0.500f;      /* caps */
    ys[2] = s * 0.800f;      /* scroll */
}

static void resolve_pixels(const float *buf, int size, unsigned char *out)
{
    size_t i, n = (size_t)size * (size_t)size;

    for (i = 0; i < n; ++i) {
        float a = buf[i * 4 + 3];
        unsigned char *p = out + i * 4;

        if (a <= 0.0039f) {
            p[0] = p[1] = p[2] = p[3] = 0;
            continue;
        }
        {
            float inv = 1.0f / a;
            float r = clamp01(buf[i * 4 + 0] * inv);
            float g = clamp01(buf[i * 4 + 1] * inv);
            float b = clamp01(buf[i * 4 + 2] * inv);

            p[0] = (unsigned char)(b * 255.0f + 0.5f); /* BGRA */
            p[1] = (unsigned char)(g * 255.0f + 0.5f);
            p[2] = (unsigned char)(r * 255.0f + 0.5f);
            p[3] = (unsigned char)(clamp01(a) * 255.0f + 0.5f);
        }
    }
}

static HICON make_icon(const unsigned char *bgra, int size)
{
    BITMAPV5HEADER bi;
    HDC hdc;
    void *bits = NULL;
    HBITMAP color, mask = NULL;
    unsigned char *mask_bits = NULL;
    size_t mask_bytes;
    ICONINFO ii;
    HICON icon;

    ZeroMemory(&bi, sizeof(bi));
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = size;
    bi.bV5Height = -size; /* top-down */
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000u;
    bi.bV5GreenMask = 0x0000FF00u;
    bi.bV5BlueMask = 0x000000FFu;
    bi.bV5AlphaMask = 0xFF000000u;

    hdc = GetDC(NULL);
    if (!hdc)
        return NULL;
    color = CreateDIBSection(hdc, (const BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, hdc);
    if (!color || !bits) {
        if (color)
            DeleteObject(color);
        return NULL;
    }
    memcpy(bits, bgra, (size_t)size * (size_t)size * 4);

    /* AND mask: all zero, the alpha channel does the masking.
     * 1bpp rows are DWORD aligned: ((w + 31) / 32) * 4 bytes per row. */
    mask_bytes = ((size_t)(size + 31) / 32) * 4 * (size_t)size;
    mask_bits = (unsigned char *)calloc(mask_bytes, 1);
    if (mask_bits) {
        mask = CreateBitmap(size, size, 1, 1, mask_bits);
        free(mask_bits);
    }
    if (!mask) {
        DeleteObject(color);
        return NULL;
    }

    ZeroMemory(&ii, sizeof(ii));
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

BOOL icons_build(IconSet *set, int size, BOOL dark_taskbar)
{
    float *buf;
    float x0, x1, h, ys[3];
    FColor on, off;
    unsigned char *pixels;
    int i;

    if (!set || size < 8 || size > 128)
        return FALSE;

    ZeroMemory(set, sizeof(*set));
    set->size = size;
    set->dark = dark_taskbar;

    buf = (float *)calloc((size_t)size * (size_t)size * 4, sizeof(float));
    pixels = (unsigned char *)malloc((size_t)size * (size_t)size * 4);
    if (!buf || !pixels) {
        free(buf);
        free(pixels);
        return FALSE;
    }

    layout_bars(size, ys, &x0, &x1, &h);

    {
        float rgb[3];

        palette_tray_on(palette_get(), dark_taskbar, rgb);
        on.r = rgb[0];
        on.g = rgb[1];
        on.b = rgb[2];
        on.a = 1.00f;
    }
    if (dark_taskbar) {
        off.r = 0.66f; off.g = 0.66f; off.b = 0.66f; off.a = 0.80f;
    } else {
        off.r = 0.34f; off.g = 0.34f; off.b = 0.34f; off.a = 0.80f;
    }

    for (i = 0; i < 8; ++i) {
        memset(buf, 0, (size_t)size * (size_t)size * 4 * sizeof(float));
        draw_bar(buf, size, x0, x1, ys[0], h, (i & 2) ? on : off); /* num */
        draw_bar(buf, size, x0, x1, ys[1], h, (i & 4) ? on : off); /* caps */
        draw_bar(buf, size, x0, x1, ys[2], h, (i & 1) ? on : off);
        resolve_pixels(buf, size, pixels);
        set->hicon[i] = make_icon(pixels, size);
    }

    free(buf);
    free(pixels);

    for (i = 0; i < 8; ++i) {
        if (!set->hicon[i]) {
            icons_destroy(set);
            return FALSE;
        }
    }
    return TRUE;
}

void icons_destroy(IconSet *set)
{
    int i;
    if (!set)
        return;
    for (i = 0; i < 8; ++i) {
        if (set->hicon[i]) {
            DestroyIcon(set->hicon[i]);
            set->hicon[i] = NULL;
        }
    }
    set->size = 0;
}

int lights_index(LightState s)
{
    return (s.caps ? 4 : 0) | (s.num ? 2 : 0) | (s.scroll ? 1 : 0);
}

BOOL icons_dump_bgra(const IconSet *set, int index, const wchar_t *path)
{
    ICONINFO ii;
    BITMAP bm;
    BITMAPINFOHEADER bih;
    FILE *f = NULL;
    void *buf;
    size_t bytes;
    BOOL ok = FALSE;
    int lines;

    if (!set || index < 0 || index > 7 || !set->hicon[index] || !path)
        return FALSE;
    ZeroMemory(&ii, sizeof(ii));
    if (!GetIconInfo(set->hicon[index], &ii))
        return FALSE;
    if (GetObjectW(ii.hbmColor, sizeof(bm), &bm) != sizeof(bm)) {
        DeleteObject(ii.hbmColor);
        DeleteObject(ii.hbmMask);
        return FALSE;
    }

    ZeroMemory(&bih, sizeof(bih));
    bih.biSize = sizeof(bih);
    bih.biWidth = bm.bmWidth;
    bih.biHeight = -bm.bmHeight;
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;

    bytes = (size_t)bm.bmWidth * (size_t)bm.bmHeight * 4;
    buf = malloc(bytes);
    if (buf) {
        HDC hdc = GetDC(NULL);
        if (hdc) {
            lines = GetDIBits(hdc, ii.hbmColor, 0, (UINT)bm.bmHeight, buf,
                              (BITMAPINFO *)&bih, DIB_RGB_COLORS);
            ReleaseDC(NULL, hdc);
            if (lines == bm.bmHeight && _wfopen_s(&f, path, L"wb") == 0 && f) {
                if (fwrite(buf, 1, bytes, f) == bytes)
                    ok = TRUE;
                fclose(f);
            }
        }
        free(buf);
    }

    DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);
    return ok;
}
