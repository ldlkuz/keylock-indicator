#include "keystate.h"

/* Tracked state shared by the hook and the poll timer (same thread). */
static HHOOK      g_hook;
static LightState g_state;
static LightState g_polled;
static BOOL       g_polled_valid;
static BYTE       g_down[256];
static HWND       g_target;
static UINT       g_target_msg;

void keystate_set_target(HWND hwnd, UINT msg)
{
    g_target = hwnd;
    g_target_msg = msg;
}

LightState keystate_read(void)
{
    LightState s;

    s.caps   = (GetKeyState(VK_CAPITAL) & 1) != 0;
    s.num    = (GetKeyState(VK_NUMLOCK) & 1) != 0;
    s.scroll = (GetKeyState(VK_SCROLL) & 1) != 0;
    return s;
}

BOOL keystate_equal(LightState a, LightState b)
{
    return a.caps == b.caps && a.num == b.num && a.scroll == b.scroll;
}

void keystate_seed(void)
{
    g_state = keystate_read();
    g_polled = g_state;
    g_polled_valid = TRUE;
    ZeroMemory(g_down, sizeof(g_down));
}

static LRESULT CALLBACK low_level_proc(int code, WPARAM wparam, LPARAM lparam)
{
    if (code == HC_ACTION) {
        const KBDLLHOOKSTRUCT *kb = (const KBDLLHOOKSTRUCT *)lparam;
        DWORD vk = kb->vkCode;
        BOOL down = (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN);

        if (vk < 256 && (vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_SCROLL)) {
            /* Toggle only on the up -> down transition so auto-repeat cannot
             * flip a light twice. */
            if (down && !g_down[vk]) {
                if (vk == VK_CAPITAL)
                    g_state.caps = !g_state.caps;
                else if (vk == VK_NUMLOCK)
                    g_state.num = !g_state.num;
                else
                    g_state.scroll = !g_state.scroll;
                if (g_target && g_target_msg)
                    PostMessageW(g_target, g_target_msg, 0, 0);
            }
            g_down[vk] = down ? 1 : 0;
        }
    }
    return CallNextHookEx(g_hook, code, wparam, lparam);
}

BOOL keystate_hook_install(void)
{
    if (g_hook)
        return TRUE;
    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, low_level_proc,
                               GetModuleHandleW(NULL), 0);
    return g_hook != NULL;
}

void keystate_hook_remove(void)
{
    if (g_hook) {
        UnhookWindowsHookEx(g_hook);
        g_hook = NULL;
    }
    ZeroMemory(g_down, sizeof(g_down));
}

BOOL keystate_hook_active(void)
{
    return g_hook != NULL;
}

LightState keystate_sample(void)
{
    LightState raw = keystate_read();

    /* A change coming from GetKeyState() always wins: the hook may have missed
     * an event (other desktop, suspended process), but a stale GetKeyState()
     * never overrides the hook because it simply does not change. */
    if (!g_polled_valid || !keystate_equal(raw, g_polled)) {
        g_polled = raw;
        g_polled_valid = TRUE;
        g_state = raw;
    }
    return g_state;
}
