#pragma once
#include <windows.h>

/* A small set of presets. Each one has two brightness variants so the tray
 * icon stays legible on both light and dark taskbars. */
#define KLI_COLOR_COUNT 4

int            palette_count(void);
const wchar_t *palette_name(int index);
int            palette_clamp(int index);

/* Process-wide selection (persisted by the caller). */
void palette_set(int index);
int  palette_get(void);

/* Tray icon: 0..1 RGB for the lit bar on the given taskbar theme. */
void palette_tray_on(int index, BOOL dark_taskbar, float rgb[3]);

/* Panel lamp on its dark card: centre and edge colours of the radial gradient. */
void palette_panel(int index, float core[3], float edge[3]);
