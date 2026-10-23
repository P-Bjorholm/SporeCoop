// Background mode: the game keeps running and rendering when its window is not focused
// (Spore pauses otherwise), and a virtual cursor lets the devkit click without moving
// the real mouse pointer.
#include "core.h"

#include <atomic>

namespace sk {

static bool g_background = true;
static bool g_test_run; // started with a test profile or as instance 2+
static WNDPROC o_WndProc;
static HWND g_sub_hwnd;

std::atomic<bool> g_vcursor_active{false};
std::atomic<int> g_vcursor_x{0}, g_vcursor_y{0};

typedef HWND(WINAPI* GetHwnd_t)(void);
typedef BOOL(WINAPI* GetCursorPos_t)(LPPOINT);
static GetHwnd_t o_GetForegroundWindow, o_GetActiveWindow, o_GetFocus;
static GetCursorPos_t o_GetCursorPos;

static HWND WINAPI h_GetForegroundWindow()
{
    HWND w = (HWND)game_window();
    return (g_background && w) ? w : o_GetForegroundWindow();
}
static HWND WINAPI h_GetActiveWindow()
{
    HWND w = (HWND)game_window();
    return (g_background && w) ? w : o_GetActiveWindow();
}
static HWND WINAPI h_GetFocus()
{
    HWND w = (HWND)game_window();
    return (g_background && w) ? w : o_GetFocus();
}
static BOOL WINAPI h_GetCursorPos(LPPOINT p)
{
    if (g_vcursor_active && p) {
        POINT c = {g_vcursor_x, g_vcursor_y};
        HWND w = (HWND)game_window();
        if (w)
            ClientToScreen(w, &c);
        *p = c;
        return TRUE;
    }
    return o_GetCursorPos(p);
}

static LRESULT CALLBACK h_WndProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_SK_PUMP) {
        pump_main_tasks();
        return 0;
    }
    if (m == WM_SK_PUMP_OUT) {
        pump_outside_tasks();
        return 0;
    }
    if (g_background) {
        switch (m) {
        case WM_ACTIVATEAPP:
            if (!wp)
                return 0; // swallow "deactivated"
            break;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE)
                return 0;
            break;
        case WM_KILLFOCUS:
            return 0;
        case WM_NCACTIVATE:
            if (!wp)
                return TRUE;
            break;
        case WM_SYSCOMMAND:
            // a test window on a workspace nobody looks at (agent-ws): GNOME marks it iconic, Wine
            // minimizes it (window at -32000) and Spore stops rendering and loading for good
            if ((wp & 0xFFF0) == SC_MINIMIZE)
                return 0;
            break;
        }
    }
    return CallWindowProcW(o_WndProc, w, m, wp, lp);
}

void focus_install_hooks()
{
    // default: on for automated test runs (profile/instance switches), off for normal play
    bool test_run = wcsstr(GetCommandLineW(), L"-sporekit-profile:") || instance_id() > 1;
    g_test_run = test_run;
    g_background = ini_int(mods_dirw() + L"\\sporekit.ini", "sporekit", "background_mode", test_run ? 1 : 0) != 0;
    HMODULE u = GetModuleHandleW(L"user32.dll");
    hook_create(&g_kit_mod, (void*)GetProcAddress(u, "GetForegroundWindow"), (void*)h_GetForegroundWindow,
                (void**)&o_GetForegroundWindow);
    hook_create(&g_kit_mod, (void*)GetProcAddress(u, "GetActiveWindow"), (void*)h_GetActiveWindow,
                (void**)&o_GetActiveWindow);
    hook_create(&g_kit_mod, (void*)GetProcAddress(u, "GetFocus"), (void*)h_GetFocus, (void**)&o_GetFocus);
    hook_create(&g_kit_mod, (void*)GetProcAddress(u, "GetCursorPos"), (void*)h_GetCursorPos,
                (void**)&o_GetCursorPos);
}

// The game caps itself at 30 fps (33 ms per frame, slept away in the main loop). Test runs use
// short frames while the game is in its menu when [sporekit] test_frame_ms is set (off by default:
// UI tests and frame-counted waits expect 30 fps). In a game the cap is the game's own again: the
// cell physics, for example, steps per frame and drifts at other frame rates. 0 = no limit;
// frame_ms forces a value everywhere; console: frame.limit <ms> (forced) / frame.limit auto.
static int g_forced = -2; // -2: not read yet, -1: automatic
static int g_menu_ms = -1;
static int g_game_ms = 33; // the game's own value, read before the first change

