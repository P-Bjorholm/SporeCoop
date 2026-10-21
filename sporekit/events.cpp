#include "core.h"

#include <mutex>

namespace sk {

struct Sub {
    SkMod* mod;
    SkEventFn fn;
    void* user;
    bool dead;
};

static std::vector<Sub> g_subs[SK_EV_COUNT];
static int g_firing = 0; // re-entrancy guard for deferred erase

int subscribe(SkMod* mod, int event, SkEventFn fn, void* user)
{
    if (event < 0 || event >= SK_EV_COUNT)
        return -1;
    g_subs[event].push_back({mod, fn, user, false});
    return 0;
}

static void sweep(int ev)
{
    auto& v = g_subs[ev];
    for (size_t i = 0; i < v.size();)
        if (v[i].dead)
            v.erase(v.begin() + i);
        else
            i++;
}

void unsubscribe(SkMod* mod, int event, SkEventFn fn, void* user)
{
    if (event < 0 || event >= SK_EV_COUNT)
        return;
    for (auto& s : g_subs[event])
        if (s.mod == mod && s.fn == fn && s.user == user)
            s.dead = true;
    if (!g_firing)
        sweep(event);
}

void unsubscribe_all(SkMod* mod)
{
    for (int e = 0; e < SK_EV_COUNT; e++) {
        for (auto& s : g_subs[e])
            if (s.mod == mod)
                s.dead = true;
        if (!g_firing)
            sweep(e);
    }
}

void fire(int event, void* arg)
{
    g_firing++;
    auto& v = g_subs[event];
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i].dead)
            continue;
        v[i].fn(v[i].user, event, arg);
    }
    g_firing--;
    if (!g_firing)
        sweep(event);
}

} // namespace sk
