#include "shared.h"

#include <cmath>
#include <cstdio>

#include "modes.h"
#include "space_ext.h"
#include "sporekit_game.hpp"

namespace shared {

static const SkHost* H;
static SkMod* M;

static uintptr_t S(const char* n) { return H ? H->sym(n) : 0; }

static void* noun_manager()
{
    uintptr_t get = S("Simulator::cGameNounManager::Get");
    return get ? skg::cdecl_call<void*>(get) : nullptr;
}

// ---- resource accessors (nullptr address = not available in this mode/state)
// tribe food = sum of the food bundles in the tribe's cGameBundleContainer (+0x20C, verified:
// cTribe::GetTotalFood 0xC8E850 sums it); bundle list = vector at container +4, amount (float) at
// bundle +0x134. cGameNounManager::mpPlayerTribe is at +0x70.
static void* player_tribe()
{
    void* nm = noun_manager();
    void* t = nm ? skg::field<void*>(nm, 0x70) : nullptr;
    return t && skg::readable(t, 0x230) ? t : nullptr;
}

static bool food_bundles(void* tribe, float**& b, float**& e)
{
    uint8_t* cont = (uint8_t*)tribe + 0x20C;
    b = *(float***)(cont + 4);
    e = *(float***)(cont + 8);
    return b && e >= b && e - b < 64 && skg::readable(b, (e - b) * 4 + 1);
}

static bool g_food(double& v)
{
    void* t = player_tribe();
    float **b, **e;
    if (!t || !food_bundles(t, b, e))
        return false;
    v = 0;
    for (float** it = b; it < e; it++)
        if (*it && skg::readable(*it, 0x138))
            v += *(float*)((uint8_t*)*it + 0x134);
    return true;
}

static void s_food(double v)
{
    void* t = player_tribe();
    float **b, **e;
    double cur;
    if (!t || !food_bundles(t, b, e) || b == e || !g_food(cur))
        return;
    double d = v - cur;
    const float shown = (float)d; // UpdateFoodVisuals shows "+N/-N": it takes the change, not the total
    for (float** it = b; it < e && std::fabs(d) > 1e-4; it++) { // spend from the bundles in order
        if (!*it || !skg::readable(*it, 0x138))
            continue;
        float& amt = *(float*)((uint8_t*)*it + 0x134);
        double n = amt + d;
        if (n < 0) {
            d = n;
            amt = 0;
        } else {
            amt = (float)n;
            d = 0;
        }
    }
    if (uintptr_t f = S("Simulator::cTribe::UpdateFoodVisuals"))
        if (std::fabs(shown) >= 1.f)
            skg::thiscall<void>(f, t, shown);
}

// civilization: GetPlayerCivilization() -> cCivilization::mWealth (+0x98)
static float* civ_money()
{
    void* nm = noun_manager();
    uintptr_t f = S("Simulator::cGameNounManager::GetPlayerCivilization");
    void* civ = nm && f ? skg::thiscall<void*>(f, nm) : nullptr;
    return civ && skg::readable(civ, 0x9C) ? (float*)((uint8_t*)civ + 0x98) : nullptr;
}

// space: GetPlayerEmpire() -> cEmpire::mEmpireMoney (+0xD0, int)
static int* space_money()
{
    uintptr_t f = S("Simulator::GetPlayerEmpire");
    void* emp = f ? skg::cdecl_call<void*>(f) : nullptr;
    return emp && skg::readable(emp, 0xD4) ? (int*)((uint8_t*)emp + 0xD0) : nullptr;
}

struct Res {
    uint8_t id;
    const char* name;
    uint32_t mode;
    bool (*get)(double&);
    void (*set)(double);
    // state
    bool have = false;   // synced at least once in this session
    double synced = 0;   // value after the last sync (guest) / last broadcast (host)
    double pending = 0;  // guest: local change not yet sent
    double last_send = 0;
    double gained = 0, spent = 0; // host: sum of the guests' positive / negative changes
};

static bool g_civ(double& v) { float* p = civ_money(); if (p) v = *p; return p; }
static void s_civ(double v) { if (float* p = civ_money()) *p = (float)v; }
static bool g_space(double& v) { int* p = space_money(); if (p) v = *p; return p; }
static void s_space(double v) { if (int* p = space_money()) *p = (int)std::lround(v); }

static Res g_res[] = {
    {1, "food", 0x1654C02, g_food, s_food},
    {2, "money", 0x1654C04, g_civ, s_civ},
    {3, "money", 0x1654C05, g_space, s_space},
};

static uint32_t g_mode; // the game mode of the last tick: status / debug_add use its resources

// rivals mode (space_rivalry.cpp): every player has its own Space budget, nothing is shared
static bool own_budget(const Res& r) { return r.mode == modes::kSpace && spaceext::rivals(); }

void init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
}

