#pragma once
#include <windows.h>

typedef struct {
    BOOL caps;
    BOOL num;
    BOOL scroll;
} LightState;

/* Raw GetKeyState() snapshot. */
LightState keystate_read(void);

/* Seed the tracked state from GetKeyState() before the hook is installed. */
void keystate_seed(void);

/* Install/remove the low level keyboard hook (WH_KEYBOARD_LL). The hook is an
 * accelerator: it flips the tracked state on key down, while keystate_sample()
 * keeps it in sync with the system whenever GetKeyState() actually changes. */
BOOL keystate_hook_install(void);
void keystate_hook_remove(void);
BOOL keystate_hook_active(void);

/* Call from the timer: returns the best known state. */
LightState keystate_sample(void);

/* The hook posts this message to the window you register, so a key press
 * refreshes the UI immediately instead of waiting for the next poll. */
#define WM_KEYSTATE_CHANGED (WM_APP + 3)
void keystate_set_target(HWND hwnd, UINT msg);

BOOL keystate_equal(LightState a, LightState b);
