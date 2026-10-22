#pragma once
#include <windows.h>
#include "keystate.h"

/* 8 icons: bit2 = caps, bit1 = num, bit0 = scroll */
typedef struct {
    int   size;
    BOOL  dark;
    HICON hicon[8];
} IconSet;

BOOL icons_build(IconSet *set, int size, BOOL dark_taskbar);
void icons_destroy(IconSet *set);
int  lights_index(LightState s);

/* Selftest helper: dump one icon as raw top-down BGRA bytes. */
BOOL icons_dump_bgra(const IconSet *set, int index, const wchar_t *path);
