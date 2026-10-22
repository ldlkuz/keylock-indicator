#include "palette.h"

/* Lit colour on a dark taskbar (needs to glow) and on a light one (needs to
 * be darker to keep contrast). Values are sRGB in 0..1. */
static const float kOnDark[KLI_COLOR_COUNT][3] = {
    {0.28f, 0.86f, 0.45f},   /* green */
    {0.36f, 0.68f, 1.00f},   /* blue  */
    {1.00f, 0.72f, 0.25f},   /* amber */
    {1.00f, 0.42f, 0.40f}    /* red   */
};

static const float kOnLight[KLI_COLOR_COUNT][3] = {
    {0.09f, 0.60f, 0.27f},
    {0.10f, 0.42f, 0.85f},
    {0.72f, 0.44f, 0.05f},
    {0.72f, 0.15f, 0.14f}
};

static const wchar_t *kNames[KLI_COLOR_COUNT] = {
    L"绿色", L"蓝色", L"琥珀", L"红色"
};

static int g_index;

static float clamp01(float v)
{
    if (v < 0.0f)
        return 0.0f;
    if (v > 1.0f)
        return 1.0f;
    return v;
}

int palette_count(void)
{
    return KLI_COLOR_COUNT;
}

const wchar_t *palette_name(int index)
{
    return kNames[palette_clamp(index)];
}

int palette_clamp(int index)
{
    if (index < 0 || index >= KLI_COLOR_COUNT)
        return 0;
    return index;
}

void palette_set(int index)
{
    g_index = palette_clamp(index);
}

int palette_get(void)
{
    return g_index;
}

void palette_tray_on(int index, BOOL dark_taskbar, float rgb[3])
{
    const float *src = dark_taskbar ? kOnDark[palette_clamp(index)]
                                    : kOnLight[palette_clamp(index)];

    rgb[0] = src[0];
    rgb[1] = src[1];
    rgb[2] = src[2];
}

void palette_panel(int index, float core[3], float edge[3])
{
    const float *base = kOnDark[palette_clamp(index)];
    int i;

    for (i = 0; i < 3; ++i) {
        core[i] = clamp01(base[i] * 1.20f + 0.12f);
        edge[i] = clamp01(base[i] * 0.62f);
    }
}