void set_frame_limit(int ms)
{
    g_forced = ms;
    SKLOG(SK_INFO, "frame limit: %d ms%s", ms, ms < 0 ? " (automatic)" : "");
    apply_frame_limit();
}

void apply_frame_limit()
{
    if (g_forced == -2) {
        std::wstring ini = mods_dirw() + L"\\sporekit.ini";
        g_forced = ini_int(ini, "sporekit", "frame_ms", -1);
        g_menu_ms = g_test_run ? ini_int(ini, "sporekit", "test_frame_ms", -1) : -1;
        if (g_menu_ms >= 0)
            SKLOG(SK_INFO, "frame limit: %d ms in the menus (test run)", g_menu_ms);
    }
    uintptr_t p = sym("App::FrameLimit_ptr");
    uint8_t* app = p ? *(uint8_t**)p : nullptr;
    uint8_t* cfg = app ? *(uint8_t**)(app + 0x3C) : nullptr;
    if (!cfg)
        return;
    static bool read_own;
    if (!read_own) {
        g_game_ms = *(int*)(cfg + 0xB0);
        read_own = true;
    }
    uint32_t mode = game_mode_id();
    bool menu = mode == 0 || mode == 0x2CCD1D2; // startup / GalaxyGameEntry
    int want = g_forced >= 0 ? g_forced : (menu && g_menu_ms >= 0 ? g_menu_ms : g_game_ms);
    if (*(int*)(cfg + 0xB0) != want)
        *(int*)(cfg + 0xB0) = want;
}

// called once the game window is known (device creation)
void focus_attach_window(void* hwnd)
{
    if (!hwnd || g_sub_hwnd == hwnd)
        return;
    g_sub_hwnd = (HWND)hwnd;
    o_WndProc = (WNDPROC)SetWindowLongPtrW(g_sub_hwnd, GWLP_WNDPROC, (LONG_PTR)h_WndProc);
    SKLOG(SK_INFO, "window %p subclassed (background mode %s)", hwnd, g_background ? "on" : "off");
}

static DWORD g_vcursor_last;

void vcursor_set(int x, int y)
{
    g_vcursor_x = x;
    g_vcursor_y = y;
    g_vcursor_active = true;
    g_vcursor_last = GetTickCount();
}

void vcursor_tick()
{
    if (g_vcursor_active && GetTickCount() - g_vcursor_last > 5000)
        g_vcursor_active = false;
}

// Test runs go to a secondary monitor (if there is one) so they do not flash in front of the
// user: [sporekit] test_monitor = auto (secondary) | primary | off. Instance 1 sits at the left
// edge, the others at the right edge of that monitor.

static BOOL CALLBACK monitor_cb(HMONITOR m, HDC, LPRECT, LPARAM lp)
{
    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    if (GetMonitorInfoW(m, &mi))
        ((std::vector<MONITORINFO>*)lp)->push_back(mi);
    return TRUE;
}

void place_test_window()
{
    HWND w = (HWND)game_window();
    if (!g_test_run || !w)
        return;
    std::string mode = ini_str(mods_dirw() + L"\\sporekit.ini", "sporekit", "test_monitor", "auto");
    if (mode == "off" || mode == "primary")
        return;
    std::vector<MONITORINFO> mons;
    EnumDisplayMonitors(nullptr, nullptr, monitor_cb, (LPARAM)&mons);
    const MONITORINFO* target = nullptr;
    for (auto& m : mons)
        if (!(m.dwFlags & MONITORINFOF_PRIMARY))
            target = &m;
    if (!target)
        return; // one monitor: leave the window where it is
    RECT wr;
    GetWindowRect(w, &wr);
    int ww = wr.right - wr.left;
    const RECT& r = target->rcWork;
    int x = instance_id() <= 1 ? r.left : (r.right - ww > r.left ? r.right - ww : r.left);
    if (wr.left == x && wr.top == r.top)
        return;
    SetWindowPos(w, nullptr, x, r.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    SKLOG(SK_INFO, "test window moved to the secondary monitor (%d,%d)", x, (int)r.top);
}

bool background_mode() { return g_background; }
void set_background_mode(bool on) { g_background = on; }

} // namespace sk
