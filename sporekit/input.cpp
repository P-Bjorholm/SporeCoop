// Input injection for automated tests: window messages + virtual key state
// (GetAsyncKeyState/GetKeyState overrides, since the game polls some keys).
#include "core.h"

#include <atomic>

namespace sk {

static std::atomic<uint8_t> g_vkdown[256];

typedef SHORT(WINAPI* GetAsyncKeyState_t)(int);
typedef SHORT(WINAPI* GetKeyState_t)(int);
static GetAsyncKeyState_t o_GetAsyncKeyState;
static GetKeyState_t o_GetKeyState;

static SHORT WINAPI h_GetAsyncKeyState(int vk)
{
    SHORT r = o_GetAsyncKeyState(vk);
    if (vk >= 0 && vk < 256 && g_vkdown[vk])
        r |= (SHORT)0x8001;
    return r;
}

static SHORT WINAPI h_GetKeyState(int vk)
{
    SHORT r = o_GetKeyState(vk);
    if (vk >= 0 && vk < 256 && g_vkdown[vk])
        r |= (SHORT)0x8000;
    return r;
}

static void ensure_hooks()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    HMODULE u = GetModuleHandleW(L"user32.dll");
    hook_create(&g_kit_mod, (void*)GetProcAddress(u, "GetAsyncKeyState"), (void*)h_GetAsyncKeyState,
                (void**)&o_GetAsyncKeyState);
    hook_create(&g_kit_mod, (void*)GetProcAddress(u, "GetKeyState"), (void*)h_GetKeyState,
                (void**)&o_GetKeyState);
}

static LPARAM key_lparam(int vk, bool up)
{
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LPARAM lp = 1 | (sc << 16);
    switch (vk) {
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_INSERT: case VK_DELETE:
    case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
        lp |= 1 << 24;
        break;
    }
    if (up)
        lp |= (1u << 30) | (1u << 31);
    return lp;
}

bool input_key(int vk, const char* mode)
{
    HWND w = (HWND)game_window();
    if (!w || vk <= 0 || vk > 255)
        return false;
    ensure_hooks();
    bool down = !mode || !strcmp(mode, "tap") || !strcmp(mode, "down");
    bool up = !mode || !strcmp(mode, "tap") || !strcmp(mode, "up");
    UINT kd = (vk == VK_MENU || vk == VK_F10) ? WM_SYSKEYDOWN : WM_KEYDOWN;
    UINT ku = (vk == VK_MENU || vk == VK_F10) ? WM_SYSKEYUP : WM_KEYUP;
    if (down) {
        g_vkdown[vk] = 1;
        PostMessageW(w, kd, vk, key_lparam(vk, false));
    }
    if (up) {
        if (down) {
            // keep the key "held" for a few frames so polling code sees it
            int v = vk;
            run_after_frames(3, [v] {
                HWND w2 = (HWND)game_window();
                g_vkdown[v] = 0;
                PostMessageW(w2, (v == VK_MENU || v == VK_F10) ? WM_SYSKEYUP : WM_KEYUP, v,
                             key_lparam(v, true));
            });
        } else {
            g_vkdown[vk] = 0;
            PostMessageW(w, ku, vk, key_lparam(vk, true));
        }
    }
    return true;
}

bool input_text(const std::string& utf8)
{
    HWND w = (HWND)game_window();
    if (!w)
        return false;
    std::wstring t = widen(utf8);
    for (wchar_t c : t)
        PostMessageW(w, WM_CHAR, c, 1);
    return true;
}

bool input_move(int x, int y)
{
    HWND w = (HWND)game_window();
    if (!w)
        return false;
    vcursor_set(x, y); // virtual cursor: the real mouse pointer is not moved
    PostMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
    return true;
}

bool input_click(int x, int y, int button)
{
    HWND w = (HWND)game_window();
    if (!w)
        return false;
    input_move(x, y);
    UINT d = button == 1 ? WM_RBUTTONDOWN : (button == 2 ? WM_MBUTTONDOWN : WM_LBUTTONDOWN);
    UINT u = button == 1 ? WM_RBUTTONUP : (button == 2 ? WM_MBUTTONUP : WM_LBUTTONUP);
    WPARAM mk = button == 1 ? MK_RBUTTON : (button == 2 ? MK_MBUTTON : MK_LBUTTON);
    int vk = button == 1 ? VK_RBUTTON : (button == 2 ? VK_MBUTTON : VK_LBUTTON);
    ensure_hooks();
    // hover for a few frames, press, hold, release (UI code reacts per frame)
    run_after_frames(3, [w, d, mk, x, y, vk] {
        vcursor_set(x, y);
        g_vkdown[vk] = 1;
        PostMessageW(w, d, mk, MAKELPARAM(x, y));
    });
    run_after_frames(6, [w, u, x, y, vk] {
        g_vkdown[vk] = 0;
        PostMessageW(w, u, 0, MAKELPARAM(x, y));
    });
    return true;
}

} // namespace sk
