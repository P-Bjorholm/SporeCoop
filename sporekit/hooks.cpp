// MinHook wrapper with per-mod ownership so hot reload can remove a mod's hooks.
#include "core.h"

#include <atomic>
#include <mutex>

#include "MinHook.h"

namespace sk {

struct HookRec {
    SkMod* mod;
    void* target;
    void* detour;
    bool enabled;
};

static std::mutex g_mu;
static std::vector<HookRec> g_hooks;

bool hooks_init()
{
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        SKLOG(SK_ERROR, "MH_Initialize failed: %s", MH_StatusToString(s));
        return false;
    }
    return true;
}

int hook_create(SkMod* mod, void* target, void* detour, void** original)
{
    if (!target)
        return -1;
    std::lock_guard<std::mutex> lk(g_mu);
    MH_STATUS s = MH_CreateHook(target, detour, original);
    if (s != MH_OK) {
        logf(mod, SK_ERROR, "hook_create %p failed: %s", target, MH_StatusToString(s));
        return (int)s;
    }
    s = MH_EnableHook(target);
    if (s != MH_OK) {
        logf(mod, SK_ERROR, "hook_enable %p failed: %s", target, MH_StatusToString(s));
        MH_RemoveHook(target);
        return (int)s;
    }
    g_hooks.push_back({mod, target, detour, true});
    std::string nm = symbols_reverse((uintptr_t)target);
    logf(mod, SK_DEBUG, "hooked %p %s", target, nm.c_str());
    return 0;
}

int hook_enable(SkMod* mod, void* target, int enable)
{
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto& h : g_hooks)
        if (h.target == target && h.mod == mod) {
            MH_STATUS s = enable ? MH_EnableHook(target) : MH_DisableHook(target);
            if (s == MH_OK)
                h.enabled = enable != 0;
            return (int)s;
        }
    return -1;
}

int hook_remove(SkMod* mod, void* target)
{
    std::lock_guard<std::mutex> lk(g_mu);
    for (size_t i = 0; i < g_hooks.size(); i++)
        if (g_hooks[i].target == target && g_hooks[i].mod == mod) {
            MH_SetKeepTrampolines(fixed(g_fix_hookkeep));
            MH_DisableHook(target);
            MH_STATUS s = MH_RemoveHook(target);
            g_hooks.erase(g_hooks.begin() + i);
            return (int)s;
        }
    return -1;
}

// Hooks are removed without stopping the other threads (freezing them deadlocks under Wine). A game
// worker thread can be inside a detour at that moment (coop hooks RandomInt, which worker threads
// call): it may still call the original through the trampoline, so the trampoline's memory is kept
// (MinHook's free list wrote into it and reused it for the next hook; BUG-0132). Returns the count.
int hooks_remove_all(SkMod* mod)
{
    std::lock_guard<std::mutex> lk(g_mu);
    MH_SetKeepTrampolines(fixed(g_fix_hookkeep));
    int n = 0;
    for (size_t i = 0; i < g_hooks.size();) {
        if (g_hooks[i].mod == mod) {
            MH_DisableHook(g_hooks[i].target);
            MH_RemoveHook(g_hooks[i].target);
            g_hooks.erase(g_hooks.begin() + i);
            n++;
        } else
            i++;
    }
    return n;
}

// ---- kit.test hookchurn: threads call a hooked function while its hook is created and removed ----
static SkMod g_churn_mod;
static std::atomic<bool> g_churn_run{false};
static std::atomic<uint32_t> g_churn_calls{0};
using churn_fn = int (*)(int);
static churn_fn volatile o_churn;
static churn_fn volatile g_churn_ptr;

__attribute__((noinline)) static int churn_target(int x)
{
    volatile int y = x;
    y = y * 3 + 1;
    return y;
}
// a detour that takes a while before it calls the original: a thread is inside it most of the time
__attribute__((noinline)) static int churn_detour(int x)
{
    for (int i = 0; i < 400; i++)
        __asm__ volatile("" ::: "memory");
    return o_churn(x);
}
static DWORD WINAPI churn_thread(void*)
{
    while (g_churn_run.load()) {
        g_churn_ptr(1);
        g_churn_calls++;
    }
    return 0;
}

void hooks_churn_test(int rounds, int threads, std::string& out)
{
    g_churn_mod.name = "kit.test";
    g_churn_ptr = churn_target;
    g_churn_calls = 0;
    g_churn_run = true;
    std::vector<HANDLE> ts;
    for (int i = 0; i < threads && i < 8; i++)
        if (HANDLE t = CreateThread(nullptr, 0, churn_thread, nullptr, 0, nullptr))
            ts.push_back(t);
    int made = 0;
    for (int r = 0; r < rounds; r++) {
        void* orig = nullptr;
        if (hook_create(&g_churn_mod, (void*)churn_target, (void*)churn_detour, &orig) == 0)
            made++;
        o_churn = (churn_fn)orig;
        Sleep(2);
        hooks_remove_all(&g_churn_mod);
        Sleep(1);
    }
    g_churn_run = false;
    if (!ts.empty())
        WaitForMultipleObjects((DWORD)ts.size(), ts.data(), TRUE, 5000);
    for (HANDLE t : ts)
        CloseHandle(t);
    char b[160];
    snprintf(b, sizeof b, "hookchurn: %d rounds (%d hooked), %u calls on %d threads, trampolines %s", rounds, made,
             g_churn_calls.load(), (int)ts.size(), fixed(g_fix_hookkeep) ? "kept" : "freed");
    out = b;
}

std::vector<std::string> hooks_describe()
{
    std::lock_guard<std::mutex> lk(g_mu);
    std::vector<std::string> out;
    for (auto& h : g_hooks) {
        char b[256];
        snprintf(b, sizeof b, "%-10s %p %s %s", h.mod->name.c_str(), h.target,
                 h.enabled ? "on " : "off", symbols_reverse((uintptr_t)h.target).c_str());
        out.push_back(b);
    }
    return out;
}

} // namespace sk
