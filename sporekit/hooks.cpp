// MinHook wrapper with per-mod ownership so hot reload can remove a mod's hooks.
#include "core.h"

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
            MH_DisableHook(target);
            MH_STATUS s = MH_RemoveHook(target);
            g_hooks.erase(g_hooks.begin() + i);
            return (int)s;
        }
    return -1;
}

void hooks_remove_all(SkMod* mod)
{
    std::lock_guard<std::mutex> lk(g_mu);
    for (size_t i = 0; i < g_hooks.size();) {
        if (g_hooks[i].mod == mod) {
            MH_DisableHook(g_hooks[i].target);
            MH_RemoveHook(g_hooks[i].target);
            g_hooks.erase(g_hooks.begin() + i);
        } else
            i++;
    }
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