void on_session_change()
{
    for (auto& r : g_res)
        r.have = false;
}

void tick(net::Session& s, uint32_t mode, double now)
{
    g_mode = mode;
    if (s.role() == net::Role::None)
        return;
    for (auto& r : g_res) {
        double v;
        if (own_budget(r)) { // paused: unsent changes stay with the player (space_rivalry.cpp splits)
            r.have = false;
            r.pending = 0;
            continue;
        }
        if (r.mode != mode || !r.get(v)) {
            r.have = false;
            continue;
        }
        if (s.role() == net::Role::Host) {
            // broadcast the total when it changed (and once per 2 s as a safety net)
            if (!r.have || std::fabs(v - r.synced) > 1e-3 || now - r.last_send > 2.0) {
                net::Writer w;
                w.u8(r.id);
                w.f64(v);
                s.broadcast_reliable(net::MSG_RES, w);
                r.synced = v;
                r.have = true;
                r.last_send = now;
            }
            continue;
        }
        // guest: whatever changed locally since the last host total is our own earning/spending
        if (!r.have)
            continue; // wait for the first total from the host
        double d = v - r.synced;
        if (std::fabs(d) > 1e-3) {
            r.pending += d;
            r.synced = v;
        }
        if (r.pending != 0 && now - r.last_send > 0.25) {
            net::Writer w;
            w.u8(r.id);
            w.f64(r.pending);
            s.send_reliable_to(0, net::MSG_RES_ADD, w);
            r.pending = 0;
            r.last_send = now;
        }
    }
}

void on_message(net::Session& s, uint8_t type, uint8_t from, net::Reader& rd)
{
    uint8_t id = rd.u8();
    double v = rd.f64();
    if (!rd.ok() || !std::isfinite(v))
        return;
    for (auto& r : g_res) {
        if (r.id != id)
            continue;
        if (own_budget(r))
            return; // a total or a change sent before the switch to rivals mode: not shared any more
        double cur;
        if (!r.get(cur))
            return;
        if (type == net::MSG_RES && s.role() == net::Role::Client) {
            // keep changes made since our last report: they are on their way to the host
            r.set(v + r.pending);
            r.synced = v + r.pending;
            r.have = true;
        } else if (type == net::MSG_RES_ADD && s.role() == net::Role::Host) {
            if (std::fabs(v) < 1e7)
                r.set(cur + v);
            (v > 0 ? r.gained : r.spent) += v; // tests: what the guests changed
            H->log(M, SK_INFO, "shared %s %+.1f from player %u", r.name, v, from);
        }
        return;
    }
}

std::string status()
{
    std::string out;
    char b[128];
    for (auto& r : g_res) {
        double v;
        // only this mode's resources: in Space the player cCivilization still exists and its
        // money would show up as a second "money" line (and take debug_add's money)
        if (r.mode != g_mode || !r.get(v))
            continue;
        snprintf(b, sizeof b, "shared %s=%.1f synced=%d pending=%.1f from_guests=+%.0f/%.0f\n", r.name, v, r.have,
                 r.pending, r.gained, r.spent);
        out += b;
    }
    return out.empty() ? "no shared resources in this mode\n" : out;
}

bool debug_add(const std::string& name, double v, std::string& err)
{
    for (auto& r : g_res) {
        double cur;
        if (name == r.name && r.mode == g_mode && r.get(cur)) {
            r.set(cur + v);
            return true;
        }
    }
    err = "no shared '" + name + "' here";
    return false;
}

} // namespace shared
