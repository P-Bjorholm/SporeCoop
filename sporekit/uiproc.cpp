// Spore UI (UTFWin) message handlers owned by sporekit: the game keeps IWinProc objects
// on its windows, so their code must outlive hot reloads of the mod that made them.
// When a mod unloads, its handlers stay attached but do nothing.
#include "core.h"

#include <mutex>
#include <vector>

#define SK_THISCALL __attribute__((thiscall))

namespace sk {

struct UiProc {
    void** vt;
    int refs;
    SkMod* mod;
    SkUiMsgFn fn;
    void* user;
    int flags;
};

static std::mutex g_mu;
static std::vector<UiProc*> g_procs;

static int SK_THISCALL p_addref(UiProc* p) { return ++p->refs; }
static int SK_THISCALL p_release(UiProc* p)
{
    int r = --p->refs;
    if (r == 0) {
        std::lock_guard<std::mutex> lk(g_mu);
        for (size_t i = 0; i < g_procs.size(); i++)
            if (g_procs[i] == p) {
                g_procs.erase(g_procs.begin() + i);
                break;
            }
        delete p;
    }
    return r;
}
static void* SK_THISCALL p_dtor(UiProc* p, int flags)
{
    (void)flags; // destroyed through Release only
    return p;
}
static void* SK_THISCALL p_cast(UiProc* p, uint32_t type) { return type == 0x2F009DD0u ? p : nullptr; }
static int SK_THISCALL p_priority(UiProc*) { return 0; }
static int SK_THISCALL p_flags(UiProc* p) { return p->flags; }
static bool SK_THISCALL p_handle(UiProc* p, void* window, const void* msg)
{
    SkUiMsgFn fn = p->fn;
    return fn && msg ? fn(p->user, window, msg) != 0 : false;
}
// MSVC vtable order of UTFWin::IWinProc (Object: AddRef, Release, ~dtor, Cast)
static void* g_vt[] = {(void*)p_addref, (void*)p_release, (void*)p_dtor, (void*)p_cast,
                       (void*)p_priority, (void*)p_flags, (void*)p_handle};

void* ui_winproc_new(SkMod* mod, SkUiMsgFn fn, void* user, int event_flags)
{
    auto* p = new UiProc{g_vt, 1, mod, fn, user, event_flags ? event_flags : 0x81};
    std::lock_guard<std::mutex> lk(g_mu);
    g_procs.push_back(p);
    return p;
}

void ui_winproc_disable(void* proc)
{
    if (proc)
        ((UiProc*)proc)->fn = nullptr;
}

void ui_winprocs_disable_mod(SkMod* mod)
{
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto* p : g_procs)
        if (p->mod == mod)
            p->fn = nullptr;
}

} // namespace sk
